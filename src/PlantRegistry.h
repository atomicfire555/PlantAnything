#pragma once

// Maps "item you can drop" -> "plant base objects that produce it".
// Built once at kDataLoaded from every FLOR / TREE record in the load order,
// so vanilla, DLC, Creation Club and any other mod's plants are picked up automatically.
class PlantRegistry
{
public:
	static PlantRegistry* GetSingleton()
	{
		static PlantRegistry singleton;
		return &singleton;
	}

	void Build();

	[[nodiscard]] bool                IsPlantable(RE::FormID a_item) const { return _map.contains(a_item); }
	[[nodiscard]] RE::TESBoundObject* PickPlant(RE::FormID a_item) const;
	[[nodiscard]] RE::TESBoundObject* PickPlantForSoil(RE::FormID a_item) const;  // prefers small FLOR over TREE
	[[nodiscard]] std::vector<RE::FormID> SortedItems() const;

	// Does this flora/tree base produce a_item (directly or through leveled lists)?
	static bool Produces(RE::TESForm* a_plant, RE::FormID a_item);
	static bool IsHarvestItem(RE::TESForm* a_form);  // ingredient or food/potion

private:
	void Consider(RE::TESBoundObject* a_plant, RE::TESProduceForm* a_produce, const std::unordered_set<RE::FormID>& a_excluded);

	std::unordered_map<RE::FormID, std::vector<RE::TESBoundObject*>> _map;
};
