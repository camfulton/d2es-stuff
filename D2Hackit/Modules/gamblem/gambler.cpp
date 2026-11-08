#include "gambler.h"
#include <fstream>
#include <algorithm>
#include <cstdlib>
#include <cstring>


// What the Refresh button sends, on the store and the gamble window alike (Pandemonium.dll draws the
// button, GuildHall.dll on the server rerolls the stock): D2's chat packet 0x15, type 0x0001, carrying
// the hidden message "\x01GVR" with empty target strings.
static const BYTE REFRESH_PACKET[] = { 0x15, 0x01, 0x00, 0x01, 'G', 'V', 'R', 0x00, 0x00, 0x00 };

// GuildHall.dll's replies when a refresh is refused. Seeing one ends the run.
static const char *REFRESH_REFUSALS[] =
{
	"Open a vendor's store to refresh it.",
	"Someone else is trading with this vendor.",
	"This gamble window cannot be refreshed.",
};

static const DWORD DEFAULT_REFRESH_COOLDOWN_MS = 500;   // used when gamblem.ini has no usable RefreshCooldownMs

#if 0
	#define DEBUG_TRACE() {server->GameStringf("%s (%s:%d)", __FUNCTION__, __FILE__, __LINE__);}
#else
	#define DEBUG_TRACE() {}
#endif

Gambler::Gambler()
{
	requestedGoldSplitBy = 1;
	isRequestingGold = false;
	transmuteSet = false;
	transmuteRare = false;
	transmuteUnique = false;
	transmuteEnabled = false;

	sellMagic = false;
	sellRare = false;
	sellSet = false;
	sellUnique = false;

	currentState = STATE_UNINITIALIZED;

	sessionOpen = false;
	refreshCooldownMs = DEFAULT_REFRESH_COOLDOWN_MS;
	stateStartTick = 0;
	lastRefreshTick = 0;
	itemsSeen = 0;
	refreshCount = 0;
}

/// <summary>
/// Reads RefreshCooldownMs from gamblem.ini, warning (and using the default) when it is missing or not a number
/// </summary>
void Gambler::ReadConfig()
{
	char cooldownText[32];
	GetPrivateProfileString("GambleM", "RefreshCooldownMs", "", cooldownText, sizeof(cooldownText), CONFIG_PATH);
	char *cooldownEnd = NULL;
	const unsigned long cooldown = strtoul(cooldownText, &cooldownEnd, 10);
	if (cooldownEnd == cooldownText)
	{
		refreshCooldownMs = DEFAULT_REFRESH_COOLDOWN_MS;
		server->GameStringf(GM_TAG "RefreshCooldownMs %s in " CONFIG_PATH ", using %lu ms",
			cooldownText[0] == '\0' ? "not found" : "is not a number", refreshCooldownMs);
	}
	else
	{
		refreshCooldownMs = cooldown;
	}

	server->GameStringf(GM_TAG "Refresh every %lu ms", refreshCooldownMs);
}

bool Gambler::SendRefresh()
{
	BYTE packet[sizeof(REFRESH_PACKET)];
	memcpy(packet, REFRESH_PACKET, sizeof(REFRESH_PACKET));

	return server->GameSendPacketToServer(packet, sizeof(packet)) != FALSE;
}

/// <summary>
/// Closes the gamble window; the next StartGambling opens it again
/// </summary>
void Gambler::CloseGambleWindow()
{
	sessionOpen = false;
	me->EndNpcSession();
}

