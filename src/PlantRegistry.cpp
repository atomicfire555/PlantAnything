#include "PlantRegistry.h"
#include "Config.h"

namespace
{
	bool IsAcceptedItem(RE::TESForm* a_form)
	{
		switch (a_form->GetFormType()) {
		case RE::FormType::Ingredient:
			return true;
		case RE::FormType::AlchemyItem:
			if (auto* alch = a_form->As<RE::AlchemyItem>()) {
				return alch->IsFood() ? Config::includeFood : Config::includePotions;
			}
			return false;
		default:
			return false;  // gold (coin purses), misc items etc. are not plantable
		}
	}

	// Produce can be a single item or a leveled list (possibly nested).
	void CollectItems(RE::TESForm* a_form, std::vector<RE::TESBoundObject*>& a_out, int a_depth)
	{
		if (!a_form || a_depth > 8) {
			return;
		}
		if (auto* lvl = a_form->As<RE::TESLevItem>()) {
			for (auto& entry : lvl->entries) {
				CollectItems(entry.form, a_out, a_depth + 1);
			}
			return;
		}
		if (IsAcceptedItem(a_form)) {
			if (auto* bound = a_form->As<RE::TESBoundObject>()) {
				a_out.push_back(bound);
			}
		}
	}

	std::string_view FileName(RE::TESForm* a_form)
	{
		if (auto* file = a_form ? a_form->GetFile(0) : nullptr) {
			return file->GetFilename();
		}
		return "<runtime>"sv;
	}
}

void PlantRegistry::Consider(RE::TESBoundObject* a_plant, RE::TESProduceForm* a_produce, const std::unordered_set<RE::FormID>& a_excluded)
{
	if (!a_plant || !a_produce || !a_produce->produceItem || a_plant->IsDeleted()) {
		return;
	}

	// Skip markers / placeholders with no mesh.
	auto*       modelForm = a_plant->As<RE::TESModel>();
	const char* model = modelForm ? modelForm->GetModel() : nullptr;
	if (!model || model[0] == '\0') {
		return;
	}
	if (Config::IsPluginExcluded(FileName(a_plant))) {
		return;
	}

	std::vector<RE::TESBoundObject*> items;
	CollectItems(a_produce->produceItem, items, 0);

	for (auto* item : items) {
		if (a_excluded.contains(item->GetFormID())) {
			continue;
		}
		auto& variants = _map[item->GetFormID()];
		if (std::ranges::find(variants, a_plant) == variants.end()) {
			variants.push_back(a_plant);
		}
	}
}

void PlantRegistry::Build()
{
	_map.clear();
	auto* dh = RE::TESDataHandler::GetSingleton();
	if (!dh) {
		spdlog::error("TESDataHandler unavailable - nothing will be plantable");
		return;
	}

	const auto excluded = Config::ResolveExcludedItems();

	std::size_t floraCount = 0;
	for (auto* flora : dh->GetFormArray<RE::TESFlora>()) {
		Consider(flora, flora, excluded);
		++floraCount;
	}

	std::size_t treeCount = 0;
	if (Config::includeTrees) {
		for (auto* tree : dh->GetFormArray<RE::TESObjectTREE>()) {
			Consider(tree, tree, excluded);
			++treeCount;
		}
	}

	spdlog::info("Scanned {} flora and {} tree records -> {} plantable items", floraCount, treeCount, _map.size());

	// Full list in the log so you can confirm CC / mod coverage.
	for (const auto& [itemID, plants] : _map) {
		auto* item = RE::TESForm::LookupByID(itemID);
		spdlog::info("  PLANTABLE  {:<32} [{:08X}] {:<40} {} plant variant(s)",
			item ? item->GetName() : "?", itemID, FileName(item), plants.size());
	}

	// Ingredients that have no plant anywhere in the load order (animal parts, merchant-only items...).
	for (auto* ingr : dh->GetFormArray<RE::IngredientItem>()) {
		if (ingr && !_map.contains(ingr->GetFormID())) {
			spdlog::info("  NO PLANT   {:<32} [{:08X}] {}", ingr->GetName(), ingr->GetFormID(), FileName(ingr));
		}
	}
}

RE::TESBoundObject* PlantRegistry::PickPlantForSoil(RE::FormID a_item) const
{
	const auto it = _map.find(a_item);
	if (it == _map.end() || it->second.empty()) {
		return nullptr;
	}
	for (auto* plant : it->second) {
		if (plant->GetFormType() == RE::FormType::Flora) {
			return plant;
		}
	}
	return it->second.front();
}

std::vector<RE::FormID> PlantRegistry::SortedItems() const
{
	std::vector<RE::FormID> out;
	out.reserve(_map.size());
	for (const auto& [id, plants] : _map) {
		out.push_back(id);
	}
	std::ranges::sort(out);
	return out;
}

bool PlantRegistry::IsHarvestItem(RE::TESForm* a_form)
{
	if (!a_form) {
		return false;
	}
	const auto type = a_form->GetFormType();
	return type == RE::FormType::Ingredient || type == RE::FormType::AlchemyItem;
}

bool PlantRegistry::Produces(RE::TESForm* a_plant, RE::FormID a_item)
{
	if (!a_plant) {
		return false;
	}
	RE::TESProduceForm* produce = nullptr;
	if (auto* flora = a_plant->As<RE::TESFlora>()) {
		produce = flora;
	} else if (auto* tree = a_plant->As<RE::TESObjectTREE>()) {
		produce = tree;
	}
	if (!produce || !produce->produceItem) {
		return false;
	}
	if (produce->produceItem->GetFormID() == a_item) {
		return true;
	}
	std::vector<RE::TESBoundObject*> items;
	CollectItems(produce->produceItem, items, 0);
	return std::ranges::any_of(items, [&](auto* i) { return i->GetFormID() == a_item; });
}

RE::TESBoundObject* PlantRegistry::PickPlant(RE::FormID a_item) const
{
	const auto it = _map.find(a_item);
	if (it == _map.end() || it->second.empty()) {
		return nullptr;
	}
	const auto& variants = it->second;
	if (!Config::randomVariant || variants.size() == 1) {
		return variants.front();  // first in load order = usually the vanilla / original plant
	}
	static thread_local std::mt19937          rng{ std::random_device{}() };
	std::uniform_int_distribution<std::size_t> dist(0, variants.size() - 1);
	return variants[dist(rng)];
}
