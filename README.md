# Plant Anything (Skyrim SE / AE SKSE plugin)

Plant every ingredient and crop that grows on a plant in Hearthfire fertile
soil and in homestead mods that use the same planters. That covers vanilla,
Dawnguard, Dragonborn, every Creation Club plugin, and any other mod. There's
no ESP and no patch; everything happens when the game loads.

## How it works
Hearthfire's fertile soil uses two matching FormLists, paired by position:
`flPlanterPlantableItem[i]` holds what you can put in the soil, and
`flPlanterPlantedFlora[i]` holds what grows from it. Homestead mods either
reuse these lists or make copies of them, and some mods (CACO, for example)
add more flora lists that follow the same order.

When the game finishes loading, the plugin:
1. Scans every Flora and Tree record in the load order and records which
   ingredient or food each one gives (including through leveled lists).
2. Finds **every** planter list group in the load order. An item list counts
   as part of a group when another list holds, in the same positions, plants
   that give those items. This finds the vanilla Hearthfire lists and any copies
   made by homestead mods, with no hard-coded FormIDs.
3. Adds each missing item to the item list, and its plant to every matching
   flora list at the same position, so the lists stay in step.

The additions are made in memory and aren't written to your save. They're
redone each time the game starts, so removing the mod is safe. Anything
already in the soil keeps growing.

In game, nothing changes: activate fertile soil and you'll see the longer list.

## Placeholder plants (PlantAnything.esp)
Some items only exist on decorative flora: garlic and onion braids, dried
Elves Ear and Frost Mirriam bundles, food displays. Those meshes hang below
their attach point, so in a soil plot they spawn underground. Some
ingredients have no plant at all.

For these items the mod uses placeholder plants from `PlantAnything.esp`
(ESL-flagged, so it uses no load-order slot). At startup each placeholder is
given a real crop mesh (Leek for garlic and onion, Potato for roots, Imp Stool
for mushrooms, Lavender for everything else), named after the item, and set
so harvesting gives that item. The item-to-placeholder assignments are kept in
`SKSE/Plugins/PlantAnything_Slots.txt` so planted placeholders stay the same
plant across launches.

Plots already planted with a hanging decoration by an older version are
lifted out of the ground when you next see them, so you can harvest them.

## Requirements
- SKSE (SE 1.5.97 or AE)
- Address Library for SKSE Plugins
- HearthFires.esm (and whatever homestead mods you use)

## Install
Install the zip with MO2. It contains `SKSE/Plugins/PlantAnything.dll`,
`SKSE/Plugins/PlantAnything.ini` and `PlantAnything.esp`. **Enable
PlantAnything.esp** in the plugin list; load order doesn't matter. It works alongside FormList Manipulator patches,
because items that are already in a list are skipped.

## Check what happened
Look in `Documents/My Games/Skyrim Special Edition/SKSE/PlantAnything.log`:
- `Planter list:` lines show each soil list group that was found, with its
  plugin and FormID.
- `+ Item -> plant` lines show each item that was added.
- `PLANTABLE` and `NO PLANT` lines list every plant-based item in your load
  order, and every ingredient that no plant gives (animal parts, critter
  drops, items only sold by merchants). The `NO PLANT` ones can't go in soil
  because there's no plant to grow.

If a homestead's soil still doesn't take an item, its lists probably weren't
recognized. The log will say so, and you can send me those lines.

## Optional: plant anywhere
Set `bEnableDropPlanting = true` to also plant by sneaking and dropping an
item anywhere in the world. This was the original feature; the INI has its
settings.

## Build
Every push to GitHub builds automatically (`.github/workflows/build.yml`).
The **PlantAnything** artifact on the Actions run is an MO2-ready zip.

To build locally, you need VS 2022 or newer, CMake, Ninja, and vcpkg, run
from a "x64 Native Tools" prompt:
```
git clone --branch v16.5.0 --recurse-submodules https://github.com/alandtse/CommonLibVR extern/CommonLibSSE-NG
vcpkg install --triplet=x64-windows-static-md --x-manifest-root=extern/CommonLibSSE-NG --x-install-root=vcpkg_installed --x-no-default-features
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake -DVCPKG_MANIFEST_MODE=OFF -DVCPKG_TARGET_TRIPLET=x64-windows-static-md -DVCPKG_INSTALLED_DIR=%CD%/vcpkg_installed
cmake --build build
```
Set the environment variable `SKYRIM_MODS_FOLDER` to your MO2 `mods` folder
and each build is copied into `mods/PlantAnything/SKSE/Plugins`.