/// <summary>
/// Initializes the gambling process
/// </summary>
/// <param name="itemCodes">List of item codes we're gambling for</param>
/// <returns>true on success, false on failure (no gambling npc)</returns>
bool Gambler::Init(const std::vector<std::string> &itemCodes)
{
	DEBUG_TRACE();
	itemsToGamble.clear();

	for(int i = 0; i < (int)itemCodes.size(); i++)
	{
		itemsToGamble.insert(itemCodes[i]);
	}

	server->GameStringf("Gambling for types: ");
	for each(auto item in itemCodes)
	{
		server->GameStringf("    [\xFF" "c:%s\xFF" "c0] %s", item.c_str(), server->GetItemName(item.c_str()));
	}

	if(noSellList.size() > 0)
	{
		isUsingNoSellList = true;

		server->GameStringf("\xFF" "c1Warning\xFF" "c0: Selling all items that are not of type: ");
		for each(auto item in noSellList)
		{
			server->GameStringf("    [\xFF" "c1%s\xFF" "c0] %s", item.c_str(), server->GetItemName(item.c_str()));
		}
	}

	gamblingNpc.dwUnitType = UNIT_TYPE_MONSTER;
	gamblingNpc.dwUnitID = FindGamblingNpc();

	if(gamblingNpc.dwUnitID == 0)
	{
		server->GamePrintString("\xFF" "c3GambleM\xFF" "c0: Failed to find gambling npc");
		return false;
	}

	ResetQueues();
	while(!itemsToSell.empty())
	{
		itemsToSell.pop();
	}

	ReadConfig();
	lastRefreshTick = 0;
	itemsSeen = 0;
	refreshCount = 0;

	return true;
}

/// <summary>
/// Stops the entire gambling process.
/// </summary>
void Gambler::StopGambling()
{
	if(currentState == STATE_UNINITIALIZED)
		return;

	DEBUG_TRACE();

	int totalGambled = stats.magic + stats.rares + stats.sets + stats.uniques;

	if(totalGambled > 0)
	{
		server->GameStringf("\xFF" "c3GambleM\xFF" "c0: \xFF" "c3Magic: %d (%d%%)  \xFF" "c9Rare: %d (%d%%)  \xFF" "c2Set: %d (%d%%)  \xFF" "c4Unique: %d (%d%%)\xFF" "c0",
			stats.magic, (int)(100 * (stats.magic / (float)totalGambled)),
			stats.rares, (int)(100 * (stats.rares / (float)totalGambled)),
			stats.sets, (int)(100 * (stats.sets / (float)totalGambled)),
			stats.uniques, (int)(100 * (stats.uniques / (float)totalGambled)));
	}

	server->GameStringf(GM_TAG "Gamble ended (%d refreshes)", refreshCount);
	me->CleanJobs();
	CloseGambleWindow();
		
	ResetQueues();
	while(!itemsToSell.empty())
	{
		itemsToSell.pop();
	}

	memset(&stats, 0, sizeof(GambleStats));
	currentState = STATE_UNINITIALIZED;
}

/// <summary>
/// Resets the queuest of items we are trying to buy from the current npc session
/// </summary>
void Gambler::ResetQueues()
{
	DEBUG_TRACE();

	while(!gambleQueue.empty())
	{
		gambleQueue.pop();
	}
}

/// <summary>
/// Attempts to make more inventory room by either selling or transmuting unwanted gambled items.
/// </summary>
void Gambler::AttemptToMakeRoom()
{
	DEBUG_TRACE();

	if(!itemsToSell.empty())
	{
		SellQueuedItems();
		return;
	}

	server->GameStringf("\xFF" "c3GambleM\xFF" "c0: Not enough room");
	if(transmuteEnabled && currentState != STATE_AUTOSTOCK_ENDED)
	{
		if(me->GetOpenedUI())
		{
			CloseGambleWindow();
			currentState = STATE_UI_CLOSING_FOR_AUTOSTOCKER;
			return;
		}

		StartAutostocker();
		return;
	}

	StopGambling();
}

