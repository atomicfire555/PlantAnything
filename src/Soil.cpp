#include "Soil.h"
#include "Config.h"
#include "PlantRegistry.h"

// Papyrus sees a FormList in this order (CK wiki, "AddForm - FormList"):
//   [script-added forms created at runtime (0xFF..)] [forms defined in plugins] [other script-added forms]
// Script-added forms are restored from the save, so the lists can only be lined up
// AFTER a save has loaded. We insert our entries into the plugin-defined part of every
// parallel list at the SAME position (the smallest plugin-defined size in the group), so
// everything after that point - including other mods' script-added entries - shifts by the
// same amount in every list and stays paired.

namespace
{
	struct ListView
	{
		RE::BGSListForm*          list{ nullptr };
		std::vector<RE::TESForm*> view;  // what Papyrus GetAt() sees, index for index
		std::uint32_t             staticCount{ 0 };
		std::uint32_t             addedCount{ 0 };
		bool                      hasRuntimeForms{ false };
	};

	struct PlanterGroup
	{
		ListView              items;
		std::vector<ListView> floraLists;
	};

	ListView MakeView(RE::BGSListForm* a_list)
	{
		ListView v;
		v.list = a_list;
		v.staticCount = a_list->forms.size();

		std::vector<RE::TESForm*> runtimeAdded, normalAdded;
		if (a_list->scriptAddedTempForms) {
			for (const auto id : *a_list->scriptAddedTempForms) {
				auto* form = RE::TESForm::LookupByID(id);
				if ((id >> 24) == 0xFF) {
					runtimeAdded.push_back(form);
				} else {
					normalAdded.push_back(form);
				}
			}
		}
		v.addedCount = static_cast<std::uint32_t>(runtimeAdded.size() + normalAdded.size());
		v.hasRuntimeForms = !runtimeAdded.empty();

		v.view.reserve(runtimeAdded.size() + v.staticCount + normalAdded.size());
		v.view.insert(v.view.end(), runtimeAdded.begin(), runtimeAdded.end());
		for (auto* form : a_list->forms) {
			v.view.push_back(form);  // keep nulls: they still take an index
		}
		v.view.insert(v.view.end(), normalAdded.begin(), normalAdded.end());
		return v;
	}

