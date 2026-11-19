#include "SoilPlants.h"
#include "Config.h"
#include "PlantRegistry.h"

namespace
{
	constexpr std::string_view kPluginName = "PlantAnything.esp"sv;
	constexpr RE::FormID       kFirstPlaceholder = 0x800;
	constexpr std::uint32_t    kPlaceholderCount = 1024;
	constexpr const char*      kSlotFile = "Data/SKSE/Plugins/PlantAnything_Slots.txt";

	std::vector<RE::TESFlora*>                     g_placeholders;
	std::unordered_set<RE::FormID>                 g_placeholderIDs;
	std::unordered_map<RE::FormID, RE::TESFlora*>  g_byItem;
	std::vector<RE::FormID>                        g_extraItems;

	std::string Lower(std::string_view a_str)
	{
		std::string out(a_str);
		std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return out;
	}

	bool ContainsAny(const std::string& a_hay, std::initializer_list<const char*> a_needles)
	{
		return std::ranges::any_of(a_needles, [&](const char* n) { return a_hay.find(n) != std::string::npos; });
	}

	// Stable key for an item across load-order changes: "plugin|localID".
	std::string ItemKey(RE::TESForm* a_form)
	{
		auto* file = a_form ? a_form->GetFile(0) : nullptr;
		if (!file) {
			return {};
		}
		const auto local = file->IsLight() ? (a_form->GetFormID() & 0xFFF) : (a_form->GetFormID() & 0xFFFFFF);
		return std::format("{}|{:06X}", Lower(file->GetFilename()), local);
	}

	// ---- slot file: which placeholder belongs to which item (kept across saves and launches)

	std::unordered_map<std::string, std::uint32_t> LoadSlots()
	{
		std::unordered_map<std::string, std::uint32_t> slots;
		std::ifstream                                   in(kSlotFile);
		std::string                                     line;
		while (std::getline(in, line)) {
			const auto eq = line.find('=');
			if (eq == std::string::npos) {
				continue;
			}
			try {
				slots[line.substr(eq + 1)] = static_cast<std::uint32_t>(std::stoul(line.substr(0, eq)));
			} catch (...) {
			}
		}
		return slots;
	}

	void SaveSlots(const std::unordered_map<std::string, std::uint32_t>& a_slots)
	{
		std::vector<std::pair<std::uint32_t, std::string>> sorted;
		for (const auto& [key, slot] : a_slots) {
			sorted.emplace_back(slot, key);
		}
		std::ranges::sort(sorted);
		std::ofstream out(kSlotFile, std::ios::trunc);
		out << "# Plant Anything: placeholder plant slot -> item. Keeps planted placeholder plants\n"
		       "# stable across launches. Delete only if you start a new game.\n";
		for (const auto& [slot, key] : sorted) {
			out << slot << '=' << key << '\n';
		}
	}

	// ---- templates: real crop meshes to dress placeholders in

	enum class Kind
	{
		kAllium,
		kRoot,
		kMushroom,
		kHerb
	};

	Kind KindOf(RE::TESForm* a_item)
	{
		const auto name = Lower(a_item->GetName());
		if (ContainsAny(name, { "garlic", "onion", "leek", "chive", "shallot" })) {
			return Kind::kAllium;
		}
		if (ContainsAny(name, { "mushroom", "amanita", "polypore", "russula", "bolete", "stool", "fung", "entoloma",
								  "funnel", "pholiota", "stinkhorn", "lichen", "spore", " cap", "tapinella", "cup", "bloat" })) {
			return Kind::kMushroom;
		}
		if (ContainsAny(name, { "potato", "carrot", "radish", "beet", "root", "yam", "turnip", "cabbage", "lettuce",
								  "corn", "gourd", "tomato", "pepper", "artichoke", "rice", "wheat", "grain" })) {
			return Kind::kRoot;
		}
		return Kind::kHerb;
	}

	struct Templates
	{
		RE::TESFlora* allium{ nullptr };
		RE::TESFlora* root{ nullptr };
		RE::TESFlora* mushroom{ nullptr };
		RE::TESFlora* herb{ nullptr };

