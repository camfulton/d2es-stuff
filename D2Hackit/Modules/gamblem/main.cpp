#include <fstream>
#include "../../Includes/ClientCore.cpp"
#include "gambler.h"
#include <set>
#include "../common/ItemLookup.h"

Gambler gambler;
bool blockId = false;
bool goldDrop = false;
bool goldPick = false;
bool autostock = false;
char masterPlayer[128];

// D2HackIt's item list ("code name" per line)
#define ALL_ITEMS_PATH ".\\plugin\\AllItems.txt"
// Which exceptional and elite bases belong to each normal base ("<normal> <exceptional> <elite>")
#define TIERS_PATH ".\\plugin\\gamblem_tiers.txt"

/// <summary>
/// Turns what was typed after "start" into the item codes to gamble for: 3-letter codes as always
///   ("rin amu"), or an item's name of any tier ("death mask" gambles for Mask, which the gamble
///   window sells and can roll as a Death Mask). See ItemLookup.h for how clashes are settled.
/// </summary>
static bool FindGambleCodes(const std::vector<std::string> &args, std::vector<std::string> &gambleFor)
{
	gambleFor.clear();

	ItemLookup::Input input;
	if(!ItemLookup::ParseInput(args, input, GM_TAG))
	{
		return false;
	}

	ItemLookup::Table table;
	if(!ItemLookup::LoadNames(table, ALL_ITEMS_PATH, GM_TAG))
	{
		// Without the item list, codes still work as they always did
		if(input.couldBeCodes && !input.forceName)
		{
			gambleFor = input.codes;
			return true;
		}
		return false;
	}
	ItemLookup::LoadTiers(table, TIERS_PATH, GM_TAG);

	// Item codes, used as typed (codes AllItems.txt does not know are still accepted)
	ItemLookup::Reading codes;
	if(input.couldBeCodes && !input.forceName)
	{
		codes.valid = true;
		for(const auto &code : input.codes)
		{
			codes.known = codes.known && ItemLookup::IsKnownCode(table, code);
			codes.items[code] = ItemLookup::NameOf(table, code);
		}
	}

	// A name, of any tier: exceptional and elite bases gamble for their normal base
	ItemLookup::Reading name;
	if(!input.forceCodes)
	{
		const auto named = table.byName.find(ItemLookup::ToLower(input.phrase));
		if(named != table.byName.end())
		{
			// Several codes can share a name (two War Hats); prefer the ones in the tier list
			std::vector<std::string> matches;
			for(const auto &code : named->second)
			{
				if(table.tiers.count(code) > 0)
				{
					matches.push_back(code);
				}
			}
			if(matches.empty())
			{
				matches = named->second;
			}

			for(const auto &code : matches)
			{
				const auto link = table.tiers.find(code);
				if(link == table.tiers.end())
				{
					name.items[code] = ItemLookup::NameOf(table, code);
					continue;
				}

				const std::string &normal = link->second.normal;
				name.items[normal] = ItemLookup::NameOf(table, normal);

				if(link->second.tier > 0)
				{
					char note[256];
					sprintf_s(note, "%s (%s) is the %s %s: gambling for %s (%s)",
						ItemLookup::NameOf(table, code).c_str(), code.c_str(), ItemLookup::TIER_NAMES[link->second.tier],
						ItemLookup::NameOf(table, normal).c_str(), ItemLookup::NameOf(table, normal).c_str(), normal.c_str());
					name.notes.push_back(note);
				}
			}
		}
		name.valid = !name.items.empty();
	}

	bool explained = false;
	const ItemLookup::Reading *chosen = ItemLookup::Choose(input, codes, name, explained, GM_TAG);
	if(chosen == nullptr)
	{
		if(!explained)
		{
			ItemLookup::Suggest(table, input.phrase, std::set<std::string>(), "items", GM_TAG);
		}
		return false;
	}

	for(const auto &note : chosen->notes)
	{
		server->GameStringf(GM_TAG "%s", note.c_str());
	}
	for(const auto &item : chosen->items)
	{
		gambleFor.push_back(item.first);
	}
	return true;
}

