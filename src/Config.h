#pragma once

// Settings read from Data/SKSE/Plugins/PlantAnything.ini
struct Config
{
	static inline bool  patchFertileSoil = true;    // add everything to Hearthfire / homestead fertile soil
	static inline bool  enableDropPlanting = false; // optional: sneak-drop an item anywhere to plant it

	static inline bool  requireSneak = true;    // must be sneaking when you drop the item
	static inline bool  allowInteriors = true;  // allow planting inside buildings / caves
	static inline bool  includeTrees = true;    // TREE records with produce (e.g. Juniper)
	static inline bool  includeFood = true;     // food produce (cabbage, potatoes, gourds...)
	static inline bool  includePotions = false; // non-food ALCH produce (rare, mod-added)
	static inline bool  randomVariant = false;  // pick a random plant model when several exist
	static inline bool  allowUproot = true;     // sneak-activate an already-harvested plant to dig it up
	static inline bool  showMessages = true;
	static inline float regrowDays = 3.0f;      // in-game days until a harvested planted plant regrows
	static inline float sproutDelay = 2.0f;     // real seconds between dropping and sprouting
	static inline float sinkDepth = 2.0f;       // push the plant slightly into the ground

	static inline std::vector<std::string> excludedPlugins;  // lower-case file names
	static inline std::vector<std::string> excludedItems;    // "Plugin.esp|0x123456"
	static inline std::vector<std::string> skippedPlanterLists;  // planter item FormLists to leave alone

	static void Load();
	static bool IsPluginExcluded(std::string_view a_fileName);
	static std::unordered_set<RE::FormID> ResolveExcludedItems() { return Resolve(excludedItems, "sExcludedItems"); }
	static std::unordered_set<RE::FormID> ResolveSkippedPlanterLists() { return Resolve(skippedPlanterLists, "sSkipPlanterLists"); }

private:
	static std::unordered_set<RE::FormID> Resolve(const std::vector<std::string>& a_entries, std::string_view a_key);
};
