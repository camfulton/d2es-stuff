#include "../../Includes/ClientCore.cpp"
#include "shopbotm.h"

ShopBot shopbot;

static bool AskedForHelp(char** argv, int argc)
{
	return argc > 2 && _stricmp(argv[2], "help") == 0;
}

// D2HackIt prints a command's usage text by splitting it at each "\n" in place, which would write
// into a read-only string and crash. So each usage text below is one line, and commands with more
// to say print their own help with PrintUsage.
static void PrintUsage(char** argv, const char *const *lines)
{
	server->GamePrintInfo("Usage:");
	for (int i = 0; lines[i] != NULL; i++)
	{
		char line[512];
		sprintf_s(line, "\xFF" "c4.%s %s", argv[0], lines[i]);
		server->GamePrintInfo(line);
	}
}

static const char *const SHOP_HELP[] =
{
	"shop [item codes]\xFF" "c0 Opens the nearest merchant's store and presses Refresh until stopped (Space),",
	"shop\xFF" "c0 buying the given 3-letter item codes, or the items in shopbotm_items.txt.",
	"shop\xFF" "c0 Only items with PrefixCount / SuffixCount good affixes (shopbotm.ini; -1 = any) are bought,",
	"shop\xFF" "c0 the good ones listed by ID in goodPrefix_shopbotm.txt and goodSuffix_shopbotm.txt.",
	"s [item codes]\xFF" "c0 Shortcut for shop.",
	NULL
};

static const char *const CHIT_HELP[] =
{
	"chit <base name>\xFF" "c0 Buys every chit of that base, e.g.\xFF" "c4 chit light plate\xFF" "c0 buys all (Light Plate) Gambling chits.",
	"chit <exceptional or elite name>\xFF" "c0 Buys the chits of its normal base, e.g.\xFF" "c4 chit hood\xFF" "c0 buys Cap chits.",
	"chit <item codes>\xFF" "c0 Buys chits for these item codes of any tier (e.g.\xFF" "c4 chit ltp yvg\xFF" "c0); chit codes themselves work too.",
	"chit #<code> ...\xFF" "c0 Forces item codes, e.g.\xFF" "c4 chit #ltp #vgl\xFF" "c0 (# on every code).",
	"chit '<name>'\xFF" "c0 Forces a name, e.g.\xFF" "c4 chit 'war hat'\xFF" "c0 (single quotes).",
	"\xFF" "c0  Only needed when the words could be read either way; you will be told when they can.",
	"c <base name | item codes>\xFF" "c0 Shortcut for chit.",
	NULL
};

// .shopbotm shop [item codes] (shortcut: s)
BOOL PRIVATE Shop(char** argv, int argc)
{
	if (argc < 2 || AskedForHelp(argv, argc))
	{
		PrintUsage(argv, SHOP_HELP);
		return TRUE;
	}

	std::unordered_map<std::string, std::string> itemsToShopFor;

	for (int i = 2; i < argc; i++)
	{
		if (strlen(argv[i]) == 3)
		{
			itemsToShopFor[argv[i]] = argv[i];
		}
		else
		{
			server->GameStringf(SBM_TAG "Invalid item code %s (must be 3 characters)", argv[i]);
			PrintUsage(argv, SHOP_HELP);
			return TRUE;
		}
	}

	shopbot.Start(itemsToShopFor, true);

	return TRUE;
}

// .shopbotm chit <base name | item codes> (shortcut: c): shop for every chit of the given bases,
// e.g. "chit light plate" or "chit ltp vgl"
BOOL PRIVATE Chit(char** argv, int argc)
{
	if (argc < 3 || AskedForHelp(argv, argc))
	{
		PrintUsage(argv, CHIT_HELP);
		return TRUE;
	}

	std::vector<std::string> args(argv + 2, argv + argc);

	std::unordered_map<std::string, std::string> chits;
	if (!shopbot.FindChits(args, chits))
	{
		return TRUE; // FindChits already explained why
	}

	shopbot.Start(chits, false);   // chits have no affixes to check

	return TRUE;
}

BOOL PRIVATE Stop(char** argv, int argc)
{
	if (AskedForHelp(argv, argc))
	{
		return FALSE;
	}

	shopbot.Stop();

	return TRUE;
}