BOOL PRIVATE Start(char** argv, int argc)
{
	if(argc < 3 || (argc == 3 && _stricmp(argv[2], "help") == 0))
	{
		return FALSE;
	}

	std::vector<std::string> args(argv + 2, argv + argc);
	std::vector<std::string> gambleFor;

	if(!FindGambleCodes(args, gambleFor))
	{
		return TRUE; // FindGambleCodes already explained why
	}

	if(!gambler.Init(gambleFor))
	{
		return TRUE;
	}

	gambler.StartGambling();

	return TRUE;
}

BOOL PRIVATE NoSell(char **argv, int argc)
{
	std::vector<std::string> gambleFor;

	if(argc < 3)
	{
		gambler.SetNoSellList(gambleFor);
		return TRUE;
	}
	
	for(int i = 2; i < argc; i++)
	{
		if(strlen(argv[i]) == 3)
		{
			gambleFor.push_back(argv[i]);
		}
		else
		{
			server->GameStringf("\xFF" "c3GambleM\xFF" "c0: Invalid item code %s (must be 3 characters)", argv[i]);
			return FALSE;
		}
	}

	gambler.SetNoSellList(gambleFor);
	return TRUE;
}

BOOL PRIVATE StartAmulets(char **argv, int argc)
{
	std::vector<std::string> jewellery;

	jewellery.push_back("amu"); //Amulet");
	jewellery.push_back("zam"); //Amazonian"); //Pin");
	jewellery.push_back("sam"); //Sorcerer's"); //Talisman");
	jewellery.push_back("nam"); //Death's"); //Locket");
	jewellery.push_back("pam"); //Holy"); //Pendant");
	jewellery.push_back("bam"); //Totemic"); //Pebble");
	jewellery.push_back("dam"); //Druidic"); //Necklace");
	jewellery.push_back("aam"); //Assassin's"); //Choker");

	if(!gambler.Init(jewellery))
	{
		return TRUE;
	}
	gambler.StartGambling();

	return TRUE;
}

BOOL PRIVATE StartAuraStones(char** argv, int argc)
{
	std::vector<std::string> jewellery;

	server->GameCommandLine("gamblem autostock magic rare set unique");

	jewellery.push_back("zam"); //Amazonian"); //Pin");
	jewellery.push_back("sam"); //Sorcerer's"); //Talisman");
	jewellery.push_back("nam"); //Death's"); //Locket");
	jewellery.push_back("pam"); //Holy"); //Pendant");
	jewellery.push_back("bam"); //Totemic"); //Pebble");
	jewellery.push_back("dam"); //Druidic"); //Necklace");
	jewellery.push_back("aam"); //Assassin's"); //Choker");

	if (!gambler.Init(jewellery))
	{
		return TRUE;
	}
	gambler.StartGambling();

	return TRUE;
}

BOOL PRIVATE StartJewellery(char** argv, int argc)
{
	std::vector<std::string> jewellery;

	jewellery.push_back("rin"); //Ring");
	jewellery.push_back("amu"); //Amulet");
	jewellery.push_back("zrn"); //Amazonian"); //Loop");
	jewellery.push_back("srn"); //Sorcerer's"); //Band");
	jewellery.push_back("nrn"); //Necromancer's"); //Stone");
	jewellery.push_back("prn"); //Paladic"); //Halo");
	jewellery.push_back("brg"); //Barbaric"); //Hoop");
	jewellery.push_back("drn"); //Druid's"); //Seal");
	jewellery.push_back("arn"); //Assassin's"); //Spiral");
	jewellery.push_back("zam"); //Amazonian"); //Pin");
	jewellery.push_back("sam"); //Sorcerer's"); //Talisman");
	jewellery.push_back("nam"); //Death's"); //Locket");
	jewellery.push_back("pam"); //Holy"); //Pendant");
	jewellery.push_back("bam"); //Totemic"); //Pebble");
	jewellery.push_back("dam"); //Druidic"); //Necklace");
	jewellery.push_back("aam"); //Assassin's"); //Choker");

	if(!gambler.Init(jewellery))
	{
		return TRUE;
	}
	gambler.StartGambling();

	return TRUE;
}

