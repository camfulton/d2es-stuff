// ItemLookup.h: turns what a player types after a command into item codes, shared by shopbotm
// (".sbm c ...") and gamblem (".gm start ..."), so both read names and codes the same way.
//
// The words can be read two ways:
//   as item codes  - every word is a 3-letter code ("rin amu"), the way these commands always worked
//   as a name      - the whole phrase is an item's name, of any tier ("death mask", "war hat")
// When only one reading makes sense it is used. When both do and they mean the same items (e.g. "cap"
// is both Cap's code and its name) that is fine too. When both do but mean different items, nothing is
// chosen and the player is told how to say which they meant:
//   #eye     - a '#' in front of every word forces item codes
//   'eye'    - single quotes around the phrase force a name (D2HackIt strips double quotes, so a
//              one-word "eye" could not be told apart from a plain eye)
//
// Needs D2Client.h (for server) to be included first. Header-only: each module compiles its own copy.
#pragma once

#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace ItemLookup
{
	static const char *const TIER_NAMES[] = { "normal", "exceptional", "elite" };

	// Removes D2 colour codes (0xFF 'c' <colour>) from an item name
	inline std::string StripColourCodes(const std::string &text)
	{
		std::string result;
		for (size_t i = 0; i < text.size(); i++)
		{
			if ((unsigned char)text[i] == 0xFF && i + 2 < text.size() && text[i + 1] == 'c')
			{
				i += 2;
				continue;
			}
			result += text[i];
		}
		return result;
	}

	inline std::string ToLower(std::string text)
	{
		for (auto &c : text)
		{
			c = (char)tolower((unsigned char)c);
		}
		return text;
	}

	inline std::string Trim(const std::string &text)
	{
		const size_t first = text.find_first_not_of(" \t\r\n");
		if (first == std::string::npos)
		{
			return "";
		}
		const size_t last = text.find_last_not_of(" \t\r\n");
		return text.substr(first, last - first + 1);
	}

	// Where a base sits in its normal -> exceptional -> elite line
	struct TierLink
	{
		std::string normal;     // the normal base's code
		int tier;               // 0 normal, 1 exceptional, 2 elite
		std::string chitBase;   // the name its chits use, when it differs from the normal base's name
	};

	// AllItems.txt and a tiers file, indexed for lookups
	struct Table
	{
		std::unordered_map<std::string, std::string> names;                    // code -> name, colour codes removed
		std::unordered_map<std::string, std::vector<std::string>> byName;      // lower-case name -> codes, in file order
		std::unordered_map<std::string, TierLink> tiers;                       // any tier's code -> its line
	};

	inline std::string NameOf(const Table &table, const std::string &code)
	{
		const auto found = table.names.find(code);
		return found == table.names.end() ? code : found->second;
	}

	inline bool IsKnownCode(const Table &table, const std::string &code)
	{
		return table.names.count(code) > 0 || table.tiers.count(code) > 0;
	}

	// AllItems.txt: "code name" per line
	inline bool LoadNames(Table &table, const char *path, const char *tag)
	{
		std::ifstream inFile(path);
		if (!inFile)
		{
			server->GameStringf("%sFailed to open file %s", tag, path);
			return false;
		}

		std::string line;
		while (std::getline(inFile, line))
		{
			line = Trim(line);
			if (line.length() <= 4 || line.at(3) != ' ')
			{
				continue;
			}

			const std::string code = ToLower(line.substr(0, 3));
			if (table.names.count(code) > 0)
			{
				continue; // keep the first entry for a code
			}

			const std::string name = Trim(StripColourCodes(line.substr(4)));
			table.names[code] = name;
			table.byName[ToLower(name)].push_back(code);
		}

		return true;
	}

	// "<normal> <exceptional> <elite> [chit=<name>] ; comment" per line. Optional: without it only
	// normal bases are understood.
	inline void LoadTiers(Table &table, const char *path, const char *tag)
	{
		std::ifstream inFile(path);
		if (!inFile)
		{
			server->GameStringf("%sFailed to open file %s; exceptional and elite names will not be recognised", tag, path);
			return;
		}

		std::string line;
		while (std::getline(inFile, line))
		{
			const size_t comment = line.find(';');
			if (comment != std::string::npos)
			{
				line = line.substr(0, comment);
			}
			line = Trim(line);
			if (line.empty())
			{
				continue;
			}

			std::string chitBase;
			const size_t chitField = line.find("chit=");
			if (chitField != std::string::npos)
			{
				chitBase = Trim(line.substr(chitField + 5));
				line = Trim(line.substr(0, chitField));
			}

			std::vector<std::string> codes;
			size_t start = 0;
			while (start < line.size())
			{
				const size_t end = line.find(' ', start);
				const std::string word = line.substr(start, end == std::string::npos ? std::string::npos : end - start);
				if (!word.empty())
				{
					codes.push_back(ToLower(word));
				}
				if (end == std::string::npos)
				{
					break;
				}
				start = end + 1;
			}

			if (codes.size() != 3)
			{
				continue;
			}

			for (int tier = 0; tier < 3; tier++)
			{
				table.tiers[codes[tier]] = TierLink{ codes[0], tier, chitBase };
			}
		}
	}

	// The typed words, split into the two readings' inputs
	struct Input
	{
		std::string phrase;               // all the words, for the name reading
		std::vector<std::string> codes;   // the words lower-cased (without '#'), for the code reading
		bool couldBeCodes = false;        // every word is 3 letters
		bool forceCodes = false;          // #code
		bool forceName = false;           // 'name'
	};

	inline bool ParseInput(const std::vector<std::string> &args, Input &input, const char *tag)
	{
		input = Input();

		std::string phrase;
		for (const auto &arg : args)
		{
			phrase += (phrase.empty() ? "" : " ") + arg;
		}
		phrase = Trim(phrase);

		if (phrase.size() >= 2 && phrase.front() == '\'' && phrase.back() == '\'')
		{
			input.forceName = true;
			input.phrase = Trim(phrase.substr(1, phrase.size() - 2));
			if (input.phrase.empty())
			{
				server->GameStringf("%sNothing between the quotes", tag);
				return false;
			}
			return true;
		}
		input.phrase = phrase;

		int hashed = 0;
		for (const auto &arg : args)
		{
			if (!arg.empty() && arg[0] == '#')
			{
				hashed++;
			}
		}
		if (hashed > 0 && hashed != (int)args.size())
		{
			server->GameStringf("%sPut # in front of every item code (e.g. #rin #amu), or none", tag);
			return false;
		}
		input.forceCodes = hashed > 0;

		input.couldBeCodes = !args.empty();
		for (const auto &arg : args)
		{
			const std::string code = ToLower(input.forceCodes ? arg.substr(1) : arg);
			input.codes.push_back(code);

			// A double-quoted argument can hold spaces; it can never be a code
			if (code.size() != 3 || code.find(' ') != std::string::npos)
			{
				input.couldBeCodes = false;
			}
		}

		if (input.forceCodes && !input.couldBeCodes)
		{
			server->GameStringf("%sItem codes are 3 letters (e.g. #rin)", tag);
			return false;
		}

		return true;
	}

	// What one reading of the input resolves to
	struct Reading
	{
		bool valid = false;                          // this reading makes sense
		bool known = true;                           // (codes) every code is a known item
		std::map<std::string, std::string> items;    // code -> name of what it resolves to (sorted, so readings compare)
		std::vector<std::string> notes;              // printed if this reading is chosen
		std::string error;                           // why it does not work, if not valid
	};

	inline std::string Describe(const std::map<std::string, std::string> &items)
	{
		std::string text;
		int shown = 0;
		for (const auto &item : items)
		{
			if (shown == 6)
			{
				text += ", ...";
				break;
			}
			text += (shown == 0 ? "" : ", ") + item.second + " (" + item.first + ")";
			shown++;
		}
		return text;
	}

	// Picks the reading to use, or nullptr. When the two readings disagree that is explained here and
	// explained is set; otherwise (neither works) the caller explains, e.g. with Suggest.
	inline const Reading *Choose(const Input &input, const Reading &codes, const Reading &name, bool &explained, const char *tag)
	{
		explained = false;

		if (input.forceCodes)
		{
			return codes.valid ? &codes : nullptr;
		}
		if (input.forceName)
		{
			return name.valid ? &name : nullptr;
		}

		const bool codesSure = codes.valid && codes.known;
		if (codesSure && name.valid && codes.items != name.items)
		{
			std::string hashed;
			for (const auto &code : input.codes)
			{
				hashed += (hashed.empty() ? "#" : " #") + code;
			}
			server->GameStringf("%s\"%s\" could mean two things:", tag, input.phrase.c_str());
			server->GameStringf("%s  as item codes: %s", tag, Describe(codes.items).c_str());
			server->GameStringf("%s  as the name \"%s\": %s", tag, input.phrase.c_str(), Describe(name.items).c_str());
			server->GameStringf("%sType\xFF" "c4 %s\xFF" "c0 for the codes or\xFF" "c4 '%s'\xFF" "c0 for the name", tag, hashed.c_str(), input.phrase.c_str());
			explained = true;
			return nullptr;
		}

		if (codesSure)
		{
			return &codes;   // the long-standing meaning; any name reading agrees with it
		}
		if (name.valid)
		{
			return &name;
		}
		if (codes.valid)
		{
			return &codes;   // codes AllItems.txt does not know, accepted as before
		}
		return nullptr;
	}

	// "No ... for x. Did you mean: ..." from item names containing what was typed
	inline void Suggest(const Table &table, const std::string &phrase, const std::set<std::string> &extraNames,
		const char *what, const char *tag)
	{
		const std::string wanted = ToLower(phrase);

		std::set<std::string> candidates = extraNames;
		for (const auto &item : table.names)
		{
			// Skip chits and coupons ("(Cap) Gambling") and descriptions stored as names (long, with "\n" in them)
			if (!item.second.empty() && item.second[0] != '(' && item.second.size() <= 40 &&
				item.second.find('\\') == std::string::npos && ToLower(item.second).find(wanted) != std::string::npos)
			{
				candidates.insert(item.second);
			}
		}

		std::string suggestions;
		int shown = 0;
		for (const auto &name : candidates)
		{
			if (ToLower(name).find(wanted) == std::string::npos)
			{
				continue;
			}
			if (shown == 10)
			{
				suggestions += ", ...";
				break;
			}
			suggestions += (shown == 0 ? "" : ", ") + name;
			shown++;
		}

		if (shown == 0)
		{
			server->GameStringf("%sNo %s found for \"%s\"", tag, what, phrase.c_str());
		}
		else
		{
			server->GameStringf("%sNo %s for \"%s\". Did you mean: %s", tag, what, phrase.c_str(), suggestions.c_str());
		}
	}
}
