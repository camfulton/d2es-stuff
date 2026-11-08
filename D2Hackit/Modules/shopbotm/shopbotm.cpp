#include "shopbotm.h"
#include <algorithm>
#include <fstream>
#include <string>
#include <cstdlib>
#include <cctype>
#include <set>
#include "../common/ItemLookup.h"

// D2HackIt's item list ("code name" per line); chits are named "(<base>) Gambling"
#define ALL_ITEMS_PATH ".\\plugin\\AllItems.txt"
// Which exceptional and elite bases belong to each normal base ("<normal> <exceptional> <elite>")
#define TIERS_PATH ".\\plugin\\shopbotm_tiers.txt"

// What the store panel's Refresh button sends (Pandemonium.dll draws the button, GuildHall.dll on
// the server rerolls the stock). It is D2's chat packet 0x15, type 0x0001, carrying the hidden
// message "\x01GVR" with empty target strings: 15 01 00 | 01 'G' 'V' 'R' 00 | 00 | 00.
static const BYTE REFRESH_PACKET[] = { 0x15, 0x01, 0x00, 0x01, 'G', 'V', 'R', 0x00, 0x00, 0x00 };

// GuildHall.dll's replies when a refresh is refused. Seeing one ends the run.
static const char *REFRESH_REFUSALS[] =
{
	"Open a vendor's store to refresh it.",
	"Someone else is trading with this vendor.",
	"This gamble window cannot be refreshed.",
};

static const int MAX_SESSION_ATTEMPTS = 3;
static const DWORD PURCHASE_TIMEOUT_MS = 3000;
static const DWORD DEFAULT_REFRESH_COOLDOWN_MS = 500;   // used when shopbotm.ini has no usable RefreshCooldownMs
static const DWORD START_DELAY_MS = 100;   // pause after closing open windows before talking to the merchant

ShopBot::ShopBot()
{
	currentState = STATE_UNINITIALIZED;

	memset(&merchantNpc, 0, sizeof(GAMEUNIT));
	merchantName = "";
	sessionAttempts = 0;
	refreshCount = 0;
}

bool ShopBot::LoadItemMap(const std::string& fileName, std::unordered_map<std::string, std::string>& itemMap)
{
	std::ifstream inFile(fileName);
	if (!inFile)
	{
		server->GameStringf(SBM_TAG "Failed to open file %s", fileName.c_str());
		return false;
	}

	itemMap.clear();

	std::string readBuff;
	while (std::getline(inFile, readBuff))
	{
		if (readBuff.length() <= 4 || readBuff.at(3) != ' ')
		{
			continue;
		}

		const auto itemName = readBuff.substr(0, 3);
		const auto itemDesc = readBuff.substr(4);

		itemMap.insert({ itemName, itemDesc });
	}

	return true;
}

// Chits are named "(<base>) Gambling"; returns the base, or "" for any other item
static std::string ChitBase(const std::string &name)
{
	static const std::string CHIT_SUFFIX = ") gambling";
	const std::string lowerName = ItemLookup::ToLower(name);

	if (lowerName.size() <= CHIT_SUFFIX.size() + 1 || lowerName[0] != '(' ||
		lowerName.compare(lowerName.size() - CHIT_SUFFIX.size(), CHIT_SUFFIX.size(), CHIT_SUFFIX) != 0)
	{
		return "";
	}

	return name.substr(1, name.size() - 1 - CHIT_SUFFIX.size());
}

// AllItems.txt and shopbotm_tiers.txt, plus the chits indexed by their base
struct ChitTable
{
	ItemLookup::Table items;
	std::unordered_map<std::string, std::vector<std::string>> chitsByBase;   // lower-case base -> chit codes
	std::set<std::string> chitBases;                                          // as written, for suggestions
};

static bool LoadChitTable(ChitTable &table)
{
	if (!ItemLookup::LoadNames(table.items, ALL_ITEMS_PATH, SBM_TAG))
	{
		return false;
	}
	ItemLookup::LoadTiers(table.items, TIERS_PATH, SBM_TAG);

	for (const auto &item : table.items.names)
	{
		const std::string base = ChitBase(item.second);
		if (!base.empty())
		{
			table.chitsByBase[ItemLookup::ToLower(base)].push_back(item.first);
			table.chitBases.insert(base);
		}
	}

	return true;
}

