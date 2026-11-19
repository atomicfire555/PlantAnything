#pragma once

// Decides which plant an item grows into in fertile soil.
//
// Many items only exist on decorative flora (garlic braids, dried herb bundles, food displays)
// whose mesh hangs below its origin, so in a soil plot it spawns underground. For those - and
// for ingredients no plant gives at all - PlantAnything.esp provides placeholder FLOR records
// that are filled in at startup with a real crop mesh, the item's name and the item as produce.
namespace SoilPlants
{
	void Init();  // kDataLoaded, after PlantRegistry::Build()

	[[nodiscard]] bool IsGoodSoilPlant(RE::TESBoundObject* a_plant);
	[[nodiscard]] bool IsPlaceholder(const RE::TESForm* a_form);
	[[nodiscard]] RE::TESBoundObject* Placeholder(RE::FormID a_item);  // nullptr if none assigned

	// Ingredients with no plant anywhere that were given a placeholder.
	[[nodiscard]] const std::vector<RE::FormID>& ExtraItems();
}
