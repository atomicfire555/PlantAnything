#include "Soil.h"
#include "Config.h"
#include "PlantRegistry.h"
#include "SoilPlants.h"

// Fertile soil = a planter script (vanilla: BYOHPlanterContainerScript) whose properties point
// at an item FormList and one or more flora FormLists that are read index-for-index:
//   flPlanterPlantableItem[i]  ->  flPlanterPlantedFlora[i]   (CACO adds ...PlantedFloraFertilized)
//
// After a save loads we ask the Papyrus VM which lists those scripts really use, repair any
// item that has no plant at its index, then add every missing plantable at the same index in
// all of the group's lists. If no planter scripts are found we fall back to matching lists.

namespace
{
	std::string Lower(std::string_view a_str)
	{
		std::string out(a_str);
		std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return out;
	}

	// What Papyrus GetAt() sees, index for index: [runtime-created script adds][forms].
	// (Script-added forms from plugins live in `forms` itself, after the plugin entries.)
	struct ListView
	{
		RE::BGSListForm*          list{ nullptr };
		std::vector<RE::TESForm*> view;
		std::uint32_t             tempCount{ 0 };
	};

	ListView MakeView(RE::BGSListForm* a_list)
	{
		ListView v;
		v.list = a_list;
		if (a_list->scriptAddedTempForms) {
			for (const auto id : *a_list->scriptAddedTempForms) {
				v.view.push_back(RE::TESForm::LookupByID(id));
				++v.tempCount;
			}
		}
		for (auto* form : a_list->forms) {
			v.view.push_back(form);  // keep nulls: they still take an index
		}
		return v;
	}

	std::string Describe(RE::BGSListForm* a_list)
	{
		auto* file = a_list->GetFile(0);
		const auto v = MakeView(a_list);
		return std::format("{} [{:08X}] ({} entries{})", file ? std::string(file->GetFilename()) : "<runtime>"s,
			a_list->GetFormID(), v.view.size(), v.tempCount ? std::format(", {} runtime", v.tempCount) : ""s);
	}

	std::string NameOf(RE::TESForm* a_form)
	{
		return a_form ? std::format("{} [{:08X}]", a_form->GetName(), a_form->GetFormID()) : "None"s;
	}

	bool IsPlantForm(RE::TESForm* a_form)
	{
		if (!a_form) {
			return false;
		}
		const auto type = a_form->GetFormType();
		return type == RE::FormType::Flora || type == RE::FormType::Tree;
	}

	struct PlanterGroup
	{
		RE::BGSListForm*              items{ nullptr };
		std::vector<RE::BGSListForm*> floraLists;
	};

	// ------------------------------------------------------------ 1) ask the VM

	std::vector<PlanterGroup> FindFromScripts()
	{
		std::vector<PlanterGroup> groups;
		auto*                     vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		if (!vm) {
			return groups;
		}

		struct Seen
		{
			std::string                   script;
			std::size_t                   instances{ 0 };
			std::vector<RE::BGSListForm*> flora;
		};
		std::unordered_map<RE::BGSListForm*, Seen> byItems;  // item list -> flora lists read with it
		std::vector<RE::BGSListForm*>              order;

		{
			RE::BSSpinLockGuard locker(vm->attachedScriptsLock);
			for (auto& [handle, scripts] : vm->attachedScripts) {
				for (auto& attached : scripts) {
					auto* obj = attached.get();
					auto* type = obj ? obj->GetTypeInfo() : nullptr;
					if (!type || !type->GetName()) {
						continue;
					}
					const auto scriptName = Lower(type->GetName());
					if (scriptName.find("planter") == std::string::npos) {
						continue;
					}

					RE::BGSListForm*              items = nullptr;
					std::vector<RE::BGSListForm*> flora;
					auto*                         props = type->GetPropertyIter();
					for (std::uint32_t i = 0; props && i < type->GetNumProperties(); ++i) {
						const auto propName = Lower(props[i].name.c_str());
						auto*      var = obj->GetProperty(props[i].name);
						if (!var || !var->IsObject()) {
							continue;
						}
						auto* list = var->Unpack<RE::BGSListForm*>();
						if (!list) {
							continue;
						}
						if (propName.find("plantableitem") != std::string::npos) {
							items = list;
						} else if (propName.find("plantedflora") != std::string::npos) {
							flora.push_back(list);
						}
					}
					if (!items || flora.empty()) {
						continue;
					}

					auto [it, inserted] = byItems.try_emplace(items);
					if (inserted) {
						it->second.script = type->GetName();
						order.push_back(items);
					}
					++it->second.instances;
					for (auto* f : flora) {
						if (std::ranges::find(it->second.flora, f) == it->second.flora.end()) {
							it->second.flora.push_back(f);
						}
					}
				}
			}
		}

		for (auto* items : order) {
			const auto& seen = byItems[items];
			spdlog::info("Planter script '{}' ({} soil plot(s)) reads:", seen.script, seen.instances);
			spdlog::info("    items {}", Describe(items));
			for (auto* f : seen.flora) {
				spdlog::info("    flora {}", Describe(f));
			}
			groups.push_back(PlanterGroup{ items, seen.flora });
		}
		return groups;
	}