// Replaces D2HackIt's default help, which lists command names only
BOOL PRIVATE Help(char** argv, int argc)
{
	if (argc > 2)
	{
		return FALSE;
	}

	server->GamePrintInfo("ShopbotM commands (.sbm works as a short form of .shopbotm when sbm is loaded):");
	server->GamePrintInfo("\xFF" "c4.shopbotm shop [item codes]\xFF" "c0 (or\xFF" "c4 s\xFF" "c0) - buy these items, or those in shopbotm_items.txt, if their affixes are good enough");
	server->GamePrintInfo("\xFF" "c4.shopbotm chit <base name | item codes>\xFF" "c0 (or\xFF" "c4 c\xFF" "c0) - buy chits of those bases, e.g.\xFF" "c4 chit light plate\xFF" "c0,\xFF" "c4 c hood\xFF" "c0 or\xFF" "c4 c ltp vgl");
	server->GamePrintInfo("\xFF" "c0  If the words could mean item codes or a name you will be asked which:\xFF" "c4 #code\xFF" "c0 forces codes,\xFF" "c4 'name'\xFF" "c0 a name");
	server->GamePrintInfo("\xFF" "c4.shopbotm stop\xFF" "c0 - stop shopping (or press Space)");
	server->GamePrintInfo("\xFF" "c4.shopbotm <command> help\xFF" "c0 - details for one command");
	server->GamePrintInfo("Settings in " CONFIG_PATH ": RefreshCooldownMs (delay between refreshes), PrefixCount and SuffixCount (shop only)");

	return TRUE;
}

DWORD EXPORT OnGameTimerTick()
{
	shopbot.OnTick();

	return 0;
}

VOID EXPORT OnThisPlayerMessage(UINT nMessage, WPARAM wParam, LPARAM lParam)
{
	switch(nMessage)
	{
		case PM_NPCSESSION:
			shopbot.OnNpcSession(wParam);
			break;
	}
}

BYTE EXPORT OnGameKeyDown(BYTE iKeyCode)
{
	switch(iKeyCode)
	{
		case VK_SPACE:
			shopbot.Stop();
			break;
	}

	return iKeyCode;
}

DWORD EXPORT OnGamePacketBeforeReceived(BYTE* aPacket, DWORD aLen)
{
	if(aPacket[0] == 0x9c)
	{
		ITEM currentItem;

		if(!server->ParseItem(aPacket, aLen, currentItem))
		{
			return aLen;
		}

		if(currentItem.iStore && currentItem.iAction != ITEM_ACTION_FROM_STORE)
		{
			shopbot.OnNpcItemList(currentItem);
		}
		else if(currentItem.iAction == ITEM_ACTION_TO_STORAGE)
		{
			shopbot.OnItemToStorage(currentItem);
		}
	}
	else if(aPacket[0] == 0x2a) // npc transation
	{
		if(aPacket[2] == 0x0C) // 0x0C = Not enough money, 0x00 = bought, 0x01 = sold
		{
			shopbot.OnNotEnoughMoney();
		}
	}
	else
	{
		shopbot.OnServerPacket(aPacket, aLen);
	}

	return aLen;
}



CLIENTINFO
(
	0,1,
	"",
	"",
	"",
	""
)

MODULECOMMANDSTRUCT ModuleCommands[]=
{
	{
		"help",
		Help,
		"help\xFF" "c0 Lists the commands;\xFF" "c4 <command> help\xFF" "c0 shows details for one."
	},
	{
		"shop",
		Shop,
		"shop [item codes]\xFF" "c0 Buys these items (or those in shopbotm_items.txt), pressing Refresh until stopped."
	},
	{
		"s",
		Shop,
		"s [item codes]\xFF" "c0 Shortcut for shop; see\xFF" "c4 .shopbotm shop help"
	},
	{
		"chit",
		Chit,
		"chit <base name | item codes>\xFF" "c0 Buys chits of those bases; see\xFF" "c4 .shopbotm chit help"
	},
	{
		"c",
		Chit,
		"c <base name | item codes>\xFF" "c0 Shortcut for chit; see\xFF" "c4 .shopbotm chit help"
	},
	{
		"stop",
		Stop,
		"stop\xFF" "c0 Stops shopping (Space does the same)."
	},
	{NULL}
};
