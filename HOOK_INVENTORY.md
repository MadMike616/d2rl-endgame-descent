# Hook Inventory — Endgame Descent

## Plugin metadata

| Field | Value |
|---|---|
| Plugin name | Endgame Descent |
| Plugin ID | endgame-descent |
| Release version | 1.2.3 |
| Inventory updated | 2026-11-27 |
| Source revision | v1.2.3 release source |
| D2R build(s) audited | 3.4.95032 through D2RLoader 1.3.1-beta |
| D2RLoader SDK / Plugin ABI | SDK 0.4.0 / ABI 4 |
| Audit status | Complete |

## Scope and method

Inspected src/plugin.cpp, src/boss_room.cpp, src/dungeon_layout.cpp, src/dungeon_modifiers.cpp, src/dungeon_loot.cpp, src/dungeon_entrance.cpp and the loaded game's native automap, maze generation, preset initialization, monster population, pack modifiers, monster drops, item property, player relocation, client selection, and scene lighting paths. The sidecar read and write routines index a 400-byte table by layer ID without a bounds check. The game's layer switch clears four trees on the outgoing layer and its teardown frees the full list of layers.

## Hook sites

| Site ID | Type | Target | Address / identifier | Operation | Guard / validation | Source reference | Notes |
|---|---|---|---|---|---|---|---|
| H-01 | Inline | Native automap sidecar read | RVA 0xD5710 | Skip disk reads for layer IDs outside 0–99 | Exact entry bytes including current-layer RIP-relative reference and exact index instruction bytes | src/plugin.cpp / ReadSidecar | In-game automap layer selection uses the game's normal path. |
| H-02 | Inline | Native automap sidecar write | RVA 0xD5FE0 | Skip disk writes for layer IDs outside 0–99 | Exact entry bytes and exact header-layer index instruction bytes | src/plugin.cpp / WriteSidecar | The layer-transition caller cleans up its trees and temporary buffers. |
| H-03 | Inline | Native automap tree clear | RVA 0xD4D60 | Retain outgoing dungeon layer trees across an area change | Exact entry, outgoing layer switch and layer teardown bytes. Matches the four switch return addresses and the mod's installed Descent layer range | src/plugin.cpp / ClearTree | Game teardown retains ownership and frees all layers. |
| H-04 | Inline | Native DRLG static room initialization | RVA 0x3F38D0 | Append a checkpoint boss encounter and its accompanying packs to each section's far stair preset | Exact entry, caller, allocator and layout witness bytes. Validated RotW monster rows, Hell, checkpoint area ID, matching map preset and room bounds | src/boss_room.cpp / InitializeRoom | The Council uses six distinct boss descriptors and one pack. The game's room and unit lifecycles own the descriptors. |
| H-05 | Inline | Native DRLG maze positioning | RVA 0x327880 | Place a Crypt down stair, remove the unused ice side branch, and select the lava stair presets | Exact positioning entry and maze return address, special room setter and Crypt rule bytes, validated LevelDefs and LvlPrest rows, Hell and dungeon area/type | src/dungeon_layout.cpp / PositionMaze | Runs before the game fixes room coordinates and allocates preset maps. |
| H-06 | Inline | Native DRLG maze generation | RVA 0x3E69E0 | Initialize the floor's RNG with a seed derived from the current offline game and floor | Exact generator entry, caller, seed initialization, difficulty store and native single-player predicate bytes. Validated LevelDefs and LvlPrest rows, offline gameplay lifecycle, RotW bank, Hell and dungeon area/type | src/dungeon_layout.cpp / GenerateMaze | Client and local server receive the same floor seed. The campaign DRLG seed is outside the write. |
| H-07 | Inline | Native treasure-class drop dispatch | RVA 0x441300 | Run the native drop, then roll a chance at one extra pick on eligible Descent kills | Exact entry and death caller, bank, difficulty, room, seed and treasure row bytes. Validated RotW level and treasure rows, offline Hell, monster source and dungeon area. Optional items also require validated treasure-entry bytes and an item-table match. | src/dungeon_loot.cpp / Drop | The native downstream drop routine remains available to Random Treasure Drop. Checkpoint boss IDs use their configured boss rewards. |
| H-08 | Inline | Native item property application | RVA 0x3D3670 | Bias the minimum in a copy of the selected superior item property row | Exact entry, superior caller, row stride, property offsets and item seed bytes. Validated QualityItems row size and scoped Descent monster drop | src/dungeon_loot.cpp / ApplyProperties | The original property application owns the item's stat writes. |
| H-09 | Inline | Native room monster population | RVA 0x50ACF0 | Apply the floor's 100–150% density roll to its server monster region before population | Exact entry, caller, region lookup, room and area layout, density read and threshold, difficulty and bank bytes. Validated RotW Levels and LevelDefs rows, offline Hell, Descent area and matching region | src/dungeon_layout.cpp / PopulateMonsters | Uses the per-game Descent seed and the configured floor density. Hell Density hooks the earlier initializer at RVA 0x508750 and excludes custom areas. |
| H-10 | Inline | Native server unit relocation | RVA 0x491CA0 | Require offline RotW Hell and the player's Baal quest reward flag when entering a Descent area | Exact entry, stair caller, room level, level definition, player data, quest record, game difficulty and bank, and single-player predicate bytes. Destination must have the Descent layer and quest flag. | src/dungeon_entrance.cpp / Relocate | The native mover owns the area transition. Floor 1's up stair returns to Harrogath. |
| H-11 | Inline | Native client unit selection | RVA 0xF2B40 | Hide the locked Harrogath Descent warp from hover selection and publish the unlocked door's selection state | Exact entry, warp branch, room accessor, data bank, client DRLG difficulty, and quest-record bytes. Requires Harrogath and the configured Descent warp. | src/dungeon_entrance.cpp / SetSelection | The client uses its synchronized quest record. Game exit clears the hover state. |
| H-12 | Inline | Native scene light preparation | RVA 0xE17F00 | Apply the door light color and power for the lighting pass when the unlocked door is selected | Exact entry, render registry, point-light storage and stride, and color writer bytes. Requires one dormant light matching the town preset's radius and attenuation. | src/dungeon_entrance.cpp / PrepareLights | The light comes from the HD town preset. The hook restores its dormant values after native light preparation. |
| H-13 | Inline | Native ordinary pack spawn | RVA 0x50B7E0 | Roll for reinforcements and a leader modifier after spawning the ordinary pack | Exact entry, pack size, population caller, native seed update, member placement, and modifier helper bytes. Requires validated Descent tables, offline RotW Hell, and an ordinary population call with a valid pack size. | src/dungeon_layout.cpp / SpawnPack | Uses native follower placement, affix initialization, and aura skill selection. |
| H-14 | Inline | Native unit display-name lookup | RVA 0x9A1B0 | Supply localized display names for checkpoint bosses | Exact entry, HUD caller, monster type and class reads, unit bank, room and level paths, and offline predicate bytes. Requires a Descent session seed and a checkpoint class ID resolved from MonStats for that floor. | src/boss_room.cpp / UnitName | All other name lookups use the original function. |
| H-15 | Inline | Native preset monster spawn | RVA 0x50A050 | Assign the checkpoint encounter's level 5 aura after native monster creation | Exact entry, preset caller, room and level layout, difficulty and bank reads, and native aura helper bytes. Requires offline RotW Hell and a matching checkpoint aura bearer class ID for the floor. | src/dungeon_layout.cpp / SpawnPresetMonster | Includes preset stair rooms that suppress ordinary monster population. The game uses the selected skill's native radius. One Council member is eligible. |
| H-16 | Inline | Native server room activation | RVA 0x4042A0 | Restore a checkpoint monster's rolled level 5 aura after room activation if it lost the skill | Exact entry, preset and inactive unit calls, room unit links, and the checkpoint aura guards above. Requires a living aura bearer on its Descent floor. | src/dungeon_layout.cpp / ActivateServerRoom | Uses the same aura and modifier slot as the initial spawn. |