BOOL PRIVATE StartRings(char** argv, int argc)
{
	std::vector<std::string> jewellery;

	jewellery.push_back("rin"); //Ring");
	//jewellery.push_back("amu"); //Amulet");
	jewellery.push_back("zrn"); //Amazonian"); //Loop");
	jewellery.push_back("srn"); //Sorcerer's"); //Band");
	jewellery.push_back("nrn"); //Necromancer's"); //Stone");
	jewellery.push_back("prn"); //Paladic"); //Halo");
	jewellery.push_back("brg"); //Barbaric"); //Hoop");
	jewellery.push_back("drn"); //Druid's"); //Seal");
	jewellery.push_back("arn"); //Assassin's"); //Spiral");
	//jewellery.push_back("zam"); //Amazonian"); //Pin");
	//jewellery.push_back("sam"); //Sorcerer's"); //Talisman");
	//jewellery.push_back("nam"); //Death's"); //Locket");
	//jewellery.push_back("pam"); //Holy"); //Pendant");
	//jewellery.push_back("bam"); //Totemic"); //Pebble");
	//jewellery.push_back("dam"); //Druidic"); //Necklace");
	//jewellery.push_back("aam"); //Assassin's"); //Choker");

	if(!gambler.Init(jewellery))
	{
		return TRUE;
	}
	gambler.StartGambling();

	return TRUE;
}

BOOL PRIVATE Rings(char **argv, int argc)
{
	server->GamePrintString("rin Ring");
	server->GamePrintString("amu Amulet");
	server->GamePrintString("zrn Amazonian Loop");
	server->GamePrintString("srn Sorcerer's Band");
	server->GamePrintString("nrn Necromancer's Stone");
	server->GamePrintString("prn Paladic Halo");
	server->GamePrintString("brg Barbaric Hoop");
	server->GamePrintString("drn Druid's Seal");
	server->GamePrintString("arn Assassin's Spiral");
	server->GamePrintString("zam Amazonian Pin");
	server->GamePrintString("sam Sorcerer's Talisman");
	server->GamePrintString("nam Death's Locket");
	server->GamePrintString("pam Holy Pendant");
	server->GamePrintString("bam Totemic Pebble");
	server->GamePrintString("dam Druidic Necklace");
	server->GamePrintString("aam Assassin's Choker");

	return TRUE;
}

BOOL PRIVATE StartRadiance(char **argv, int argc)
{
	server->GameCommandLine("gamblem sell magic rare set");
	server->GameCommandLine("gamblem nosell 0cr");
	server->GameCommandLine("gamblem start crs");

	return TRUE;
}

BOOL PRIVATE StartMagesDelight(char **argv, int argc)
{
	server->GameCommandLine("gamblem sell magic rare set");
	server->GameCommandLine("gamblem nosell ytp");
	server->GameCommandLine("gamblem start ltp");

	return TRUE;

}
BOOL PRIVATE StartWraithShroud(char **argv, int argc)
{
	server->GameCommandLine("gamblem sell magic rare set");
	server->GameCommandLine("gamblem nosell yrb");
	server->GameCommandLine("gamblem start rob");

	return TRUE;
}

BOOL PRIVATE StartExNihilo(char **argv, int argc)
{
	server->GameCommandLine("gamblem sell magic rare set");
	server->GameCommandLine("gamblem nosell 0ts");
	server->GameCommandLine("gamblem start tsp");

	return TRUE;
}

BOOL PRIVATE StartDeathwatchGuard(char **argv, int argc)
{
	server->GameCommandLine("gamblem sell magic rare set");
	server->GameCommandLine("gamblem nosell yft");
	server->GameCommandLine("gamblem start fts");

	return TRUE;
}

BOOL PRIVATE StartPolarStar(char **argv, int argc)
{
	server->GameCommandLine("gamblem sell magic rare set");
	server->GameCommandLine("gamblem nosell ci3");
	server->GameCommandLine("gamblem start ci0");

	return TRUE;
}

BOOL PRIVATE StartHeavyMetal(char **argv, int argc)
{
	server->GameCommandLine("gamblem sell magic rare set");
	server->GameCommandLine("gamblem nosell 0gi");
	server->GameCommandLine("gamblem start gix");

	return TRUE;
}