// Adds the chits for one item code to a reading: a chit code itself, or any tier of a base
static bool AddChitsForCode(const ChitTable &table, const std::string &code, ItemLookup::Reading &reading)
{
	const std::string name = ItemLookup::NameOf(table.items, code);
	if (!ChitBase(name).empty())
	{
		reading.items[code] = name;
		return true;
	}

	std::string normal = code;
	std::string chitBase;
	int tier = 0;

	const auto link = table.items.tiers.find(code);
	if (link != table.items.tiers.end())
	{
		normal = link->second.normal;
		chitBase = link->second.chitBase;
		tier = link->second.tier;
	}
	if (chitBase.empty())
	{
		chitBase = ItemLookup::NameOf(table.items, normal);
	}

	const auto found = table.chitsByBase.find(ItemLookup::ToLower(chitBase));
	if (found == table.chitsByBase.end())
	{
		return false;
	}

	if (tier > 0)
	{
		char note[256];
		sprintf_s(note, "%s (%s) is the %s %s: shopping for (%s) Gambling chits",
			name.c_str(), code.c_str(), ItemLookup::TIER_NAMES[tier], ItemLookup::NameOf(table.items, normal).c_str(), chitBase.c_str());
		reading.notes.push_back(note);
	}

	for (const auto &chitCode : found->second)
	{
		reading.items[chitCode] = ItemLookup::NameOf(table.items, chitCode);
	}
	return true;
}

bool ShopBot::FindChits(const std::vector<std::string> &args, std::unordered_map<std::string, std::string> &chits)
{
	chits.clear();

	ItemLookup::Input input;
	if (!ItemLookup::ParseInput(args, input, SBM_TAG))
	{
		return false;
	}

	ChitTable table;
	if (!LoadChitTable(table))
	{
		return false;
	}

	// Item codes, like the shop command: base codes of any tier, or chit codes themselves
	ItemLookup::Reading codes;
	if (input.couldBeCodes && !input.forceName)
	{
		codes.valid = true;
		for (const auto &code : input.codes)
		{
			codes.known = codes.known && ItemLookup::IsKnownCode(table.items, code);
			if (!AddChitsForCode(table, code, codes))
			{
				codes.valid = false;
				if (ItemLookup::IsKnownCode(table.items, code))
				{
					codes.error = "No chits found for " + code + " (" + ItemLookup::NameOf(table.items, code) + ")";
				}
				break;
			}
		}
	}

	// A name: a chit's own base ("light plate"), or any item with that name ("hood", the elite Cap)
	ItemLookup::Reading name;
	if (!input.forceCodes)
	{
		const std::string wanted = ItemLookup::ToLower(input.phrase);
		const auto base = table.chitsByBase.find(wanted);
		if (base != table.chitsByBase.end())
		{
			for (const auto &chitCode : base->second)
			{
				name.items[chitCode] = ItemLookup::NameOf(table.items, chitCode);
			}
		}
		else
		{
			const auto named = table.items.byName.find(wanted);
			if (named != table.items.byName.end())
			{
				for (const auto &code : named->second)
				{
					AddChitsForCode(table, code, name);   // several codes can share a name; some may have no chits
				}
			}
		}
		name.valid = !name.items.empty();
	}

	bool explained = false;
	const ItemLookup::Reading *chosen = ItemLookup::Choose(input, codes, name, explained, SBM_TAG);
	if (chosen == nullptr)
	{
		if (!explained)
		{
			if (!codes.error.empty() && (input.forceCodes || !input.forceName))
			{
				server->GameStringf(SBM_TAG "%s", codes.error.c_str());
			}
			else
			{
				ItemLookup::Suggest(table.items, input.phrase, table.chitBases, "chits", SBM_TAG);
			}
		}
		return false;
	}

	for (const auto &note : chosen->notes)
	{
		server->GameStringf(SBM_TAG "%s", note.c_str());
	}
	for (const auto &item : chosen->items)
	{
		chits[item.first] = item.second;
	}
	return true;
}