		RE::TESFlora* For(Kind a_kind) const
		{
			RE::TESFlora* pick = nullptr;
			switch (a_kind) {
			case Kind::kAllium:
				pick = allium ? allium : root;
				break;
			case Kind::kRoot:
				pick = root;
				break;
			case Kind::kMushroom:
				pick = mushroom;
				break;
			default:
				pick = herb;
				break;
			}
			return pick ? pick : (herb ? herb : (root ? root : mushroom));
		}
	};

	RE::TESFlora* AsGoodFlora(RE::TESForm* a_form)
	{
		auto* flora = a_form ? a_form->As<RE::TESFlora>() : nullptr;
		return flora && SoilPlants::IsGoodSoilPlant(flora) && !SoilPlants::IsPlaceholder(flora) ? flora : nullptr;
	}

	// Prefer the plant Hearthfire itself grows for that item (sized for planters).
	RE::TESFlora* FindTemplate(std::string_view a_itemName, RE::FormID a_vanillaItem)
	{
		auto* dh = RE::TESDataHandler::GetSingleton();
		auto* items = dh->LookupForm<RE::BGSListForm>(0x008247, "HearthFires.esm"sv);
		auto* flora = dh->LookupForm<RE::BGSListForm>(0x008246, "HearthFires.esm"sv);
		if (items && flora) {
			const auto n = (std::min)(items->forms.size(), flora->forms.size());
			for (std::uint32_t i = 0; i < n; ++i) {
				auto* item = items->forms[i];
				if (item && (item->GetFormID() == a_vanillaItem || Lower(item->GetName()) == Lower(a_itemName))) {
					if (auto* good = AsGoodFlora(flora->forms[i])) {
						return good;
					}
				}
			}
		}
		if (const auto* variants = PlantRegistry::GetSingleton()->Variants(a_vanillaItem)) {
			for (auto* v : *variants) {
				if (auto* good = AsGoodFlora(v)) {
					return good;
				}
			}
		}
		return nullptr;
	}

	void Dress(RE::TESFlora* a_ph, RE::TESBoundObject* a_item, RE::TESFlora* a_template)
	{
		static_cast<RE::TESModel*>(a_ph)->SetModel(static_cast<RE::TESModel*>(a_template)->GetModel());
		a_ph->fullName = a_item->GetName();
		a_ph->produceItem = a_item;
		a_ph->harvestSound = a_template->harvestSound;
		for (auto& chance : a_ph->produceChance) {
			chance = 100;
		}
		a_ph->boundData = a_template->boundData;
	}

	bool HasGoodVariant(RE::FormID a_item)
	{
		const auto* variants = PlantRegistry::GetSingleton()->Variants(a_item);
		return variants && std::ranges::any_of(*variants, [](RE::TESBoundObject* v) { return SoilPlants::IsGoodSoilPlant(v); });
	}
}

bool SoilPlants::IsPlaceholder(const RE::TESForm* a_form)
{
	return a_form && g_placeholderIDs.contains(a_form->GetFormID());
}

bool SoilPlants::IsGoodSoilPlant(RE::TESBoundObject* a_plant)
{
	if (!a_plant) {
		return false;
	}
	if (IsPlaceholder(a_plant)) {
		return true;
	}
	auto*       model = a_plant->As<RE::TESModel>();
	const char* path = model ? model->GetModel() : nullptr;
	if (!path || !*path) {
		return false;
	}
	if (ContainsAny(Lower(path), { "hanging", "braid", "dried", "wallmount", "ceiling" })) {
		return false;  // decorations that hang from a hook
	}

	const auto& b = a_plant->boundData;
	const bool  noBounds = b.boundMin.x == 0 && b.boundMin.y == 0 && b.boundMin.z == 0 &&
	                      b.boundMax.x == 0 && b.boundMax.y == 0 && b.boundMax.z == 0;
	if (noBounds) {
		return true;  // can't tell - trust the mesh path
	}
	if (b.boundMax.z <= 2 || b.boundMin.z < -40) {
		return false;  // hangs below its origin -> would spawn underground
	}
	const int width = b.boundMax.x - b.boundMin.x;
	const int depth = b.boundMax.y - b.boundMin.y;
	const int height = b.boundMax.z - b.boundMin.z;
	return width <= 250 && depth <= 250 && height <= 300;  // no trees, carcasses or furniture
}