## Known collision overlaps

None identified in the current inventory; this is not a compatibility guarantee.

| Other plugin | Shared target | Relationship | Evidence / notes |
|---|---|---|---|
| None | N/A | N/A | Random Treasure Drop installs at the downstream RVA 0x4404F0. MapSense research references native automap and room initialization. Its active source installs hooks at other entry points. |

## Audit notes

The sidecar guards install in one SDK transaction. Other hooks use separate transactions. Compared with release 1.0.0, release 1.2.2 adds H-14, H-15, and H-16 for checkpoint names, initial checkpoint auras, and aura restoration. The other hook sites are unchanged. The new entry points and supporting layout witnesses were validated against the audited build. In-game checks verified level 5 Defiance reaching Council members and guards, persistence after returning from town, and Conviction during normal monster AI operation. Disabled monster AI can prevent aura execution.

The runtime resolves the installed area, layer, lava preset, and town warp IDs from the mod's files. In-memory automap retention uses the installed Descent layer range. Other game builds have not been validated; hooks fail closed when their expected instructions or layouts do not match.

Release 1.2.3 source review: TOML installation completion and configuration mirroring add no native hook, patch, vtable, or OS input-hook targets. Hook-site changes since the previous release: none. Startup completion was checked in the testing environment; the existing native validation limits above still apply.