// itemsToShopFor maps item code -> name; when empty, the list is read from shopbotm_items.txt
bool ShopBot::Start(const std::unordered_map<std::string, std::string> &itemsToShopFor)
{
	if (currentState != STATE_UNINITIALIZED)
	{
		server->GameStringf(SBM_TAG "Already running (press Space to stop)");
		return false;
	}

	memset(&merchantNpc, 0, sizeof(GAMEUNIT));
	merchantName = "";
	targetItems.clear();

	while (!gambleQueue.empty()) {
		gambleQueue.pop();
	}

	if (itemsToShopFor.empty())
	{
		if (!LoadItemMap(".\\plugin\\shopbotm_items.txt", targetItems))
		{
			return false;
		}
	}
	else
	{
		targetItems = itemsToShopFor;
	}

	std::string itemList = "";
	for (const auto& iter : targetItems) {
		itemList += " " + iter.first;
	}
	server->GameStringf(SBM_TAG "Shopping for:%s", itemList.c_str());

	// Read as text first so a missing or mistyped setting can be reported, not silently replaced
	char cooldownText[32];
	GetPrivateProfileString("ShopBotM", "RefreshCooldownMs", "", cooldownText, sizeof(cooldownText), CONFIG_PATH);
	char *cooldownEnd = NULL;
	const unsigned long cooldown = strtoul(cooldownText, &cooldownEnd, 10);
	if (cooldownEnd == cooldownText)
	{
		refreshCooldownMs = DEFAULT_REFRESH_COOLDOWN_MS;
		server->GameStringf(SBM_TAG "RefreshCooldownMs %s in " CONFIG_PATH ", using %lu ms",
			cooldownText[0] == '\0' ? "not found" : "is not a number", refreshCooldownMs);
	}
	else
	{
		refreshCooldownMs = cooldown;
	}

	auto fleeModule = GetModuleHandle("Flee.d2h");
	if (fleeModule != NULL)
	{
		server->GameCommandf("flee SuppressAutoTp 1");
	}

	server->GameStringf(SBM_TAG "Refresh every %lu ms", refreshCooldownMs);

	lastRefreshTick = 0;
	storeItemsSeen = 0;
	refreshCount = 0;
	sessionAttempts = 0;

	// Start from a clean slate: close whatever is open, then open the nearest merchant's store.
	me->CloseAllUIs();
	stateStartTick = GetTickCount();
	currentState = STATE_START_SESSION;
	return true;
}

void ShopBot::Stop()
{
	if(currentState != STATE_COMPLETE && currentState != STATE_UNINITIALIZED)
	{
		me->CleanJobs();

		Finish("stopped");
	}
}

void ShopBot::Finish(const char *reason)
{
	if (reason != NULL)
	{
		server->GameStringf(SBM_TAG "%s", reason);
	}

	currentState = STATE_COMPLETE;
}

DWORD ShopBot::FindMerchant()
{
	GAMEUNIT gameUnit;

	if(merchantName.length() == 0)
	{
		const char *merchantNames[] =
		{
			"charsi",
			"gheed",
			"akara",
			"elzix",
			"lysander",
			"fara",
			"drognan",
			"alkor",
			"asheara",
			"ormus",
			"hratli",
			"jamella",
			"halbu",
			"larzuk",
			"malah",
			"anya",
		};

		DWORD closestNpcID = 0;
		DWORD closestDistance = 0;

		for(int i = 0; i < sizeof(merchantNames)/sizeof(merchantNames[0]); i++)
		{
			if(server->FindUnitByName(merchantNames[i], UNIT_TYPE_MONSTER, &gameUnit))
			{
				MAPPOS npcPos = server->GetUnitPosition(&gameUnit);
				DWORD distance = me->GetDistanceFrom(npcPos.x, npcPos.y);

				if(closestNpcID == 0 || closestDistance > distance)
				{
					closestNpcID = gameUnit.dwUnitID;
					closestDistance = distance;
				}
			}
		}

		return closestNpcID;
	}
	else
	{
		if(server->FindUnitByName(merchantName.c_str(), UNIT_TYPE_MONSTER, &gameUnit))
		{
			return gameUnit.dwUnitID;
		}
	}

	return 0;
}

