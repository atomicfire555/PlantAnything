#include "Soil.h"
#include "Config.h"
#include "PlantRegistry.h"

namespace
{
	struct PlanterGroup
	{
		RE::BGSListForm*              items{ nullptr };
		std::vector<RE::BGSListForm*> floraLists;
	};

	std::string Describe(RE::BGSListForm* a_list)
	{
		auto* file = a_list->GetFile(0);
		return std::format("{} [{:08X}] ({} entries)",
			file ? std::string(file->GetFilename()) : "<runtime>"s, a_list->GetFormID(), a_list->forms.size());
	}

	bool IsPlantForm(RE::TESForm* a_form)
	{
		if (!a_form) {
			return false;
		}
		const auto type = a_form->GetFormType();
		return type == RE::FormType::Flora || type == RE::FormType::Tree;
	}

	template <class Pred>
	std::size_t CountIf(RE::BGSListForm* a_list, Pred a_pred)
	{
		std::size_t n = 0;
		for (auto* form : a_list->forms) {
			if (a_pred(form)) {
				++n;
			}
		}
		return n;
	}

	// Fraction of positions where flora[i] actually produces items[i].
	std::size_t AlignedMatches(RE::BGSListForm* a_items, RE::BGSListForm* a_flora)
	{
		const auto n = (std::min)(a_items->forms.size(), a_flora->forms.size());
		std::size_t matches = 0;
		for (decltype(n) i = 0; i < n; ++i) {
			auto* item = a_items->forms[i];
			auto* plant = a_flora->forms[i];
			if (item && plant && PlantRegistry::Produces(plant, item->GetFormID())) {
				++matches;
			}
		}
		return matches;
	}

	bool ListContains(RE::BGSListForm* a_list, RE::FormID a_id)
	{
		return std::ranges::any_of(a_list->forms, [&](RE::TESForm* f) { return f && f->GetFormID() == a_id; });
	}

	std::vector<PlanterGroup> FindPlanterGroups()
	{
		auto& lists = RE::TESDataHandler::GetSingleton()->GetFormArray<RE::BGSListForm>();

		std::vector<RE::BGSListForm*> itemLists;
		std::vector<RE::BGSListForm*> floraLists;
		for (auto* list : lists) {
			if (!list) {
				continue;
			}
			const auto size = list->forms.size();
			if (size < 3) {
				continue;
			}
			if (CountIf(list, PlantRegistry::IsHarvestItem) * 10 >= size * 8) {
				itemLists.push_back(list);
			} else if (CountIf(list, IsPlantForm) * 10 >= size * 8) {
				floraLists.push_back(list);
			}
		}

		const auto skipped = Config::ResolveSkippedPlanterLists();

		std::vector<PlanterGroup>                         groups;
		std::unordered_map<RE::BGSListForm*, std::size_t> floraOwner;  // flora list -> how many item lists claim it
		for (auto* items : itemLists) {
			if (skipped.contains(items->GetFormID())) {
				spdlog::info("Skipping planter item list {} (sSkipPlanterLists)", Describe(items));
				continue;
			}
			PlanterGroup group{ items, {} };
			for (auto* flora : floraLists) {
				const auto n = items->forms.size();
				const auto matches = AlignedMatches(items, flora);
				if (matches < 3 || matches * 10 < (std::min<std::size_t>)(n, flora->forms.size()) * 6) {
					continue;  // not a parallel list
				}
				if (flora->forms.size() != n) {
					spdlog::warn("Planter lists look paired but sizes differ, not touching them: items {} / flora {}",
						Describe(items), Describe(flora));
					continue;
				}
				group.floraLists.push_back(flora);
				++floraOwner[flora];
			}
			if (!group.floraLists.empty()) {
				groups.push_back(std::move(group));
			}
		}

		// A flora list shared by two item lists would get two appends and fall out of sync - drop those.
		for (auto& group : groups) {
			std::erase_if(group.floraLists, [&](RE::BGSListForm* f) {
				if (floraOwner[f] > 1) {
					spdlog::warn("Flora list {} is paired with more than one item list - skipped", Describe(f));
					return true;
				}
				return false;
			});
		}
		std::erase_if(groups, [](const PlanterGroup& g) { return g.floraLists.empty(); });
		return groups;
	}
}

void SoilPatcher::Run()
{
	if (!Config::patchFertileSoil) {
		return;
	}

	const auto groups = FindPlanterGroups();
	if (groups.empty()) {
		spdlog::warn("No fertile-soil planter lists found (is HearthFires.esm loaded?)");
		return;
	}

	auto*      registry = PlantRegistry::GetSingleton();
	const auto allItems = registry->SortedItems();

	for (const auto& group : groups) {
		spdlog::info("Planter list: items {}", Describe(group.items));
		for (auto* flora : group.floraLists) {
			spdlog::info("          -> flora {}", Describe(flora));
		}

		std::size_t added = 0;
		for (const auto itemID : allItems) {
			if (ListContains(group.items, itemID)) {
				continue;  // already plantable here
			}
			auto* item = RE::TESForm::LookupByID(itemID);
			auto* plant = registry->PickPlantForSoil(itemID);
			if (!item || !plant) {
				continue;
			}
			// Append to every parallel list at the same index so they stay aligned.
			group.items->forms.push_back(item);
			for (auto* flora : group.floraLists) {
				flora->forms.push_back(plant);
			}
			++added;
			spdlog::info("    + {} [{:08X}] -> plant {:08X}", item->GetName(), itemID, plant->GetFormID());
		}
		spdlog::info("  Added {} new plantables ({} total)", added, group.items->forms.size());
	}

	spdlog::info("Fertile soil patched: {} planter list group(s)", groups.size());
}