BOOL PRIVATE StartFoc(char **argv, int argc)
{

	server->GameCommandLine("gamblem sell magic rare set");
	server->GameCommandLine("gamblem nosell yul");
	server->GameCommandLine("gamblem start ful");

	return TRUE;
}

BOOL PRIVATE SetMaster(char **argv, int argc)
{
	if(argc != 3 || strlen(argv[2]) == 0 || strlen(argv[2]) >= sizeof(masterPlayer)/sizeof(masterPlayer[0]))
	{
		server->GameStringf("No master set", masterPlayer);
		server->GameStringf("Gold dropping \xFF" "c1disabled\xFF" "c0", masterPlayer);
		return FALSE;
	}

	strcpy_s(masterPlayer, argv[2]);

	goldDrop = true;
	server->GameStringf("Setting master to %s", masterPlayer);
	server->GameStringf("Gold dropping \xFF" "c2enabled\xFF" "c0 for \"%s\"", masterPlayer);
	server->GameCommandLine("pick townpick 0");

	return TRUE;
}

BOOL PRIVATE ToggleRequestGold(char **argv, int argc)
{
	int requestSplitBy = 1;

	if(argc == 3)
	{
		requestSplitBy = atoi(argv[2]);

		if(requestSplitBy <= 0)
			requestSplitBy = 1;
	}

	gambler.ToggleRequestGold(requestSplitBy);

	server->GameCommandLine("pick townpick 1");
	server->GameCommandLine("pick radius 40");

	return TRUE;
}

BOOL PRIVATE ToggleGoldDrop(char **argv, int argc)
{
	goldDrop = !goldDrop;

	server->GameCommandLine("pick townpick 0");

	if(goldDrop)
		server->GameStringf("Gold dropping \xFF" "c2enabled\xFF" "c0 for \"%s\"", masterPlayer);
	else
		server->GameStringf("Gold dropping \xFF" "c1disabled\xFF" "c0 for \"%s\"", masterPlayer);

	return TRUE;
}

BOOL PRIVATE ToggleGambleSell(char **argv, int argc)
{
	bool uniques = false;
	bool rares = false;
	bool sets = false;
	bool magics = false;

	for(int i = 2; i < argc; i++)
	{
		if(_stricmp(argv[i], "magics") == 0 || _stricmp(argv[i], "magic") == 0)
		{
			magics = true;
		}
		if(_stricmp(argv[i], "sets") == 0 || _stricmp(argv[i], "set") == 0)
		{
			sets = true;
		}
		else if(_stricmp(argv[i], "rares") == 0 || _stricmp(argv[i], "rare") == 0)
		{
			rares = true;
		}
		else if(_stricmp(argv[i], "uniques") == 0 || _stricmp(argv[i], "unique") == 0)
		{
			uniques = true;
		}
	}

	gambler.ToggleGambleSell(magics, sets, rares, uniques);

	return TRUE;
}

BOOL PRIVATE Autostock(char **argv, int argc)
{
	bool uniques = false;
	bool rares = false;
	bool sets = false;

	if(argc < 2)
		return FALSE;

	for(int i = 2; i < argc; i++)
	{
		if(_stricmp(argv[i], "sets") == 0 || _stricmp(argv[i], "set") == 0)
		{
			sets = true;
		}
		else if(_stricmp(argv[i], "rares") == 0 || _stricmp(argv[i], "rare") == 0)
		{
			rares = true;
		}
		else if(_stricmp(argv[i], "uniques") == 0 || _stricmp(argv[i], "unique") == 0)
		{
			uniques = true;
		}
	}

	autostock = !autostock;
	gambler.ToggleAutostock(sets, rares, uniques);

	return TRUE;
}

BOOL PRIVATE ToggleIDBlock(char **argv, int argc)
{
	blockId = !blockId;

	if(blockId)
	{
		server->GamePrintString("Now blocking ID on gamble");
	}
	else
	{
		server->GamePrintString("No longer blocking ID on gamble");
	}

	return TRUE;
}

////////////////////////////////////////////
//
//               EXPORTS
//
/////////////////////////////////////////////