bool ShopBot::WillItemFit(DWORD dwItemId)
{
	char itemCode[4];

	if(!server->GetItemCode(dwItemId, itemCode, 4))
	{
		server->GameErrorf("WillItemFit: Failed to get item code (ID = %04X), attempting retry", dwItemId);
		Sleep(1000);

		if(!server->GetItemCode(dwItemId, itemCode, 4))
		{
			server->GameErrorf("WillItemFit: Failed to get item code (ID = %04X)", dwItemId);
			return false;
		}
	}

	return me->FindFirstStorageSpace(STORAGE_INVENTORY, server->GetItemSize(itemCode), NULL)  == true;
}

bool ShopBot::SendRefresh()
{
	BYTE packet[sizeof(REFRESH_PACKET)];
	memcpy(packet, REFRESH_PACKET, sizeof(REFRESH_PACKET));

	return server->GameSendPacketToServer(packet, sizeof(packet)) != FALSE;
}

void ShopBot::PurchaseQueuedItems()
{
	if(currentState != STATE_PURCHASE_NEXTITEM)
		return;

	if(gambleQueue.empty())
	{
		currentState = STATE_PURCHASE_DONE;
		return;
	}

	DWORD currentItemId = gambleQueue.front();
	gambleQueue.pop();

	if(!WillItemFit(currentItemId))
	{
		Finish("Inventory is full, stopping");
		return;
	}

	if(!server->VerifyUnit(&merchantNpc))
	{
		server->GamePrintInfo(SBM_TAG "Could not verify npc unit");
		currentState = STATE_PURCHASE_DONE;
		return;
	}

	currentState = STATE_PURCHASE_WAITFORITEM;
	stateStartTick = GetTickCount();
	if(!me->BuyItem(currentItemId))
	{
		// Usually an item that left the store with the refresh; move on to the next one.
		server->GamePrintInfo(SBM_TAG "Unable to buy item");
		currentState = STATE_PURCHASE_NEXTITEM;
		return;
	}
}