/// <summary>
/// Begins another iteration of gambling. Inventory space remaining is checked prior to gambling and attempts
///   are made to free some room if possible. Gambling ends if inventory is completely full.
/// </summary>
bool Gambler::StartGambling()
{
	DEBUG_TRACE();

	for(std::unordered_set<std::string>::iterator i = itemsToGamble.begin(); i != itemsToGamble.end(); ++i)
	{
		if(!WillItemFit((*i).c_str()))
		{
			AttemptToMakeRoom();
			return false;
		}
	}

	// The window is already open: press Refresh to reroll it instead of closing and reopening it
	if(sessionOpen)
	{
		currentState = STATE_REFRESH_COOLDOWN;
		return true;
	}

	currentState = STATE_NPC_LISTINGITEMS;
	if(!me->StartNpcSession(&gamblingNpc, NPC_GAMBLE))
	{
		server->GamePrintInfo("\xFF" "c3GambleM\xFF" "c0: Failed to start gambling");
		me->RedrawClient(false);
		StartGambling();
		return false;
	}

	return true;
}

/// <summary>
/// Attempts to request more gold by either asking our money bot or selling previously purchesd items.
///   Gambling is ended if the previous two options are not available.
/// </summary>
void Gambler::RequestMoreGold()
{
	DEBUG_TRACE();

	if(isRequestingGold)
	{
		me->CleanJobs();
		CloseGambleWindow();
		ResetQueues();
		currentState = STATE_GOLD_WAIT;

		char goldRequest[32];

		sprintf_s(goldRequest, sizeof(goldRequest)/sizeof(goldRequest[0]), "%d", (me->GetInventoryGoldLimit() - me->GetStat(STAT_GOLD))/requestedGoldSplitBy);

		server->GameStringf("Looking for gold...");
		me->Say(goldRequest);				
	}
	else
	{
		if(!itemsToSell.empty())
		{
			SellQueuedItems();
		}
		else
		{
			StopGambling();
		}
	}
}

/// <summary>
/// Starts the autostocker. Completion of the autostocker is handled in the
///   OnAutostockerEnded event handler.
/// </summary>
void Gambler::StartAutostocker()
{
	DEBUG_TRACE();

	currentState = STATE_AUTOSTOCK_RUNNING;

	std::string asCommand;

	while(!gambleQueue.empty())
		gambleQueue.pop();

	asCommand = server->GetCommandCharacter();
	asCommand += "as start_rares -cx1 -cx2 -cx3 -jew chat";

	if(transmuteSet)
		asCommand += " sets";
	if(transmuteRare)
		asCommand += " rares";
	if(transmuteUnique)
		asCommand += " uniques";

	me->Say(asCommand.c_str());
}

/// <summary>
/// Attempts to sell a previously gambled item. Successful selling of the item will
///   change to the STATE_GAMBLE_SOLDITEM state, which will be handled in the OnItemSold
///   event handler.
/// </summary>
void Gambler::SellQueuedItems()
{
	DEBUG_TRACE();

	if(itemsToSell.empty())
	{
		StartGambling();
		return;
	}
	
	DWORD currentItemId = itemsToSell.front();
	itemsToSell.pop();

	if(!server->VerifyUnit(&gamblingNpc))
	{
		server->GamePrintInfo("\xFF" "c3GambleM\xFF" "c0: Could not verify npc unit");
		StopGambling();
		return;
	}

	currentState = STATE_GAMBLE_SOLDITEM;
	if(!me->SellItem(currentItemId))
	{
		server->GameStringf("\xFF" "c3GambleM\xFF" "c0: Unable to sell item");
		return;
	}
}

/// <summary>
/// Locates the nearest gambling NPC and returns its unique ID
/// </summary>
/// <returns>ID of the closest gambling NPC or 0 on failure</returns>
DWORD Gambler::FindGamblingNpc()
{
	DEBUG_TRACE();

	GAMEUNIT gameUnit;
	const char *gamblingNpcNames[] = 
	{
		"Gheed",
		"Elzix",
		"Alkor",
		"Jamella",
		"Nihlathak",
		"Anya"
	};

	for(int i = 0; i < sizeof(gamblingNpcNames)/sizeof(gamblingNpcNames[0]); i++)
	{
		if(server->FindUnitByName(gamblingNpcNames[i], UNIT_TYPE_MONSTER, &gameUnit))
		{
			return gameUnit.dwUnitID;
		}
	}

	return 0;
}