	std::string Describe(const ListView& a_v)
	{
		auto* file = a_v.list->GetFile(0);
		return std::format("{} [{:08X}] ({} = {} plugin + {} script-added)",
			file ? std::string(file->GetFilename()) : "<runtime>"s, a_v.list->GetFormID(),
			a_v.view.size(), a_v.staticCount, a_v.addedCount);
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
	std::size_t CountIf(const ListView& a_v, Pred a_pred)
	{
		return static_cast<std::size_t>(std::ranges::count_if(a_v.view, a_pred));
	}

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

	std::vector<PlanterGroup> FindPlanterGroups()
	{
		auto& lists = RE::TESDataHandler::GetSingleton()->GetFormArray<RE::BGSListForm>();

		std::vector<ListView> itemLists, floraLists;
		for (auto* list : lists) {
			if (!list) {
				continue;
			}
			auto v = MakeView(list);
			const auto size = v.view.size();
			if (size < 3) {
				continue;
			}
			if (CountIf(v, PlantRegistry::IsHarvestItem) * 10 >= size * 8) {
				itemLists.push_back(std::move(v));
			} else if (CountIf(v, IsPlantForm) * 10 >= size * 8) {
				floraLists.push_back(std::move(v));
			}
		}

		const auto skipped = Config::ResolveSkippedPlanterLists();

		std::vector<PlanterGroup>                         groups;
		std::unordered_map<RE::BGSListForm*, std::size_t> floraOwner;
		for (auto& items : itemLists) {
			if (skipped.contains(items.list->GetFormID())) {
				spdlog::info("Skipping planter item list {} (sSkipPlanterLists)", Describe(items));
				continue;
			}
			PlanterGroup group{ items, {} };
			for (auto& flora : floraLists) {
				const auto n = (std::min)(items.view.size(), flora.view.size());
				const auto matches = AlignedMatches(items, flora);
				if (matches < 3 || matches * 10 < n * 6) {
					continue;  // not a parallel list
				}
				if (flora.view.size() != items.view.size()) {
					spdlog::warn("Planter lists look paired but are out of step already, not touching them:");
					spdlog::warn("    items {}", Describe(items));
					spdlog::warn("    flora {}", Describe(flora));
					continue;
				}
				group.floraLists.push_back(flora);
				++floraOwner[flora.list];
			}
			if (!group.floraLists.empty()) {
				groups.push_back(std::move(group));
			}
		}

		for (auto& group : groups) {
			std::erase_if(group.floraLists, [&](const ListView& f) {
				if (floraOwner[f.list] > 1) {
					spdlog::warn("Flora list {} is paired with more than one item list - skipped", Describe(f));
					return true;
				}
				return false;
			});
		}
		std::erase_if(groups, [](const PlanterGroup& g) { return g.floraLists.empty(); });

		// Runtime-created forms sit in front of the plugin part and would break index math.
		std::erase_if(groups, [](const PlanterGroup& g) {
			const bool runtime = g.items.hasRuntimeForms ||
			                     std::ranges::any_of(g.floraLists, [](const ListView& f) { return f.hasRuntimeForms; });
			if (runtime) {
				spdlog::warn("Planter list {} holds runtime-created forms - skipped", Describe(g.items));
			}
			return runtime;
		});
		return groups;
	}

	void InsertAt(RE::BGSListForm* a_list, std::uint32_t a_pos, const std::vector<RE::TESForm*>& a_add)
	{
		std::vector<RE::TESForm*> old(a_list->forms.begin(), a_list->forms.end());
		a_list->forms.clear();
		a_list->forms.reserve(static_cast<std::uint32_t>(old.size() + a_add.size()));
		for (std::uint32_t i = 0; i < a_pos && i < old.size(); ++i) {
			a_list->forms.push_back(old[i]);
		}
		for (auto* form : a_add) {
			a_list->forms.push_back(form);
		}
		for (std::size_t i = a_pos; i < old.size(); ++i) {
			a_list->forms.push_back(old[i]);
		}
	}

	std::string NameAt(const ListView& a_v, std::size_t a_i)
	{
		auto* f = a_i < a_v.view.size() ? a_v.view[a_i] : nullptr;
		return f ? std::format("{} [{:08X}]", f->GetName(), f->GetFormID()) : "None"s;
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
		for (const auto& flora : group.floraLists) {
			spdlog::info("          -> flora {}", Describe(flora));
		}

		// Insert point: inside the plugin-defined part of EVERY list in the group.
		std::uint32_t pos = group.items.staticCount;
		for (const auto& flora : group.floraLists) {
			pos = (std::min)(pos, flora.staticCount);
		}

		// Entries after the insert point (other mods' additions) - logged so pairing can be checked.
		for (std::size_t i = pos; i < group.items.view.size(); ++i) {
			std::string line = std::format("    existing [{}] {}", i, NameAt(group.items, i));
			for (const auto& flora : group.floraLists) {
				line += " -> " + NameAt(flora, i);
			}
			spdlog::info("{}", line);
		}

		std::vector<RE::TESForm*>              addItems;
		std::vector<std::vector<RE::TESForm*>> addFlora(group.floraLists.size());
		for (const auto itemID : allItems) {
			auto* item = RE::TESForm::LookupByID(itemID);
			if (!item || group.items.list->HasForm(item)) {
				continue;  // already plantable here (by plugin or by script)
			}
			auto* plant = registry->PickPlantForSoil(itemID);
			if (!plant) {
				continue;
			}
			addItems.push_back(item);
			for (auto& v : addFlora) {
				v.push_back(plant);
			}
		}

		if (addItems.empty()) {
			spdlog::info("  Nothing new to add (already patched)");
			continue;
		}

		InsertAt(group.items.list, pos, addItems);
		for (std::size_t f = 0; f < group.floraLists.size(); ++f) {
			InsertAt(group.floraLists[f].list, pos, addFlora[f]);
		}

		for (std::size_t i = 0; i < addItems.size(); ++i) {
			spdlog::info("    + [{}] {} [{:08X}] -> plant {:08X}", pos + i, addItems[i]->GetName(),
				addItems[i]->GetFormID(), addFlora.front()[i]->GetFormID());
		}
		spdlog::info("  Inserted {} new plantables at index {}", addItems.size(), pos);

		// Verify every list in the group still lines up.
		const auto after = MakeView(group.items.list);
		for (const auto& flora : group.floraLists) {
			const auto fv = MakeView(flora.list);
			spdlog::info("  Check: items {} / flora {} -> {}", after.view.size(), fv.view.size(),
				after.view.size() == fv.view.size() ? "in step" : "OUT OF STEP");
		}
	}
}
