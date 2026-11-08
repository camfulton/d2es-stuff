#include <queue>
#include <vector>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include "../../Includes/D2Client.h"

// Chat prefix: "ShopbotM: " in D2's colour codes (0xFF 'c' <colour>).
// Kept as escapes so the source stays plain ASCII.
#define CONFIG_PATH ".\\plugin\\shopbotm.ini"

#define SBM_TAG "\xFF" "c:ShopbotM" "\xFF" "c0: "

enum States
{
	STATE_UNINITIALIZED = 0,
	STATE_COMPLETE,

	// Opening the store once at the start
	STATE_START_SESSION,         // close any open UI, find the nearest merchant, open its store
	STATE_NPC_LISTINGITEMS,      // store opening; the merchant's items arrive (OnNpcItemList)
	STATE_NPC_DONELISTINGITEMS,  // the NPC session reported success

	// Buying whatever matched
	STATE_PURCHASE_NEXTITEM,
	STATE_PURCHASE_WAITFORITEM,
	STATE_PURCHASE_DONE,

	// The Refresh button loop (replaces the teleport-out-of-town loop)
	STATE_REFRESH_COOLDOWN,      // wait until the button's cooldown has passed, then press it
	STATE_REFRESH_LISTING,       // the new stock arrives (OnNpcItemList) until it goes quiet
};

class ShopBot
{
	public:
		ShopBot();
		// checkAffixes: buy only items with enough good prefixes/suffixes (the shop command, as in shopbot).
		// The chit command passes false: chits have no affixes.
		bool Start(const std::unordered_map<std::string, std::string>& itemsToShopFor, bool checkAffixes);
		// args: a base name ("light plate") or item codes ("ltp vgl", or chit codes); fills chits with code -> name
		bool FindChits(const std::vector<std::string> &args, std::unordered_map<std::string, std::string> &chits);
		void OnTick();
		void OnNpcSession(int success);
		void OnNpcItemList(ITEM &merchantItem);
		void OnItemToStorage(ITEM &merchantItem);
		void OnNotEnoughMoney();
		void OnServerPacket(const BYTE *aPacket, DWORD aLen);
		void Stop();

	private:
		bool ReadConfig(const std::string &configPath, std::unordered_set<int> &readTo);
		bool LoadItemMap(const std::string &fileName, std::unordered_map<std::string, std::string> &itemMap);
		bool IsItemGood(const ITEM &item);
		void PurchaseQueuedItems();
		bool WillItemFit(DWORD dwItemId);
		bool SendRefresh();
		void Finish(const char *reason);

		std::queue<DWORD> gambleQueue;

		DWORD FindMerchant();
		int minPrefix;
		int minSuffix;
		bool checkAffixes;
		States currentState;
		std::string merchantName;
		std::unordered_set<int> goodPrefix;
		std::unordered_set<int> goodSuffix;
		std::unordered_map<std::string, std::string> targetItems;

		GAMEUNIT merchantNpc;

		DWORD refreshCooldownMs;   // from shopbotm.ini: time between presses of Refresh, and how long to listen for the new stock

		DWORD stateStartTick;      // when the current wait began
		DWORD lastRefreshTick;     // when Refresh was last pressed (0 = never)
		int storeItemsSeen;        // store items received since the last press
		int refreshCount;
		int sessionAttempts;       // tries at opening the store this run
};