/// <summary>
/// Toggles requesting of the maximum amount of gold when gold runs out
/// </summary>
/// <param name="splitBy">Number of players listening to our request. Total amount requested is divided by this amount</param>
void Gambler::ToggleRequestGold(int splitBy)
{
	DEBUG_TRACE();

	isRequestingGold = !isRequestingGold;
	requestedGoldSplitBy = splitBy;

	if(isRequestingGold)
		server->GameStringf("\xFF" "c3GambleM\xFF" "c0: Gold requesting \xFF" "c2enabled\xFF" "c0, split by %d", splitBy);
	else
		server->GamePrintInfo("\xFF" "c3GambleM\xFF" "c0: Gold requesting \xFF" "c1disabled\xFF" "c0");
}

/// <summary>
/// Toggles the selling gambled items when inventory becomes full during gambling
/// </summary>
/// <param name="sellMagic">Magic items will be sold</param>
/// <param name="sellSet">Set items will be sold</param>
/// <param name="sellRare">Rare items will be sold</param>
/// <param name="sellUnique">Unique items will be sold</param>
void Gambler::ToggleGambleSell(bool sellMagic, bool sellSet, bool sellRare, bool sellUnique)
{
	DEBUG_TRACE();

	this->sellMagic = sellMagic;
	this->sellRare = sellRare;
	this->sellSet = sellSet;
	this->sellUnique = sellUnique;

	while(!itemsToSell.empty())
	{
		itemsToSell.pop();
	}

	if(sellMagic || sellSet || sellRare || sellUnique)
	{
		server->GameStringf("\xFF" "c3GambleM\xFF" "c0: Selling %s%s%s%s", sellMagic?" \xFF" "c3Magic":"", sellSet?" \xFF" "c2Sets":"", sellRare?" \xFF" "c9Rares":"", sellUnique?" \xFF" "c4Uniques":"");
	}
	else
	{
		server->GamePrintInfo("\xFF" "c3GambleM\xFF" "c0: Selling gambled items \xFF" "c1disabled\xFF" "c0");
	}
}

/// <summary>
/// Sets the list of items that should not be sold when gambling. All other item types will be sold. ToggleSell will still
///  sell all items of a specific type. Set ToggleSell to magic|set|rare and NoSell to specific item code to sell all items
///  that are not unique AND the specific item code.
/// </summary>
/// <param name="noSellList">List of items to be kept</param>
void Gambler::SetNoSellList(const std::vector<std::string> &noSellList)
{
	this->noSellList.clear();

	server->GameStringf("Selling all items (that aren't filtered by .gamblem sell) of type: ");
	for each(auto item in noSellList)
	{
		this->noSellList.insert(item);
		server->GameStringf("    [\xFF" "c1%s\xFF" "c0] %s", item.c_str(), server->GetItemName(item.c_str()));
	}
}

/// <summary>
/// Toggles the usage of AutoStocker when inventory becomes full during gambling
/// </summary>
/// <param name="transmuteSet">Set items will be transmuted</param>
/// <param name="transmuteRare">Rare items will be transmuted</param>
/// <param name="transmuteUnique">Unique items will be transmuted</param>
void Gambler::ToggleAutostock(bool transmuteSet, bool transmuteRare, bool transmuteUnique)
{
	DEBUG_TRACE();

	transmuteEnabled = !transmuteEnabled;

	if(transmuteEnabled)
	{
		this->transmuteSet = transmuteSet;
		this->transmuteRare = transmuteRare;
		this->transmuteUnique = transmuteUnique;
		server->GameStringf("\xFF" "c3GambleM\xFF" "c0: Autostocking \xFF" "c3Magic%s%s%s", transmuteSet?" \xFF" "c2Sets":"", transmuteRare?" \xFF" "c9Rares":"", transmuteUnique?" \xFF" "c4Uniques":"");
	}
	else
	{
		this->transmuteSet = false;
		this->transmuteRare = false;
		this->transmuteUnique = false;
		server->GamePrintInfo("\xFF" "c3GambleM\xFF" "c0: Autostock \xFF" "c1disabled\xFF" "c0");
	}
}

