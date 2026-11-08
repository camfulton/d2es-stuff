// gm: a short name for the gamblem module.
//
// D2HackIt sends ".<name> ..." to the loaded module whose file is <name>.d2h, and has no aliases,
// so ".gm" needs a module of its own. This one does no shopping: it passes every command on
// unchanged, so ".gm start rin" runs ".gamblem start rin". gamblem is loaded on first use.
#include "../../Includes/ClientCore.cpp"
#include <string>
#include <vector>

#define GM_FORWARD_TAG "\xFF" "c3gm" "\xFF" "c0: "

static const char *TARGET_MODULE = "gamblem";
static const char *TARGET_FILE = "gamblem.d2h";

BOOL PRIVATE Forward(char** argv, int argc)
{
	if (GetModuleHandle(TARGET_FILE) == NULL)
	{
		server->GameCommandf("load %s", TARGET_MODULE);

		if (GetModuleHandle(TARGET_FILE) == NULL)
		{
			server->GameStringf(GM_FORWARD_TAG "Could not load %s; is it in the plugin folder?", TARGET_FILE);
			return TRUE;
		}
	}

	// argv[0] is "gm"; the rest goes through as typed
	std::string command = TARGET_MODULE;
	for (int i = 1; i < argc; i++)
	{
		command += " ";
		command += argv[i];
	}

	std::vector<char> buffer(command.begin(), command.end());
	buffer.push_back('\0');
	server->GameCommandLine(buffer.data());

	return TRUE;
}

CLIENTINFO
(
	0,1,
	"",
	"",
	"",
	""
)

// Every gamblem command, so D2HackIt routes each one here; help and usage come from gamblem itself.
MODULECOMMANDSTRUCT ModuleCommands[]=
{
	{ "help", Forward, "help\xFF" "c0 Same as\xFF" "c4 .gamblem help" },
	{ "Start", Forward, "Start\xFF" "c0 Same as\xFF" "c4 .gamblem Start" },
	{ "StartRings", Forward, "StartRings\xFF" "c0 Same as\xFF" "c4 .gamblem StartRings" },
	{ "StartAmulets", Forward, "StartAmulets\xFF" "c0 Same as\xFF" "c4 .gamblem StartAmulets" },
	{ "StartJewellery", Forward, "StartJewellery\xFF" "c0 Same as\xFF" "c4 .gamblem StartJewellery" },
	{ "StartAuraStones", Forward, "StartAuraStones\xFF" "c0 Same as\xFF" "c4 .gamblem StartAuraStones" },
	{ "StartFoc", Forward, "StartFoc\xFF" "c0 Same as\xFF" "c4 .gamblem StartFoc" },
	{ "StartHeavyMetal", Forward, "StartHeavyMetal\xFF" "c0 Same as\xFF" "c4 .gamblem StartHeavyMetal" },
	{ "StartPolarStar", Forward, "StartPolarStar\xFF" "c0 Same as\xFF" "c4 .gamblem StartPolarStar" },
	{ "StartDeathwatchGuard", Forward, "StartDeathwatchGuard\xFF" "c0 Same as\xFF" "c4 .gamblem StartDeathwatchGuard" },
	{ "StartExNihilo", Forward, "StartExNihilo\xFF" "c0 Same as\xFF" "c4 .gamblem StartExNihilo" },
	{ "StartWraithShroud", Forward, "StartWraithShroud\xFF" "c0 Same as\xFF" "c4 .gamblem StartWraithShroud" },
	{ "StartMagesDelight", Forward, "StartMagesDelight\xFF" "c0 Same as\xFF" "c4 .gamblem StartMagesDelight" },
	{ "Sell", Forward, "Sell\xFF" "c0 Same as\xFF" "c4 .gamblem Sell" },
	{ "NoSell", Forward, "NoSell\xFF" "c0 Same as\xFF" "c4 .gamblem NoSell" },
	{ "Master", Forward, "Master\xFF" "c0 Same as\xFF" "c4 .gamblem Master" },
	{ "Request", Forward, "Request\xFF" "c0 Same as\xFF" "c4 .gamblem Request" },
	{ "Autostock", Forward, "Autostock\xFF" "c0 Same as\xFF" "c4 .gamblem Autostock" },
	{NULL}
};