DWORD lastGoldAmount = 0;

DWORD EXPORT OnGameTimerTick()
{
	if(me->GetStat(STAT_GOLD) != lastGoldAmount)
	{
		gambler.OnGoldPickup();

		lastGoldAmount = me->GetStat(STAT_GOLD);
	}
	else
	{
		gambler.OnTick();
	}

	return 0;
}

BOOL EXPORT OnClientStart()
{
	memset(&masterPlayer, 0, sizeof(masterPlayer)/sizeof(masterPlayer[0]));
	lastGoldAmount = me->GetStat(STAT_GOLD);

	return TRUE;
}

DWORD EXPORT OnGamePacketBeforeSent(BYTE* aPacket, DWORD aLen)
{

	if(aPacket[0] == 0x37)
	{
		//DWORD itemID = *((DWORD*)(aPacket+1));

		//gambler.OnItemIdentified(itemID);

		if(blockId)
		{
			return 0;
		}
	}
	else if(autostock && aPacket[0] == 0x15 && aPacket[1] == 0x01)
	{
		char *chatMessage = (char *)(aPacket+3);

		//server->GameStringf("Msg: %s", chatMessage);

		// Sneaky message passing, doesn't send autostocker chat messages out
		if(strncmp(chatMessage, "\xFF" "c:Autostocker\xFF" "c0:", 18) == 0)
		{
			const auto message = std::string_view(chatMessage + 19);
			if (!gambler.OnAutostockerMessage(message))
			{
				return aLen;
			}
			else
			{
				return 0;
			}
		}
	}

	return aLen;
}

/// <summary>
/// Process game packets that have already been processed by d2hackit and the game. Internal inventory structure
///   for d2hackit is up to date at this time.
/// </summary>
VOID EXPORT OnGamePacketAfterReceived(BYTE* aPacket, DWORD aLen)
{
	if(aPacket[0] == 0x2a) // npc transation
	{
		if(aPacket[2] == 0x01)
		{
			gambler.OnItemSold();
		}
	}
}

DWORD EXPORT OnGamePacketBeforeReceived(BYTE* aPacket, DWORD aLen)
{
	// Stops the run if the server refuses a Refresh press
	gambler.OnServerPacket(aPacket, aLen);

	// 0x27 D2GS_NPCINTERACT

	//if((aPacket[0] < 0x67 || aPacket[0] > 0x6D) && aPacket[0] != 0x0A && aPacket[0] != 0x8f) 
	//{
	//	char buff[1024];
	//	memset(buff, 0, sizeof(buff));
	//	for(int i = 0; i < aLen; ++i)
	//	{
	//		sprintf(buff, "%s %02X", buff, aPacket[i]);
	//	}
	//	server->GameStringf("IN: %s", buff);
	//}
	if(aPacket[0] == 0x2c)
	{
		BYTE entityType = *((BYTE *)(aPacket+1));
		DWORD entityID = *((DWORD *)(aPacket+2));
		WORD speechID = *((WORD *)(aPacket+6));

		// Identify sound
		if(speechID == 6)
		{
			gambler.OnItemIdentifiedSounded();
		}
	}
	else if(aPacket[0] == 0x9c)
	{
		ITEM currentItem;

		if(!server->ParseItem(aPacket, aLen, currentItem))
		{
			return aLen;
		}

		if(currentItem.iGamble)
		{
			gambler.OnNpcGambleItemList(currentItem);
		}
		else if(currentItem.iAction == ITEM_ACTION_TO_STORAGE)
		{
			gambler.OnItemToStorage(currentItem);
		}
	}
	else if(aPacket[0] == 0x2a) // npc transation
	{
		if(aPacket[2] == 0x0C) // 0x0C = Not enough money, 0x00 = bought, 0x01 = sold
		{
			gambler.OnNotEnoughMoney();
		}
	}
	else if(goldDrop && aPacket[0] == 0x26 && aPacket[1] == 0x01)
	{
		char *chatName = (char *)(aPacket+10);
		char *chatMessage = NULL;

		if(_stricmp(masterPlayer, chatName) == 0)
		{
			chatMessage = (char *)(aPacket + 11 + strlen(chatName));

			DWORD goldAmount = atoi(chatMessage);

			if(goldAmount == 0)
				return aLen;

			if(goldAmount > me->GetStat(STAT_GOLD))
				goldAmount = me->GetStat(STAT_GOLD);

			if(goldAmount == 0)
			{
				me->Say("no gold left");
			}
			else
			{
				me->DropGold(goldAmount);
			}
		}
	}

	return aLen;
}