void ShopBot::OnTick()
{
	const DWORD now = GetTickCount();

	switch(currentState)
	{
		case STATE_START_SESSION:
		{
			if (now - stateStartTick < START_DELAY_MS)
			{
				return;
			}

			merchantNpc.dwUnitType = UNIT_TYPE_MONSTER;
			merchantNpc.dwUnitID = FindMerchant();

			if(merchantNpc.dwUnitID == 0)
			{
				Finish("Failed to find merchant");
				return;
			}

			sessionAttempts++;
			currentState = STATE_NPC_LISTINGITEMS;
			if(!me->StartNpcSession(&merchantNpc, NPC_TRADE))
			{
				Finish("Failed to start npc session");
				return;
			}

			break;
		}
		case STATE_NPC_DONELISTINGITEMS:
		{
			currentState = STATE_PURCHASE_NEXTITEM;
			break;
		}
		case STATE_PURCHASE_NEXTITEM:
		{
			PurchaseQueuedItems();
			break;
		}
		case STATE_PURCHASE_WAITFORITEM:
		{
			// The item never reached the inventory (lag, or it was gone); don't wait forever.
			if (now - stateStartTick >= PURCHASE_TIMEOUT_MS)
			{
				server->GamePrintInfo(SBM_TAG "No reply to purchase, moving on");
				currentState = STATE_PURCHASE_NEXTITEM;
			}
			break;
		}
		case STATE_PURCHASE_DONE:
		{
			// The store stays open: the Refresh button rerolls the stock in place.
			currentState = STATE_REFRESH_COOLDOWN;
			break;
		}
		case STATE_REFRESH_COOLDOWN:
		{
			if (lastRefreshTick != 0 && now - lastRefreshTick < refreshCooldownMs)
			{
				return;
			}

			while (!gambleQueue.empty()) {
				gambleQueue.pop();
			}
			storeItemsSeen = 0;

			if (!SendRefresh())
			{
				Finish("Failed to send the Refresh request");
				return;
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
			if (now - stateStartTick < refreshCooldownMs)
			{
				return;
			}

			if (storeItemsSeen == 0)
			{
				server->GameStringf(SBM_TAG "Refresh %d: Refreshed before server could respond, try increasing RefreshCooldownMs in " CONFIG_PATH, refreshCount);
			}
			else
			{
				server->GameStringf(SBM_TAG "Refresh %d: %d items, %d to buy", refreshCount, storeItemsSeen, (int)gambleQueue.size());
			}

			currentState = STATE_PURCHASE_NEXTITEM;
			break;
		}
		case STATE_COMPLETE:
		{
			auto fleeModule = GetModuleHandle("Flee.d2h");
			if (fleeModule != NULL)
			{
				server->GameCommandf("flee SuppressAutoTp 0");
			}

			server->GameStringf(SBM_TAG "complete (%d refreshes)", refreshCount);
			currentState = STATE_UNINITIALIZED;
			return;
		}
	}
}

void ShopBot::OnNotEnoughMoney()
{
	if(currentState != STATE_PURCHASE_WAITFORITEM)
		return;

	Finish("Not enough money");
}

// Called for each item the merchant lists: when the store opens, and again after each Refresh
void ShopBot::OnNpcItemList(ITEM &merchantItem)
{
	if(currentState != STATE_NPC_LISTINGITEMS && currentState != STATE_REFRESH_LISTING)
		return;

	if (currentState == STATE_REFRESH_LISTING)
	{
		storeItemsSeen++;
	}

	// Any item whose code is on the list is bought, whatever its affixes
	if((targetItems.empty() && server->IsArmor(merchantItem.szItemCode)) || targetItems.count(merchantItem.szItemCode) > 0)
	{
		if(targetItems.empty())
		{
			server->GameStringf(SBM_TAG "Armor found");
		}
		else
		{
			server->GameStringf(SBM_TAG "%s found", targetItems[merchantItem.szItemCode].c_str());
		}

		gambleQueue.push(merchantItem.dwItemID);
	}
}

// Called when a bought item is placed in our inventory
void ShopBot::OnItemToStorage(ITEM &merchantItem)
{
	if(currentState != STATE_PURCHASE_WAITFORITEM)
		return;

	currentState = STATE_PURCHASE_NEXTITEM;
}

void ShopBot::OnNpcSession(int success)
{
	if(currentState != STATE_NPC_LISTINGITEMS)
		return;

	if(!success)
	{
		if (sessionAttempts >= MAX_SESSION_ATTEMPTS)
		{
			Finish("Could not open the merchant's store");
			return;
		}

		me->RedrawClient(FALSE);
		me->MoveToUnit(&merchantNpc, TRUE);
		server->GamePrintInfo(SBM_TAG "NPC request failed, retrying");
		stateStartTick = GetTickCount();
		currentState = STATE_START_SESSION;
		return;
	}

	currentState = STATE_NPC_DONELISTINGITEMS;
}

// Watches the server's messages while refreshing, so a refused refresh stops the run
// instead of pressing the button forever.
void ShopBot::OnServerPacket(const BYTE *aPacket, DWORD aLen)
{
	if (currentState != STATE_REFRESH_LISTING && currentState != STATE_REFRESH_COOLDOWN)
		return;

	const char *begin = reinterpret_cast<const char *>(aPacket);
	const char *end = begin + aLen;

	for (const char *refusal : REFRESH_REFUSALS)
	{
		const char *refusalEnd = refusal + strlen(refusal);
		if (std::search(begin, end, refusal, refusalEnd) != end)
		{
			std::string reason = std::string("The server refused the refresh: ") + refusal;
			Finish(reason.c_str());
			return;
		}
	}
}