/// <summary>
/// Check to see if an exsiting item can fit in our inventory
/// </summary>
/// <param name="dwItemId">Unique ID of the item being checked</param>
/// <returns>True if item can fit, false if it won't fit</returns>
bool Gambler::WillItemFit(DWORD dwItemId)
{
	DEBUG_TRACE();

	char itemCode[4];

	if(!server->GetItemCode(dwItemId, itemCode, 4))
	{
		server->GameErrorf("WillItemFit: Failed to get item code");
	}
	else
	{
		return me->FindFirstStorageSpace(STORAGE_INVENTORY, server->GetItemSize(itemCode), NULL) == TRUE;
	}

	return false;
}

/// <summary>
/// Check to see if an item of specified item code can fit in our inventory
/// </summary>
/// <param name="itemCode">Code of item being checked</param>
/// <returns>True if item can fit, false if it won't fit</returns>
bool Gambler::WillItemFit(const char *itemCode)
{
	DEBUG_TRACE();

	return me->FindFirstStorageSpace(STORAGE_INVENTORY, server->GetItemSize(itemCode), NULL) == TRUE;
}

/// <summary>
/// Attempt to buy all of the items we're interested in. If we're full we will attempt to sell any
///   previously purchased items (if enabled)
/// </summary>
void Gambler::GambleQueuedItems()
{
	DEBUG_TRACE();

	if(gambleQueue.empty())
	{
		StartGambling();
		return;
	}
	
	// Don't pop the item from the queue, we might be able to attempt another gamble after
	//  selling items
	DWORD currentItemId = gambleQueue.front();
	if(!WillItemFit(currentItemId))
	{
		SellQueuedItems();
		return;
	}
	gambleQueue.pop();

	if(!server->VerifyUnit(&gamblingNpc))
	{
		server->GamePrintInfo("\xFF" "c3GambleM\xFF" "c0: Could not verify npc unit");
		StopGambling();
		return;
	}

	currentState = STATE_GAMBLE_WAITFORITEM;
	if(!me->Gamble(currentItemId))
	{
		server->GamePrintInfo("\xFF" "c3GambleM\xFF" "c0: Unable to buy item");
		ResetQueues();
		StartGambling();
		return;
	}
}

/// <summary>
/// NPC is listing their inventory
/// </summary>
/// <param name="gambleItem">Item that can be purchased</param>
void Gambler::OnNpcGambleItemList(ITEM &gambleItem)
{
	if(currentState != STATE_NPC_LISTINGITEMS && currentState != STATE_REFRESH_LISTING)
		return;

	if(currentState == STATE_REFRESH_LISTING)
	{
		itemsSeen++;
	}

	//DEBUG_TRACE();
	if(itemsToGamble.count(gambleItem.szItemCode) > 0)
	{
		gambleQueue.push(gambleItem.dwItemID);
		ticksTillGambleItemTimeout = 30 * (1000 / server->GetTickRate()); // 30 second timeout
	}
}

/// <summary>
/// We don't have enough money to buy the item(s), request more gold
/// </summary>
void Gambler::OnNotEnoughMoney()
{
	if(currentState != STATE_GAMBLE_WAITFORITEM)
		return;

	DEBUG_TRACE();
	server->GameStringf("\xFF" "c3GambleM\xFF" "c0: Not enough money");
	RequestMoreGold();
}

/// <summary>
/// Item was sold to the vendor. Event occurs after game has processed it.
/// </summary>
void Gambler::OnItemSold()
{
	if(currentState != STATE_GAMBLE_SOLDITEM)
		return;

	DEBUG_TRACE();
	GambleQueuedItems();
}