VOID EXPORT OnThisPlayerMessage(UINT nMessage, WPARAM wParam, LPARAM lParam)
{
	if(nMessage == PM_UICLOSED)
	{
		gambler.OnUIClosed();
	}
	if(nMessage == PM_NPCSESSION)
	{
		gambler.OnNpcSession(wParam);
	}
}

BYTE EXPORT OnGameKeyDown(BYTE iKeyCode)
{
	switch(iKeyCode)
	{
		case VK_SPACE:
			gambler.StopGambling();
			break;
	}

	return iKeyCode;
}


CLIENTINFO
(
	0,1,
	"",
	"",
	"GambleM",
	""
)

MODULECOMMANDSTRUCT ModuleCommands[]=
{
	{
		"help",
		OnGameCommandHelp,
		"help\xFF" "c0 List commands available in this module.\n"
		"<command> help\xFF" "c0 Shows detailed help for <command> in this module."
	},
	{
		"Start",
		Start,
		"Start <item codes>\xFF" "c0 Gambles for these 3-letter item codes, e.g.\xFF" "c4 start rin amu\n"
		"Start <item name>\xFF" "c0 Gambles for that item; exceptional and elite names gamble for their normal base, e.g.\xFF" "c4 start death mask\xFF" "c0 gambles for Mask.\n"
		"Start #<code> / '<name>'\xFF" "c0 Forces codes or a name when the words could be read either way."
	},
	{
		"StartRings",
		StartRings,
		"Starts gambling for rings"
	},
	{
		"StartAmulets",
		StartAmulets,
		"Starts gambling for amulets"
	},
	{
		"StartJewellery",
		StartJewellery,
		"Start gambling for rings and amulets",
	},
	{
		"StartAuraStones",
		StartAuraStones,
		"Start gambling for class specific amulets",
	},
	{
		"StartFoc",
		StartFoc,
		"Starts gambling for FoC"
	},
	{
		"StartHeavyMetal",
		StartHeavyMetal,
		"Starts gambling for HeavyMetal"
	},
	{
		"StartPolarStar",
		StartPolarStar,
		"Starts gambling for PolarStar"
	},
	{
		"StartDeathwatchGuard",
		StartDeathwatchGuard,
		"Starts gambling for DeathwatchGuard"
	},
	{
		"StartExNihilo",
		StartExNihilo,
		"Starts gambling for ExNihilo"
	},
	{
		"StartWraithShroud",
		StartWraithShroud,
		"Starts gambling for WraithShroud"
	},	
	{
		"StartMagesDelight",
		StartMagesDelight,
		"Starts gambling for Mage's Delight"
	},
	//{
	//	"StartRadiance",
	//	StartRadiance,
	//	"Starts gambling for Radiance (disabled?)"
	//},
	//{
	//	"Block",
	//	ToggleIDBlock,
	//	"Blocks the identify process when gambling"
	//},
	{
		"Sell",
		ToggleGambleSell,
		"Sells magic items that were gambled when inventory's full"
	},
	{
		"NoSell",
		NoSell,
		"Sets the list of items that should not be sold when gambling. Set ToggleSell to magic set rare and set NoSell to a specific item code to sell all items that are not unique version of the specified item code(s).",
	},
	//{
	//	"Gold",
	//	ToggleGoldDrop,
	//	"Drops gold when number is said in chat by [master]"
	//},
	{
		"Master",
		SetMaster,
		"Sets master for gold drop requests"
	},
	{
		"Request",
		ToggleRequestGold,
		"Usage: request [splitAmount]"
	},
	{
		"Autostock",
		Autostock,
		"Usage: autostock [sets] [rares] [uniques]"
	},
	{NULL}
};