	// ------------------------------------------------------------ 2) fallback: match lists

	std::size_t AlignedMatches(const ListView& a_items, const ListView& a_flora)
	{
		const auto  n = (std::min)(a_items.view.size(), a_flora.view.size());
		std::size_t matches = 0;
		for (std::size_t i = 0; i < n; ++i) {
			auto* item = a_items.view[i];
			auto* plant = a_flora.view[i];
			if (item && plant && PlantRegistry::Produces(plant, item->GetFormID())) {
				++matches;
			}
		}
		return matches;
	}

	std::vector<PlanterGroup> FindByMatching()
	{
		auto&                 lists = RE::TESDataHandler::GetSingleton()->GetFormArray<RE::BGSListForm>();
		std::vector<ListView> itemLists, floraLists;
		for (auto* list : lists) {
			if (!list) {
				continue;
			}
			auto       v = MakeView(list);
			const auto size = v.view.size();
			if (size < 3) {
				continue;
			}
			const auto harvest = static_cast<std::size_t>(std::ranges::count_if(v.view, PlantRegistry::IsHarvestItem));
			const auto plants = static_cast<std::size_t>(std::ranges::count_if(v.view, IsPlantForm));
			if (harvest * 10 >= size * 8) {
				itemLists.push_back(std::move(v));
			} else if (plants * 10 >= size * 5) {
				floraLists.push_back(std::move(v));
			}
		}

		std::vector<PlanterGroup> groups;
		for (auto& items : itemLists) {
			PlanterGroup group{ items.list, {} };
			for (auto& flora : floraLists) {
				const auto matches = AlignedMatches(items, flora);
				if (matches >= 10 && flora.view.size() == items.view.size()) {
					group.floraLists.push_back(flora.list);
				} else if (matches >= 10) {
					spdlog::info("  (not used) {} vs {}: {} matching positions", Describe(items.list), Describe(flora.list), matches);
				}
			}
			if (!group.floraLists.empty()) {
				spdlog::info("Matched planter lists: items {}", Describe(group.items));
				for (auto* f : group.floraLists) {
					spdlog::info("    flora {}", Describe(f));
				}
				groups.push_back(std::move(group));
			}
		}
		return groups;
	}

	// ------------------------------------------------------------ patching

	// The plant a_item should grow into: a variant that grows properly in soil and isn't already
	// in any of the group's flora lists (FormLists drop duplicates when the game restores them,
	// which would knock the lists out of step), otherwise the item's placeholder plant.
	RE::TESBoundObject* PickUniquePlant(RE::FormID a_item, const PlanterGroup& a_group, const std::unordered_set<RE::TESForm*>& a_used)
	{
		auto usable = [&](RE::TESBoundObject* p) {
			if (!p || a_used.contains(p)) {
				return false;
			}
			return std::ranges::none_of(a_group.floraLists, [&](RE::BGSListForm* f) { return f->HasForm(p); });
		};
		if (const auto* variants = PlantRegistry::GetSingleton()->Variants(a_item)) {
			for (auto* p : *variants) {
				if (p->GetFormType() == RE::FormType::Flora && SoilPlants::IsGoodSoilPlant(p) && usable(p)) {
					return p;
				}
			}
			for (auto* p : *variants) {
				if (SoilPlants::IsGoodSoilPlant(p) && usable(p)) {
					return p;
				}
			}
		}
		auto* ph = SoilPlants::Placeholder(a_item);
		return usable(ph) ? ph : nullptr;
	}

	void InsertAt(RE::BGSListForm* a_list, std::size_t a_viewPos, const std::vector<RE::TESForm*>& a_add)
	{
		const auto temp = MakeView(a_list).tempCount;
		const auto pos = a_viewPos >= temp ? a_viewPos - temp : 0;

		std::vector<RE::TESForm*> old(a_list->forms.begin(), a_list->forms.end());
		a_list->forms.clear();
		for (std::size_t i = 0; i < pos && i < old.size(); ++i) {
			a_list->forms.push_back(old[i]);
		}
		for (auto* form : a_add) {
			a_list->forms.push_back(form);
		}
		for (std::size_t i = pos; i < old.size(); ++i) {
			a_list->forms.push_back(old[i]);
		}
	}

