#include "../../Includes/ClientCore.cpp"
#include "shopbotm.h"

ShopBot shopbot;

static bool AskedForHelp(char** argv, int argc)
{
	return argc > 2 && _stricmp(argv[2], "help") == 0;
}

// .shopbotm shop [item codes] (shortcut: s)
BOOL PRIVATE Shop(char** argv, int argc)
{
	if (argc < 2 || AskedForHelp(argv, argc))
	{
		return FALSE;
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
			return FALSE;
		}
	}

	shopbot.Start(itemsToShopFor);

	return TRUE;
}

// .shopbotm chit <base name | item codes> (shortcut: c): shop for every chit of the given bases,
// e.g. "chit light plate" or "chit ltp vgl"
BOOL PRIVATE Chit(char** argv, int argc)
{
	if (argc < 3 || AskedForHelp(argv, argc))
	{
		return FALSE;
	}

	std::vector<std::string> args(argv + 2, argv + argc);

	std::unordered_map<std::string, std::string> chits;
	if (!shopbot.FindChits(args, chits))
	{
		return TRUE; // FindChits already explained why
	}

	shopbot.Start(chits);

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
	server->GamePrintInfo("\xFF" "c4.shopbotm shop [item codes]\xFF" "c0 (or\xFF" "c4 s\xFF" "c0) - buy these items, or those in shopbotm_items.txt");
	server->GamePrintInfo("\xFF" "c4.shopbotm chit <base name | item codes>\xFF" "c0 (or\xFF" "c4 c\xFF" "c0) - buy chits of those bases, e.g.\xFF" "c4 chit light plate\xFF" "c0,\xFF" "c4 c hood\xFF" "c0 or\xFF" "c4 c ltp vgl");
	server->GamePrintInfo("\xFF" "c4.shopbotm stop\xFF" "c0 - stop shopping (or press Space)");
	server->GamePrintInfo("\xFF" "c4.shopbotm <command> help\xFF" "c0 - details for one command");
	server->GamePrintInfo("Delay between refreshes: RefreshCooldownMs in " CONFIG_PATH);

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
		"help\xFF" "c0 List the commands in this module.\n"
		"<command> help\xFF" "c0 Shows detailed help for <command>."
	},
	{
		"shop",
		Shop,
		"shop [item codes]\xFF" "c0 Opens the nearest merchant's store and presses Refresh until stopped (Space),\n"
		"shop\xFF" "c0 buying the given 3-letter item codes, or the items in shopbotm_items.txt.\n"
		"s [item codes]\xFF" "c0 Shortcut for shop."
	},
	{
		"s",
		Shop,
		"s [item codes]\xFF" "c0 Shortcut for shop; see\xFF" "c4 .shopbotm shop help"
	},
	{
		"chit",
		Chit,
		"chit <base name>\xFF" "c0 Buys every chit of that base, e.g.\xFF" "c4 chit light plate\xFF" "c0 buys all (Light Plate) Gambling chits.\n"
		"chit <exceptional or elite name>\xFF" "c0 Buys the chits of its normal base, e.g.\xFF" "c4 chit hood\xFF" "c0 buys Cap chits.\n"
		"chit <item codes>\xFF" "c0 Buys chits for these item codes of any tier (e.g.\xFF" "c4 chit ltp yvg\xFF" "c0); chit codes themselves work too.\n"
		"chit #<code> / '<name>'\xFF" "c0 Forces codes or a name when the words could be read either way.\n"
		"c <base name | item codes>\xFF" "c0 Shortcut for chit."
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