/// <summary>
/// Process a single tick. Handles timeouts/etc
/// </summary>
void Gambler::OnTick()
{
	const DWORD now = GetTickCount();

	switch(currentState)
	{
		case STATE_REFRESH_COOLDOWN:
		{
			// Closed by something else (e.g. the player): open it again
			if(!sessionOpen)
			{
				StartGambling();
				break;
			}

			if(lastRefreshTick != 0 && now - lastRefreshTick < refreshCooldownMs)
			{
				break;
			}

			ResetQueues();
			itemsSeen = 0;

			if(!SendRefresh())
			{
				server->GameStringf(GM_TAG "Failed to send the Refresh request");
				StopGambling();
				break;
			}

			refreshCount++;
			lastRefreshTick = now;
			stateStartTick = now;
			currentState = STATE_REFRESH_LISTING;
			break;
		}
		case STATE_REFRESH_LISTING:
		{
			// The server sends the new stock one item at a time with no end marker, so listen
			// for the whole cooldown; nothing that arrives before the next press is missed.
			if(now - stateStartTick < refreshCooldownMs)
			{
				break;
			}

			if(itemsSeen == 0)
			{
				server->GameStringf(GM_TAG "Refresh %d: Refreshed before server could respond, try increasing RefreshCooldownMs in " CONFIG_PATH, refreshCount);
			}
			else
			{
				server->GameStringf(GM_TAG "Refresh %d: %d items, %d to gamble", refreshCount, itemsSeen, (int)gambleQueue.size());
			}

			GambleQueuedItems();
			break;
		}
		case STATE_GAMBLE_WAITFORITEM:
		{
			if(ticksTillGambleItemTimeout-- == 0)
			{
				server->GamePrintInfo("\xFF" "c3GambleM\xFF" "c0: Timed out buying items");
				ResetQueues();
				StartGambling();
			}
			break;
		}
	}
}

/// <summary>
/// An item was placed in the player's inventory. If we're currently gambling then the item
///   is processed, otherwise it's ignored.
/// </summary>
/// <param name="gambleItem">Item that was placed in our inventory.</param>
void Gambler::OnItemToStorage(ITEM &gambleItem)
{
	if(currentState != STATE_GAMBLE_WAITFORITEM)
		return;

	DEBUG_TRACE();
	std::string itemName = server->GetItemName(gambleItem.szItemCode);

	if(gambleItem.iQuality == ITEM_LEVEL_UNIQUE)
	{
		server->GameStringf("Gamble: \xFF" "c4Unique %s", itemName.c_str());
		stats.uniques++;

		if(sellUnique)
		{
			itemsToSell.push(gambleItem.dwItemID);
		}
		else if(isUsingNoSellList && (noSellList.count(gambleItem.szItemCode) == 0))
		{
			itemsToSell.push(gambleItem.dwItemID);
		}
	}
	else if(gambleItem.iQuality == ITEM_LEVEL_SET)
	{
		server->GameStringf("Gamble: \xFF" "c2Set %s", itemName.c_str());
		stats.sets++;

		if(sellSet)
		{
			itemsToSell.push(gambleItem.dwItemID);
		}
		else if(isUsingNoSellList && (noSellList.count(gambleItem.szItemCode) == 0))
		{
			itemsToSell.push(gambleItem.dwItemID);
		}
	}
	else if(gambleItem.iQuality == ITEM_LEVEL_RARE)
	{
		server->GameStringf("Gamble: \xFF" "c9Rare %s", itemName.c_str());
		stats.rares++;

		if(sellRare)
		{
			itemsToSell.push(gambleItem.dwItemID);
		}
		else if(isUsingNoSellList && (noSellList.count(gambleItem.szItemCode) == 0))
		{
			itemsToSell.push(gambleItem.dwItemID);
		}
	}
	else if(gambleItem.iQuality == ITEM_LEVEL_MAGIC)
	{
		server->GameStringf("Gamble: \xFF" "c3Magic %s", itemName.c_str());
		stats.magic++;

		if(sellMagic)
		{
			itemsToSell.push(gambleItem.dwItemID);
		}
		else if(isUsingNoSellList && (noSellList.count(gambleItem.szItemCode) == 0))
		{
			itemsToSell.push(gambleItem.dwItemID);
		}
	}
	else
	{
		server->GameStringf("Gamble: \xFF" "c2Unknown[%02X] %s", gambleItem.iQuality, itemName.c_str());
	}
}

