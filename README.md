# Endgame Descent

Endgame Descent adds a 60-floor dungeon to offline Reign of the Warlock. The dungeon starts at area level 85 and rises one area level per floor to 144. Dungeon automaps stay in memory across area transitions and start fresh in a new game.

Monster level scaling includes damage, life, attack rating, defense, and experience through level 147. Above level 110, the installer extends the existing table using the growth between levels 99 and 109 and preserves any higher levels the mod already defines.

In offline Reign of the Warlock Hell games, the wooden door by Harrogath's main gate leads directly to floor 1 once the character has completed the Hell Baal quest. Hover over the unlocked door to see its light and floor label. Both cues stay hidden while the door is locked. The first floor's up stairs return to the same door.

The Descent uses six tileset groups and connects its floors through native stair transitions. Each ten-floor checkpoint rolls one of 17 encounters. The 16 solo bosses each have three names and two accompanying packs. For a Chaos Sanctuary seal boss, one pack uses that boss's original escort family and table count. The Council encounter spawns all six Act III members with one local pack and averages four checkpoint reward drops at players 1. The dungeon's data tables are required for stair placement, encounters, and maze rerolls in Reign of the Warlock on Hell difficulty.

Each offline game rolls a Descent seed for the dungeon mazes and checkpoint boss pools. It is shared by the local server and client and stays fixed through return trips within the game. Campaign map seeds and Map Seed Selector settings are independent of the Descent seed.

Each Descent floor independently rolls a whole-number monster density from 100% to 150% of its configured Hell density. The roll stays fixed for that floor until the next offline game. The multiplier applies to normal monster population. Champion and unique pack limits and scripted checkpoint encounters use their configured values.

Each eligible ordinary pack can roll one or two extra monsters of its own type. The chance starts at 1% on floor 1 and rises evenly to 6% on floor 60. Reinforcements use the standard monster placement rules in the room.

Ordinary pack leaders get an independent 1–6% modifier roll with the same floor progression. A successful roll chooses one affix two-thirds of the time or one level 5 aura one-third of the time. Choices within each pool are equally weighted.

- Affixes include Extra Fast, Magic Resistant, Spectral Hit, and Stone Skin.
- The pack leader emits one aura. Might, Resist Fire, Defiance, Blessed Aim, and Vigor buff allied monsters within range. Holy Freeze, Holy Fire, and Holy Shock affect enemies within range.

Each checkpoint encounter rolls one level 5 aura from the pack aura pool plus Fanaticism and Conviction. The game uses the selected aura's normal level 5 range and restores the same aura if the boss loses it during room reactivation. In the Council encounter, one member emits the aura for the group. Allied monsters receive beneficial auras within range; damaging auras and Conviction affect enemies.

Ordinary monster kills in offline Hell Descent can roll one extra item. The chance is 1.56% on floor 1 and increases by about 0.10 percentage points per floor to 7.32% on floor 60. One-third of successful rolls use the full table. The other two-thirds roll exceptional and elite equipment, boosted gear or jewelry, and Flawless and Perfect Gems. The full table also offers the dedicated Lem–Zod branch. Superior items from these kills gain a floor-based chance to roll each eligible stat from the upper half of its existing range.

The ten-floor groups offer the Western, Eastern, Southern, Deep, and Northern Worldstone Shards in that order. Floors 51–60 can roll any of the five. Corrupted Shards can drop throughout the dungeon if the active mod defines them. The Keys of Terror, Hate, and Destruction can drop on floors 1–10, 21–30, and 51–60, respectively. Within the full table, the weights are 30 in 1,024 for Worldstone Shards, 20 in 1,024 for Corrupted Shards, and 20 in 1,024 for the eligible key. The DLL checks each extra item against the loaded item table and uses the standard treasure class when an extra item is unavailable. Checkpoint bosses use their own reward tables.

## Install

Download the [latest release](../../releases/latest) and follow the [installation guide](INSTALL.md) to install into a new or existing compatible mod. It includes the full input file list, exact destinations, the first-launch log, and rollback steps.

The merge adds rows to `levels.txt`, `lvlmaze.txt`, `lvlprest.txt`, `lvlwarp.txt`, `monstats.txt`, `monlvl.txt`, and `treasureclassex.txt`. It adds strings to `ui.json`, model aliases to `monsters.json`, and the door transition and light to the Harrogath map. It also generates four lava stair presets and creates an empty expansion DS1 cache override so the loose town preset loads. The installer reads `armor.txt`, `weapons.txt`, and `misc.txt` to build loot pools. The extracted base files go in `mods/<mod>/d2rloader/plugins/basefiles-and-backups/basefiles/`. Setup backs up edited originals under `mods/<mod>/d2rloader/plugins/basefiles-and-backups/backups/endgame-descent/<install>/` with their original paths. `created-files.txt` lists new files.

Setup appends the dungeon rows after the mod's existing rows and assigns available area, automap, preset, and warp IDs. It connects the Harrogath door through a free transition slot. The stair logic relies on the corresponding stock maze and town geometry. If another mod uses the same physical doorway or fills all eight Harrogath transition slots, setup logs the conflict. A complete existing Descent overlay keeps its IDs when the DLL is updated.

TXT rows are matched to the mod's columns by exact header name, including reordered headers. New rows use `MadMike` in an existing `*Creator` column. Setup reports missing required columns before writing files.

## Build

From the Visual Studio x64 Native Tools Command Prompt, run `build-release.cmd "<PluginSDK root>"` with [D2RLoader PluginSDK 0.4.0](https://github.com/D2RLoader/PluginSDK). The script puts the Release DLL in the sibling `.build/endgame-descent-1.2.3` directory. The DLL embeds `src/assets/install-data.json`. To regenerate it, run `tools/generate-install-data.py` with the extracted stock data directory, the matching extracted Reign of the Warlock `global/excel` directory, the output JSON path, and a staging directory outside the source tree.

The native hooks verify the expected instructions during loading. The audited setup used D2R 3.4.95032 with D2RLoader 1.3.1-beta. See `HOOK_INVENTORY.md` for the hook sites.

## License

Endgame Descent is available under the [MIT License](LICENSE). See [third-party notices](THIRD_PARTY_NOTICES.md) for bundled and build-time dependencies.


## Installation completion

After successful first-run setup, the plugin records a completion token in its TOML configuration. Later launches skip that completed setup and preserve existing files and settings. Failed setup remains retryable.

The completion token does not replace the installed dungeon files. If they are removed, restore them or reset the token to rerun setup.

To rerun setup intentionally, close the game and set `complete = false` in the relevant token section (`[installation]` by default).

Installable release ZIPs include the plugin documentation and applicable SDK and JSON-library licenses.
