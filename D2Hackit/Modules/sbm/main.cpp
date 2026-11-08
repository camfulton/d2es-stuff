// sbm: a short name for the shopbotm module.
//
// D2HackIt sends ".<name> ..." to the loaded module whose file is <name>.d2h, and has no aliases,
// so ".sbm" needs a module of its own. This one does no shopping: it passes every command on
// unchanged, so ".sbm c light plate" runs ".shopbotm c light plate". shopbotm is loaded on first use.
#include "../../Includes/ClientCore.cpp"
#include <string>
#include <vector>

#define SBM_FORWARD_TAG "\xFF" "c:sbm" "\xFF" "c0: "

static const char *TARGET_MODULE = "shopbotm";
static const char *TARGET_FILE = "shopbotm.d2h";

BOOL PRIVATE Forward(char** argv, int argc)
{
	if (GetModuleHandle(TARGET_FILE) == NULL)
	{
		server->GameCommandf("load %s", TARGET_MODULE);

		if (GetModuleHandle(TARGET_FILE) == NULL)
		{
			server->GameStringf(SBM_FORWARD_TAG "Could not load %s; is it in the plugin folder?", TARGET_FILE);
			return TRUE;
		}
	}

	// argv[0] is "sbm"; the rest goes through as typed
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

// Every shopbotm command, so D2HackIt routes each one here; help and usage come from shopbotm itself.
MODULECOMMANDSTRUCT ModuleCommands[]=
{
	{ "help", Forward, "help\xFF" "c0 Same as\xFF" "c4 .shopbotm help" },
	{ "shop", Forward, "shop [item codes]\xFF" "c0 Same as\xFF" "c4 .shopbotm shop" },
	{ "s",    Forward, "s [item codes]\xFF" "c0 Same as\xFF" "c4 .shopbotm s" },
	{ "chit", Forward, "chit <base name | item codes>\xFF" "c0 Same as\xFF" "c4 .shopbotm chit" },
	{ "c",    Forward, "c <base name | item codes>\xFF" "c0 Same as\xFF" "c4 .shopbotm c" },
	{ "stop", Forward, "stop\xFF" "c0 Same as\xFF" "c4 .shopbotm stop" },
	{NULL}
};