RE::TESBoundObject* SoilPlants::Placeholder(RE::FormID a_item)
{
	const auto it = g_byItem.find(a_item);
	return it == g_byItem.end() ? nullptr : it->second;
}

const std::vector<RE::FormID>& SoilPlants::ExtraItems()
{
	return g_extraItems;
}

void SoilPlants::Init()
{
	auto* dh = RE::TESDataHandler::GetSingleton();
	if (!dh->LookupModByName(kPluginName)) {
		spdlog::warn("{} is not enabled - items without a proper soil plant can't be planted", kPluginName);
		return;
	}
	for (std::uint32_t i = 0; i < kPlaceholderCount; ++i) {
		auto* ph = dh->LookupForm<RE::TESFlora>(kFirstPlaceholder + i, kPluginName);
		g_placeholders.push_back(ph);
		if (ph) {
			g_placeholderIDs.insert(ph->GetFormID());
		}
	}

	Templates t;
	t.allium = FindTemplate("Leek", 0);
	t.root = FindTemplate("Potato", 0x00064B41);
	t.mushroom = FindTemplate("Imp Stool", 0x0004DA23);
	t.herb = FindTemplate("Lavender", 0x00045C28);
	spdlog::info("Placeholder templates: allium {:08X}, root {:08X}, mushroom {:08X}, herb {:08X}",
		t.allium ? t.allium->GetFormID() : 0, t.root ? t.root->GetFormID() : 0,
		t.mushroom ? t.mushroom->GetFormID() : 0, t.herb ? t.herb->GetFormID() : 0);
	if (!t.For(Kind::kHerb)) {
		spdlog::error("No template plant found - placeholders disabled");
		return;
	}

	// Which items need a placeholder?
	auto*                   registry = PlantRegistry::GetSingleton();
	std::vector<RE::FormID> needs;
	for (const auto id : registry->SortedItems()) {
		if (!HasGoodVariant(id)) {
			needs.push_back(id);
		}
	}
	if (Config::soilAllIngredients) {
		const auto excluded = Config::ResolveExcludedItems();
		for (auto* ingr : dh->GetFormArray<RE::IngredientItem>()) {
			if (ingr && !registry->IsPlantable(ingr->GetFormID()) && !excluded.contains(ingr->GetFormID()) &&
				ingr->GetName() && *ingr->GetName()) {
				needs.push_back(ingr->GetFormID());
				g_extraItems.push_back(ingr->GetFormID());
			}
		}
	}

	// Assign slots: keep earlier assignments, give new items free slots.
	auto                     slots = LoadSlots();
	std::vector<bool>        used(kPlaceholderCount, false);
	for (const auto& [key, slot] : slots) {
		if (slot < kPlaceholderCount) {
			used[slot] = true;
		}
	}
	bool        changed = false;
	std::size_t dressed = 0, full = 0;
	for (const auto id : needs) {
		auto* item = RE::TESForm::LookupByID<RE::TESBoundObject>(id);
		const auto key = ItemKey(item);
		if (!item || key.empty()) {
			continue;
		}
		std::uint32_t slot = 0;
		if (auto it = slots.find(key); it != slots.end() && it->second < kPlaceholderCount) {
			slot = it->second;
		} else {
			auto freeIt = std::ranges::find(used, false);
			if (freeIt == used.end()) {
				++full;
				continue;
			}
			slot = static_cast<std::uint32_t>(std::distance(used.begin(), freeIt));
			used[slot] = true;
			slots[key] = slot;
			changed = true;
		}
		auto* ph = g_placeholders[slot];
		auto* tmpl = t.For(KindOf(item));
		if (!ph || !tmpl) {
			continue;
		}
		Dress(ph, item, tmpl);
		g_byItem[id] = ph;
		++dressed;
		spdlog::info("  placeholder {:08X} -> {} [{:08X}] (looks like {:08X})", ph->GetFormID(), item->GetName(), id, tmpl->GetFormID());
	}
	if (changed) {
		SaveSlots(slots);
	}
	if (full) {
		spdlog::warn("{} items didn't get a placeholder (all {} slots used)", full, kPlaceholderCount);
	}
	spdlog::info("Placeholders: {} items use a placeholder plant ({} have no plant at all)", dressed, g_extraItems.size());
}
