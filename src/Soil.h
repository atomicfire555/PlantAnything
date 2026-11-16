#pragma once

// Hearthfire-style fertile soil works off two parallel FormLists:
//   flPlanterPlantableItem[i]  (what you can put in the soil)
//   flPlanterPlantedFlora[i]   (the plant that grows from it)
// Homestead mods either reuse those lists or duplicate them, and some (e.g. CACO)
// add more parallel flora lists. SoilPatcher finds every such list group in the
// load order by matching produce, then appends every plantable item + its plant.
namespace SoilPatcher
{
	void Run();
}