/// <summary>
/// *Should* be called whenever the npc has completed listing all of their items. Possible to miss
///   some since d2hackit waits 1000ms after every item from the merchant (resetting to 1000ms every
///   time a new item packet comes in) before this message is sent
/// </summary>
/// <param name="success">True if session was successfully established with NPC.</param>
void Gambler::OnNpcSession(int success)
{
	if(currentState != STATE_NPC_LISTINGITEMS)
		return;

	DEBUG_TRACE();
	if (!success)
	{
		auto myPos = me->GetPosition();

		me->MoveTo(myPos.x + (3 - rand() % 5), myPos.y + (3 - rand() % 5), false);
		FindGamblingNpc();
		me->RedrawClient(FALSE);
		me->MoveToUnit(&gamblingNpc, FALSE);
		server->GamePrintInfo("\xFF" "c3GambleM\xFF" "c0: NPC request failed");
		StartGambling();
		return;
	}

	sessionOpen = true;
	GambleQueuedItems();
}

/// <summary>
/// We have picked up more gold and are ready to gamble more
/// </summary>
void Gambler::OnGoldPickup()
{
	if(currentState != STATE_GOLD_WAIT)
		return;

	DEBUG_TRACE();
	StartGambling();
}

/// <summary>
/// Autostocker has completed, we will continue gambling if we requested autostocker to run
///   previously
/// </summary>
bool Gambler::OnAutostockerMessage(const std::string_view &message)
{
	if (currentState != STATE_AUTOSTOCK_RUNNING)
	{
		return false;
	}

	if (message == "Autostocker Ended")
	{
		DEBUG_TRACE();
		StartGambling();
	}

	return true;
}

/// <summary>
/// The 'item identified' swish sound has been scheduled to play, this is the last identifiable packet that is 
///   sent done before we can continue gambling the rest of our items. If we gamble again before this packet then
///   we might get kicked
/// </summary>
void Gambler::OnItemIdentifiedSounded()
{
	if(currentState != STATE_GAMBLE_WAITFORITEM)
		return;

	DEBUG_TRACE();
	GambleQueuedItems();
}

/// <summary>
/// All UIs have been closed and we're able to start the autostocker now if we're waiting on it
/// </summary>
void Gambler::OnUIClosed()
{
	sessionOpen = false;

	if(currentState != STATE_UI_CLOSING_FOR_AUTOSTOCKER)
		return;

	DEBUG_TRACE();
	StartAutostocker();
}

/// <summary>
/// Watches the server's messages while refreshing, so a refused refresh stops the run
///   instead of pressing the button forever.
/// </summary>
void Gambler::OnServerPacket(const BYTE *aPacket, DWORD aLen)
{
	if(currentState != STATE_REFRESH_LISTING && currentState != STATE_REFRESH_COOLDOWN)
		return;

	const char *begin = reinterpret_cast<const char *>(aPacket);
	const char *end = begin + aLen;

	for(const char *refusal : REFRESH_REFUSALS)
	{
		const char *refusalEnd = refusal + strlen(refusal);
		if(std::search(begin, end, refusal, refusalEnd) != end)
		{
			server->GameStringf(GM_TAG "The server refused the refresh: %s", refusal);
			StopGambling();
			return;
		}
	}
}