	void PatchGroup(const PlanterGroup& a_group)
	{
		auto*                              registry = PlantRegistry::GetSingleton();
		std::unordered_set<RE::TESForm*> used;

		// A) Repair: give every existing item a plant at its own index.
		{
			const auto items = MakeView(a_group.items);
			for (auto* floraList : a_group.floraLists) {
				const auto flora = MakeView(floraList);
				if (flora.view.size() >= items.view.size()) {
					continue;
				}
				std::vector<RE::TESForm*> fill;
				for (std::size_t i = flora.view.size(); i < items.view.size(); ++i) {
					auto* item = items.view[i];
					RE::TESForm* plant = item ? PickUniquePlant(item->GetFormID(), a_group, used) : nullptr;
					if (!plant && item) {
						// Nothing unique: reuse any plant for it so the indices still line up.
						if (const auto* v = registry->Variants(item->GetFormID()); v && !v->empty()) {
							plant = v->front();
						}
					}
					if (!plant) {
						plant = flora.view.empty() ? nullptr : flora.view.front();
					}
					fill.push_back(plant);
					if (plant) {
						used.insert(plant);
					}
					spdlog::info("    repair [{}] {} -> {}", i, NameOf(item), NameOf(plant));
				}
				InsertAt(floraList, flora.view.size(), fill);
				spdlog::info("  Repaired {}: added {} missing plants", Describe(floraList), fill.size());
			}
		}

		// A2) Existing entries whose plant can't grow in soil (hanging decorations that spawn
		//     underground): swap in a proper plant at the same index.
		{
			const auto items = MakeView(a_group.items);
			for (auto* floraList : a_group.floraLists) {
				const auto flora = MakeView(floraList);
				const auto n = (std::min)(items.view.size(), flora.view.size());
				for (std::size_t i = flora.tempCount; i < n; ++i) {
					auto* item = items.view[i];
					auto* plant = flora.view[i] ? flora.view[i]->As<RE::TESBoundObject>() : nullptr;
					if (!item || !plant || !IsPlantForm(plant) || SoilPlants::IsGoodSoilPlant(plant)) {
						continue;
					}
					auto* better = PickUniquePlant(item->GetFormID(), a_group, used);
					if (!better) {
						continue;
					}
					floraList->forms[static_cast<std::uint32_t>(i - flora.tempCount)] = better;
					used.insert(better);
					spdlog::info("    fix [{}] {}: {} -> {}", i, NameOf(item), NameOf(plant), NameOf(better));
				}
			}
		}

		// B) Add every missing plantable at the same index in every list.
		const auto                pos = MakeView(a_group.items).view.size();
		std::vector<RE::TESForm*> addItems, addPlants;
		auto allItems = registry->SortedItems();
		allItems.insert(allItems.end(), SoilPlants::ExtraItems().begin(), SoilPlants::ExtraItems().end());
		for (const auto itemID : allItems) {
			auto* item = RE::TESForm::LookupByID(itemID);
			if (!item || a_group.items->HasForm(item)) {
				continue;
			}
			auto* plant = PickUniquePlant(itemID, a_group, used);
			if (!plant) {
				spdlog::info("    skip {}: no free plant for it", NameOf(item));
				continue;
			}
			used.insert(plant);
			addItems.push_back(item);
			addPlants.push_back(plant);
		}

		if (addItems.empty()) {
			spdlog::info("  Nothing new to add");
		} else {
			InsertAt(a_group.items, pos, addItems);
			for (auto* f : a_group.floraLists) {
				InsertAt(f, pos, addPlants);
			}
			for (std::size_t i = 0; i < addItems.size(); ++i) {
				spdlog::info("    + [{}] {} -> {}", pos + i, NameOf(addItems[i]), NameOf(addPlants[i]));
			}
			spdlog::info("  Added {} new plantables at index {}", addItems.size(), pos);
		}

		// C) Verify: every item needs a plant at its index that grows in soil.
		const auto items = MakeView(a_group.items);
		for (auto* f : a_group.floraLists) {
			const auto  flora = MakeView(f);
			std::size_t bad = 0;
			for (std::size_t i = 0; i < items.view.size(); ++i) {
				auto* plant = i < flora.view.size() ? flora.view[i] : nullptr;
				auto* bound = plant ? plant->As<RE::TESBoundObject>() : nullptr;
				if (!plant || (IsPlantForm(plant) && !SoilPlants::IsGoodSoilPlant(bound))) {
					if (bad++ < 20) {
						spdlog::warn("    won't grow [{}] {} -> {}", i, NameOf(items.view[i]), NameOf(plant));
					}
				}
			}
			spdlog::info("  Check {}: {} items / {} plants, {} without a plant that grows in soil",
				Describe(f), items.view.size(), flora.view.size(), bad);
		}
	}
}

void SoilPatcher::Run()
{
	if (!Config::patchFertileSoil) {
		return;
	}

	auto groups = FindFromScripts();
	if (groups.empty()) {
		spdlog::warn("No planter scripts found in the VM - falling back to matching FormLists");
		groups = FindByMatching();
	}
	if (groups.empty()) {
		spdlog::warn("No fertile-soil planter lists found");
		return;
	}

	const auto skipped = Config::ResolveSkippedPlanterLists();
	for (const auto& group : groups) {
		if (skipped.contains(group.items->GetFormID())) {
			spdlog::info("Skipping {} (sSkipPlanterLists)", Describe(group.items));
			continue;
		}
		spdlog::info("Patching items {}", Describe(group.items));
		PatchGroup(group);
	}
}
