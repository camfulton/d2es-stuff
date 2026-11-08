#ifndef _GAMBLER_H_
#define _GAMBLER_H_

#include <unordered_set>
#include <string>
#include <queue>
#include <vector>
#include "../../Includes/D2Client.h"

#define CONFIG_PATH ".\\plugin\\gamblem.ini"
#define GM_TAG "\xFF" "c3GambleM\xFF" "c0: "

enum States
{
	STATE_UNINITIALIZED = 0,
	STATE_NPC_LISTINGITEMS,
	STATE_GAMBLE_WAITFORITEM,
	STATE_GAMBLE_SOLDITEM,
	STATE_GOLD_WAIT,
	STATE_AUTOSTOCK_RUNNING,
	STATE_AUTOSTOCK_ENDED,
	STATE_UI_CLOSING_FOR_AUTOSTOCKER,

	// The Refresh button loop (replaces closing and reopening the gamble window)
	STATE_REFRESH_COOLDOWN,      // wait until the button's cooldown has passed, then press it
	STATE_REFRESH_LISTING,       // the new gamble stock arrives (OnNpcGambleItemList)
};

struct GambleStats
{
	GambleStats()
	{
		uniques = 0;
		rares = 0;
		sets = 0;
		magic = 0;
	}

	int uniques;
	int rares;
	int sets;
	int magic;
};

class Gambler
{
	public:
		Gambler();
		bool Init(const std::vector<std::string> &itemCodes);
		void StopGambling();
		bool StartGambling();
		void SetAutostockStartDelay(int ticks);
		void ToggleGambleSell(bool magics, bool sellSet, bool sellRare, bool sellUnique);
		void ToggleRequestGold(int splitBy);
		void ToggleAutostock(bool transmuteSet, bool transmuteRare, bool transmuteUnique);
		void SetNoSellList(const std::vector<std::string> &noSellList);
		
		void OnItemToStorage(ITEM &gambleItem);
		void OnNpcSession(int success);
		void OnNpcGambleItemList(ITEM &gambleItem);
		void OnTick();
		void OnNotEnoughMoney();
		void OnItemSold();
		void OnGoldPickup();
		bool OnAutostockerMessage(const std::string_view &message);
		void OnItemIdentifiedSounded();
		void OnUIClosed();
		void OnServerPacket(const BYTE *aPacket, DWORD aLen);
		
	private:
		void GambleQueuedItems();
		void SellQueuedItems();
		void ResetQueues();
		bool WillItemFit(DWORD dwItemId);
		bool WillItemFit(const char *itemCode);
		void AttemptToMakeRoom();
		DWORD FindGamblingNpc();

		void StartAutostocker();
		void RequestMoreGold();

		void ReadConfig();
		bool SendRefresh();
		void CloseGambleWindow();

		int ticksTillGambleItemTimeout;

		int requestedGoldSplitBy;

		bool isRequestingGold;

		bool transmuteEnabled;
		bool transmuteSet;
		bool transmuteRare;
		bool transmuteUnique;

		bool sellMagic;
		bool sellRare;
		bool sellSet;
		bool sellUnique;
		bool isUsingNoSellList;

		GambleStats stats;
		States currentState;
		GAMEUNIT gamblingNpc;
		std::queue<DWORD> gambleQueue;
		std::queue<DWORD> itemsToSell;
		std::unordered_set<std::string> noSellList;
		std::unordered_set<std::string> itemsToGamble;

		bool sessionOpen;          // the gamble window is open, so Refresh can reroll it in place
		DWORD refreshCooldownMs;   // from gamblem.ini: time between presses of Refresh, and how long to listen for the new stock
		DWORD stateStartTick;      // when the current refresh wait began
		DWORD lastRefreshTick;     // when Refresh was last pressed (0 = never)
		int itemsSeen;             // gamble items received since the last press
		int refreshCount;
};

#endif