#include "dungeon_layout.h"
#include "dungeon_modifiers.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <bcrypt.h>
#include <intrin.h>
#include <mutex>

namespace {

constexpr std::uintptr_t GenerateMazeRva = 0x3E69E0;
constexpr std::uintptr_t GenerateMazeReturnRva = 0x32722F;
constexpr std::uintptr_t InitLowSeedRva = 0x367110;
constexpr std::uintptr_t IsSinglePlayerRva = 0x8D4B0;
constexpr std::uintptr_t PositionMazeRva = 0x327880;
constexpr std::uintptr_t MazePositionReturnRva = 0x3E75D4;
constexpr std::uintptr_t SetSpecialRoomRva = 0x3EB850;
constexpr std::uintptr_t CryptStairRulesRva = 0x1D0F070;
constexpr std::uintptr_t PopulateMonstersRva = 0x50ACF0;
constexpr std::uintptr_t SpawnPackRva = 0x50B7E0;
constexpr std::uintptr_t PopulatePackReturnRva = 0x50B114;
constexpr std::uintptr_t SpawnPackMemberRva = 0x492BF0;
constexpr std::uintptr_t SpawnPresetMonsterRva = 0x50A050;
constexpr std::uintptr_t PresetMonsterReturnRva = 0x48DED2;
constexpr std::uintptr_t ActivateServerRoomRva = 0x4042A0;
constexpr std::uint32_t MazeLevelType = 1;
constexpr std::uint32_t CryptTiles = 4;
constexpr std::uint32_t IceTiles = 33;
constexpr std::uint32_t LavaTiles = 35;
constexpr std::uint32_t FirstLavaRoom = 1053;
constexpr std::size_t LevelsRowSize = 0x18C;
constexpr std::size_t HellDensityOffset = 0x24 + sizeof(std::int32_t) * Hell;
constexpr std::size_t RegionDensityOffset = 0x2B8;
constexpr std::size_t PopulationSeedLowOffset = 0x1A8;
constexpr std::size_t PopulationSeedHighOffset = 0x1AC;
constexpr std::int64_t MaxMonsterDensity = 10000;
constexpr std::uint32_t ReinforcementRollSize = 100000;
constexpr std::uint32_t FirstFloorReinforcementChance = 1000;
constexpr std::uint32_t LastFloorReinforcementChance = 6000;
constexpr std::uint8_t MaxEligiblePackSize = 6;

struct SpecialRoomRule {
    std::int32_t originalDef;
    std::int32_t replacementDef;
    std::int32_t fileIndex;
    std::int32_t direction;
};

// The stock game uses this array for the four orientations of a Tower Cellar down stair.
constexpr SpecialRoomRule CryptStairs[]{
    {116, 146, -1, 3}, {110, 144, -1, 0}, {112, 145, -1, 1}, {109, 143, -1, 2},
};
// Replace the unused side passage with an ordinary room with the same orientation.
constexpr std::uint32_t IceSideRoomBase[]{1003, 1004, 1006, 1010};

using PositionMazeFn = void(__fastcall*)(void* level) noexcept;
using SetSpecialRoomFn = void(__fastcall*)(void* level, const SpecialRoomRule* rule, std::int32_t* nextIndex) noexcept;
using GenerateMazeFn = void(__fastcall*)(std::uint8_t bank, void* level) noexcept;
using InitLowSeedFn = void(__fastcall*)(void* seed, std::uint32_t low) noexcept;
using IsSinglePlayerFn = bool(__fastcall*)() noexcept;
using PopulateMonstersFn = void(__fastcall*)(void* game, void* room) noexcept;
using SpawnPackFn = void*(__fastcall*)(void* game, void* room, void* placement, int monsterId,
                                      std::uint8_t minimum, std::uint8_t maximum) noexcept;
using SpawnPackMemberFn = void*(__fastcall*)(void* game, void* placement, void* leader, int monsterId,
                                            int mode, int distance, std::uint16_t flags) noexcept;
using SpawnPresetMonsterFn = void*(__fastcall*)(void* game, void* room, int monsterId, int x, int y,
                                                int mode, std::uint16_t flags) noexcept;

const D2RL::PluginContext* pluginContext = nullptr;
const D2RL::DataTableService* tables = nullptr;
SetSpecialRoomFn setSpecialRoom = nullptr;
InitLowSeedFn initLowSeed = nullptr;
IsSinglePlayerFn isSinglePlayer = nullptr;
std::atomic<PositionMazeFn> originalPositionMaze = nullptr;
std::atomic<GenerateMazeFn> originalGenerateMaze = nullptr;
std::atomic<PopulateMonstersFn> originalPopulateMonsters = nullptr;
std::atomic<SpawnPackFn> originalSpawnPack = nullptr;
std::atomic<SpawnPresetMonsterFn> originalSpawnPresetMonster = nullptr;
std::atomic<PopulateMonstersFn> originalActivateServerRoom = nullptr;
SpawnPackMemberFn spawnPackMember = nullptr;
std::atomic_bool dungeonTablesReady = false;
std::array<std::atomic<std::int32_t>, DungeonFloorCount> baseMonsterDensities{};

// Client and local server maze generation must use the same seed.
std::atomic<std::uint32_t> dungeonSessionSeed = 0;
std::mutex sessionSeedMutex;
std::uint32_t previousDungeonSeed = 0;

std::uint32_t MazeSeed(std::uint32_t sessionSeed, std::uint32_t floor) noexcept {
    return MixDungeonSeed(sessionSeed ^ (floor * 0x9E3779B9U));
}

std::uint32_t EnsureDungeonSessionSeed() noexcept {
    // Harrogath can preload floor 1 before LocalPlayerReady. Initialize the
    // shared seed before the first maze rather than during a UI event.
    std::lock_guard lock(sessionSeedMutex);
    if (const auto current = DungeonSessionSeed()) {
        return current;
    }
    std::uint32_t seed{};
    if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&seed), sizeof(seed), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        pluginContext->LogWarn("Could not obtain a random Descent seed. Using the next session seed.");
        seed = 0;
    }
    if (seed == 0 || seed == previousDungeonSeed) {
        seed = previousDungeonSeed + 1;
        if (seed == 0) {
            seed = 1;
        }
    }
    previousDungeonSeed = seed;
    dungeonSessionSeed.store(seed, std::memory_order_release);
    return seed;
}

void __cdecl OnGameLeft(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
    std::lock_guard lock(sessionSeedMutex);
    dungeonSessionSeed.store(0, std::memory_order_release);
}

void __fastcall GenerateMaze(std::uint8_t bank, void* level) noexcept {
    const auto original = originalGenerateMaze.load(std::memory_order_acquire);
    if (original == nullptr) {
        return;
    }
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - pluginContext->exeBase;
    if (caller == GenerateMazeReturnRva && level != nullptr
        && bank == static_cast<std::uint8_t>(D2RL::DataTables::Bank::Rotw)
        && dungeonTablesReady.load(std::memory_order_acquire) && ReadField<std::uint32_t>(level, 0) == MazeLevelType) {
        const auto floor = dungeonPlacement.Floor(ReadField<std::uint32_t>(level, 0x1F8));
        const auto* drlg = ReadField<const void*>(level, 0x1C8);
        if (floor != 0 && drlg != nullptr
            && ReadField<std::uint8_t>(drlg, 0x830) == Hell && isSinglePlayer()) {
            const auto seed = MazeSeed(EnsureDungeonSessionSeed(), floor);
            initLowSeed(static_cast<std::byte*>(level) + 0x1E4, seed);
        }
    }
    original(bank, level);
}

template <typename Value>
void WriteField(void* object, std::size_t offset, Value value) noexcept {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof(value));
}

std::uint32_t DensityPercent(std::uint32_t sessionSeed, std::uint32_t floor) noexcept {
    // Salt the input so the floor's maze and density use different hash values.
    return 100 + MazeSeed(sessionSeed ^ 0xD4E12C77U, floor) % 51;
}

const void* OfflineHellDungeonRoom(const void* game, const void* room) noexcept {
    if (game == nullptr || room == nullptr
        || !dungeonTablesReady.load(std::memory_order_acquire)
        || ReadField<std::uint8_t>(game, 0x104) != Hell
        || ReadField<std::uint8_t>(game, 0x106) != static_cast<std::uint8_t>(D2RL::DataTables::Bank::Rotw)
        || !isSinglePlayer()) {
        return nullptr;
    }
    return ReadField<const void*>(room, 0x18);
}

std::uint32_t DescentRoomFloor(const void* drlgRoom) noexcept {
    if (drlgRoom == nullptr) return 0;
    const auto* level = ReadField<const void*>(drlgRoom, 0x90);
    if (level == nullptr) {
        return 0;
    }
    return dungeonPlacement.Floor(ReadField<std::uint32_t>(level, 0x1F8));
}

std::uint32_t DescentPopulationFloor(const void* game, const void* room) noexcept {
    const auto* drlgRoom = OfflineHellDungeonRoom(game, room);
    if (drlgRoom == nullptr || (ReadField<std::uint32_t>(drlgRoom, 0x50) & 0x800000) != 0) return 0;
    return DescentRoomFloor(drlgRoom);
}

void ApplyMonsterDensity(void* game, const void* room) noexcept {
    const auto seed = DungeonSessionSeed();
    const auto floor = seed == 0 ? 0 : DescentPopulationFloor(game, room);
    if (floor == 0) {
        return;
    }
    const auto area = dungeonPlacement.firstArea + floor - 1;
    const auto index = floor - 1;
    const auto baseline = baseMonsterDensities[index].load(std::memory_order_acquire);
    const auto* regions = ReadField<const void*>(game, 0x1D0);
    if (baseline <= 0 || regions == nullptr) {
        return;
    }
    auto* region = ReadField<void*>(regions, area * sizeof(void*));
    if (region == nullptr || ReadField<std::uint32_t>(region, 0x2C0) != area) {
        return;
    }
    const auto percent = DensityPercent(seed, floor);
    const auto density = static_cast<std::int32_t>(
        std::min(MaxMonsterDensity, (std::int64_t{baseline} * percent + 50) / 100));
    const auto current = ReadField<std::int32_t>(region, RegionDensityOffset);
    // Each native game owns its regions. Derive the value from the table baseline
    // on every visit, so the same floor cannot multiply its own density twice.
    if (current != baseline && current != density) {
        return;
    }
    if (current != density) {
        WriteField(region, RegionDensityOffset, density);
    }
}

void __fastcall PopulateMonsters(void* game, void* room) noexcept {
    if (const auto original = originalPopulateMonsters.load(std::memory_order_acquire)) {
        ApplyMonsterDensity(game, room);
        original(game, room);
    }
}

constexpr std::uint32_t ReinforcementChance(std::uint32_t floor) noexcept {
    return FirstFloorReinforcementChance
        + (LastFloorReinforcementChance - FirstFloorReinforcementChance) * (floor - 1) / (DungeonFloorCount - 1);
}

std::uint32_t PackSeed(std::uint32_t sessionSeed, const void* game, int monsterId) noexcept {
    // The native population loop advances its seed for each candidate before calling the pack spawner.
    return sessionSeed ^ ReadField<std::uint32_t>(game, PopulationSeedLowOffset)
        ^ MazeSeed(ReadField<std::uint32_t>(game, PopulationSeedHighOffset), static_cast<std::uint32_t>(monsterId));
}

unsigned RollReinforcements(std::uint32_t packSeed, std::uint32_t floor) noexcept {
    const auto roll = MazeSeed(packSeed ^ 0x97C29B3DU, floor);
    if (roll % ReinforcementRollSize >= ReinforcementChance(floor)) {
        return 0;
    }
    return 1 + (MazeSeed(roll, 0xB52D76ADU) & 1U);
}

void* __fastcall SpawnPack(void* game, void* room, void* placement, int monsterId,
                           std::uint8_t minimum, std::uint8_t maximum) noexcept {
    const auto original = originalSpawnPack.load(std::memory_order_acquire);
    if (original == nullptr) {
        return nullptr;
    }
    const auto seed = DungeonSessionSeed();
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - pluginContext->exeBase;
    const bool eligiblePack = minimum != 0 && minimum <= maximum && maximum <= MaxEligiblePackSize && monsterId >= 0;
    const auto floor = seed != 0 && placement != nullptr && caller == PopulatePackReturnRva && eligiblePack
        ? DescentPopulationFloor(game, room) : 0;
    const auto packSeed = floor == 0 ? 0U : PackSeed(seed, game, monsterId);
    const auto extra = floor == 0 ? 0U : RollReinforcements(packSeed, floor);
    auto* leader = original(game, room, placement, monsterId, minimum, maximum);
    if (leader != nullptr && extra != 0) {
        for (unsigned i = 0; i < extra; ++i) {
            // Same collision-aware helper and arguments as the native ordinary pack loop.
            spawnPackMember(game, placement, leader, monsterId, 1, 3, 0x10);
        }
    }
    if (floor != 0) {
        ApplyPackModifier(leader, floor, monsterId, packSeed);
    }
    return leader;
}

void* __fastcall SpawnPresetMonster(void* game, void* room, int monsterId, int x, int y,
                                   int mode, std::uint16_t flags) noexcept {
    const auto original = originalSpawnPresetMonster.load(std::memory_order_acquire);
    if (original == nullptr) return nullptr;

    auto* monster = original(game, room, monsterId, x, y, mode, flags);
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - pluginContext->exeBase;
    if (caller == PresetMonsterReturnRva && monster != nullptr && ReadField<std::uint32_t>(monster, 0) == 1
        && ReadField<int>(monster, 4) == monsterId) {
        // Stair rooms may suppress ordinary population while still hosting preset bosses.
        const auto floor = DescentRoomFloor(OfflineHellDungeonRoom(game, room));
        if (IsCheckpointAuraBearer(floor, static_cast<std::uint32_t>(monsterId))) {
            ApplyCheckpointAura(monster, floor, EnsureDungeonSessionSeed());
        }
    }
    return monster;
}

void __fastcall ActivateServerRoom(void* game, void* room) noexcept {
    const auto original = originalActivateServerRoom.load(std::memory_order_acquire);
    if (original == nullptr) return;
    original(game, room);

    const auto floor = DescentRoomFloor(OfflineHellDungeonRoom(game, room));
    if (floor == 0 || floor % DungeonSectionSize != 0) return;
    // Preset creation and restoration of inactive units both finish before this pass.
    auto* unit = ReadField<void*>(room, 0xA8);
    for (unsigned count = 0; unit != nullptr && count < 512; ++count) {
        if (ReadField<std::uint32_t>(unit, 0) == 1) {
            const auto mode = ReadField<std::uint32_t>(unit, 0x0C);
            if (mode != 0 && mode != 12 && IsCheckpointAuraBearer(floor, ReadField<std::uint32_t>(unit, 4))) {
                ApplyCheckpointAura(unit, floor, EnsureDungeonSessionSeed());
            }
        }
        unit = ReadField<void*>(unit, 0x160);
    }
}

bool HasRoomPreset(const void* level, std::uint32_t firstDef, std::uint32_t lastDef) noexcept {
    const auto* room = ReadField<const void*>(level, 0x10);
    for (std::uint32_t count = 0; room != nullptr && count < 128; ++count) {
        const auto* preset = ReadField<const void*>(room, 0x40);
        if (preset != nullptr) {
            const auto def = ReadField<std::uint32_t>(preset, 0);
            if (firstDef <= def && def <= lastDef) {
                return true;
            }
        }
        room = ReadField<const void*>(room, 0x48);
    }
    return false;
}

void AddCryptStair(void* level, std::uint32_t floor) noexcept {
    if (HasRoomPreset(level, 143, 146)) {
        return;
    }
    auto direction = static_cast<std::int32_t>(ReadField<std::uint32_t>(level, 0x1E4) & 3);
    for (unsigned attempt = 0; attempt < std::size(CryptStairs); ++attempt) {
        const auto& rule = CryptStairs[direction];
        setSpecialRoom(level, &rule, &direction);
        if (HasRoomPreset(level, 143, 146)) {
            return;
        }
    }
    D2RL::LogWarnF(pluginContext, "Could not place a down stair on Descent floor %u.", floor);
}

void ApplyStairPresets(void* level, std::uint32_t floor, std::uint32_t tiles) noexcept {
    auto* room = ReadField<void*>(level, 0x10);
    unsigned lavaStairs = 0;
    for (std::uint32_t count = 0; room != nullptr && count < 128; ++count) {
        auto* preset = ReadField<void*>(room, 0x40);
        if (preset != nullptr && ReadField<std::uint32_t>(room, 0x74) == PresetRoomType) {
            const auto def = ReadField<std::uint32_t>(preset, 0);
            if (tiles == IceTiles && def >= 1026 && def <= 1029) {
                WriteField(preset, 0, IceSideRoomBase[def - 1026]);
                WriteField(preset, 4, std::int32_t{-1});
            } else if (tiles == LavaTiles && def >= FirstLavaRoom && def < FirstLavaRoom + 4
                && ReadField<std::uint32_t>(room, 0x68) == 24 && ReadField<std::uint32_t>(room, 0x6C) == 24) {
                WriteField(preset, 0, dungeonPlacement.firstLavaPreset + def - FirstLavaRoom);
                WriteField(preset, 4, std::int32_t{0});
                ++lavaStairs;
            }
        }
        room = ReadField<void*>(room, 0x48);
    }
    if (tiles == LavaTiles && lavaStairs != 2) {
        D2RL::LogWarnF(pluginContext, "Descent floor %u generated %u lava stair rooms.", floor, lavaStairs);
    }
}

void __fastcall PositionMaze(void* level) noexcept {
    const auto original = originalPositionMaze.load(std::memory_order_acquire);
    if (original == nullptr) {
        return;
    }
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - pluginContext->exeBase;
    if (level != nullptr && caller == MazePositionReturnRva && dungeonTablesReady.load(std::memory_order_acquire)
        && ReadField<std::uint32_t>(level, 0) == MazeLevelType) {
        const auto floor = dungeonPlacement.Floor(ReadField<std::uint32_t>(level, 0x1F8));
        const auto tiles = ReadField<std::uint32_t>(level, 0x1E0);
        const auto* drlg = ReadField<const void*>(level, 0x1C8);
        if (floor != 0 && drlg != nullptr
            && ReadField<std::uint8_t>(drlg, 0x830) == Hell) {
            if (floor >= 21 && floor <= 30 && tiles == CryptTiles) {
                AddCryptStair(level, floor);
            } else if ((floor >= 41 && floor <= 50 && tiles == IceTiles) || (floor >= 51 && tiles == LavaTiles)) {
                ApplyStairPresets(level, floor, tiles);
            }
        }
    }
    original(level);
}

bool ValidateDungeonTables(const D2RL::PluginContext* context) noexcept {
    using namespace D2RL::DataTables;
    TableView levels{};
    levels.structSize = TableViewSize;
    if (tables->getTable(context, Bank::Rotw, TableId::LevelDefs, &levels) != Result::Success
        || !HasTableViewField(&levels, TableViewRequiredSize) || !VerifyDungeonLevels(levels)) {
        return false;
    }
    const auto firstPreset = dungeonPlacement.firstLavaPreset;
    TableView presets{};
    presets.structSize = TableViewSize;
    if (tables->getTable(context, Bank::Rotw, TableId::LvlPrest, &presets) != Result::Success
        || !HasTableViewField(&presets, TableViewRequiredSize) || presets.rowSize != 0x1AC
        || presets.rows == nullptr || presets.rowCount < firstPreset + 4) {
        return false;
    }
    for (std::uint32_t def = firstPreset; def < firstPreset + 4; ++def) {
        const auto* row = static_cast<const std::byte*>(presets.rows) + def * presets.rowSize;
        if (ReadField<std::uint32_t>(row, 0) != def || ReadField<std::uint32_t>(row, 0x3C) != 1) {
            return false;
        }
    }
    return true;
}

void __cdecl OnLayoutTablesLoaded(const D2RL::PluginContext* context,
                                  const D2RL::Lifecycle::DataTablesLoadedEvent*, void*) noexcept {
    using namespace D2RL::DataTables;
    const bool valid = ValidateDungeonTables(context);
    if (originalPopulateMonsters.load(std::memory_order_acquire) != nullptr) {
        TableView levels{};
        levels.structSize = TableViewSize;
        const bool densityReady = valid
            && tables->getTable(context, Bank::Rotw, TableId::Levels, &levels) == Result::Success
            && HasTableViewField(&levels, TableViewRequiredSize) && levels.rowSize == LevelsRowSize
            && levels.rows != nullptr && levels.rowCount >= dungeonPlacement.firstArea + DungeonFloorCount;
        for (std::uint32_t index = 0; index < DungeonFloorCount; ++index) {
            const auto baseline = densityReady
                ? ReadField<std::int32_t>(levels.rows, (dungeonPlacement.firstArea + index) * LevelsRowSize + HellDensityOffset)
                : 0;
            baseMonsterDensities[index].store(baseline, std::memory_order_release);
        }
        if (!densityReady) {
            context->LogWarn("Descent density unavailable. Could not verify the monster density table.");
        }
    }
    dungeonTablesReady.store(valid, std::memory_order_release);
    if (!valid) {
        context->LogWarn("Dungeon stairs unavailable. Could not verify the level and preset tables.");
    }
}

constexpr std::uint8_t PositionEntry[]{
    0x48, 0x89, 0x4C, 0x24, 0x08, 0x55, 0x53, 0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC,
    0x58, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x15,
};
constexpr std::uint8_t MazeCaller[]{
    0x8B, 0x83, 0xF8, 0x01, 0x00, 0x00, 0x83, 0xF8, 0x1C, 0x74, 0x1D, 0x83, 0xF8, 0x6B,
    0x74, 0x0A, 0x48, 0x8B, 0xCB, 0xE8, 0xAC, 0x02, 0xF4, 0xFF, 0xEB, 0x1A,
};
constexpr std::uint8_t SpecialRoomEntry[]{
    0x41, 0x56, 0x48, 0x83, 0xEC, 0x30, 0x44, 0x8B, 0x0A, 0x4D, 0x8B, 0xF0, 0x48, 0x89,
    0x5C, 0x24, 0x40, 0x4C, 0x8B, 0xD2, 0x48, 0x8B, 0x59, 0x10, 0x48, 0x89, 0x6C, 0x24,
    0x48, 0x48, 0x8B, 0xCB, 0x8B, 0x6A, 0x08,
};
constexpr std::uint8_t RoomPresetWrite[]{
    0x49, 0x83, 0xC8, 0x02, 0x44, 0x89, 0x3A, 0x4C, 0x89, 0x42, 0x10, 0x89, 0x6A, 0x04,
};
constexpr std::uint8_t GenerateMazeEntry[]{
    0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x55, 0x41, 0x56, 0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC,
    0x28, 0x48, 0x8B, 0xDA, 0x44, 0x0F, 0xB6, 0xE9, 0x48, 0x8B, 0xCB, 0xBA, 0x02, 0x00, 0x00,
    0x00, 0xE8, 0x3C, 0xA7, 0xF7, 0xFF,
};
constexpr std::uint8_t LevelSeedSetup[]{
    0x48, 0x8B, 0x82, 0xC8, 0x01, 0x00, 0x00, 0x4C, 0x8B, 0xF2, 0x44, 0x0F, 0xB6, 0xF9,
    0x8B, 0x90, 0x60, 0x08, 0x00, 0x00, 0x49, 0x8D, 0x8E, 0xE4, 0x01, 0x00, 0x00, 0x41, 0x03,
    0x96, 0xF8, 0x01, 0x00, 0x00, 0xE8, 0x1B, 0xFF, 0x03, 0x00,
};
constexpr std::uint8_t GenerateMazeCaller[]{0x49, 0x8B, 0xD6, 0x41, 0x0F, 0xB6, 0xCF, 0xE8, 0xB1, 0xF7, 0x0B, 0x00};
constexpr std::uint8_t InitLowSeedEntry[]{0x89, 0x11, 0xC7, 0x41, 0x04, 0x9A, 0x02, 0x00, 0x00, 0xC3};
constexpr std::uint8_t IsSinglePlayerEntry[]{0x83, 0x3D, 0x3D, 0x5F, 0x99, 0x02, 0x00, 0x0F, 0x94, 0xC0, 0xC3};
constexpr std::uint8_t DifficultyStore[]{
    0x0F, 0xB6, 0x85, 0x38, 0x08, 0x00, 0x00, 0x88, 0x87, 0x30, 0x08, 0x00, 0x00,
};
constexpr std::uint8_t PopulateMonstersEntry[]{
    0x4C, 0x8B, 0xDC, 0x53, 0x56, 0x48, 0x81, 0xEC, 0xB8, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0x05, 0xC5, 0x05, 0x4C, 0x02, 0x48, 0x33, 0xC4,
};
constexpr std::uint8_t PopulationCaller[]{0x48, 0x8B, 0xD3, 0x48, 0x8B, 0xCF, 0xE8, 0x03, 0x6A, 0x10, 0x00};
constexpr std::uint8_t PopulationRegion[]{
    0x8B, 0xD0, 0x48, 0x8B, 0x8B, 0xD0, 0x01, 0x00, 0x00, 0xE8, 0x38, 0xD9, 0xFF, 0xFF,
    0x48, 0x8B, 0xE8, 0x48, 0x89, 0x44, 0x24, 0x68, 0x8B, 0x88, 0xB8, 0x02, 0x00, 0x00,
    0xB8, 0x10, 0x27, 0x00, 0x00, 0x3B, 0xC8, 0x0F, 0x4F, 0xC8,
};
constexpr std::uint8_t RegionLookup[]{0x48, 0x63, 0xC2, 0x48, 0x8B, 0x04, 0xC1, 0xC3};
constexpr std::uint8_t RoomLookup[]{
    0x48, 0x85, 0xC9, 0x75, 0x03, 0x33, 0xC0, 0xC3, 0x48, 0x8B, 0x49, 0x18, 0xE9, 0xDF, 0x12, 0x07, 0x00,
};
constexpr std::uint8_t RoomArea[]{
    0xF7, 0x43, 0x50, 0x00, 0x00, 0x80, 0x00, 0x74, 0x08, 0x33, 0xC0, 0x48, 0x83, 0xC4,
    0x20, 0x5B, 0xC3, 0x48, 0x8B, 0x83, 0x90, 0x00, 0x00, 0x00, 0x8B, 0x80, 0xF8, 0x01, 0x00, 0x00,
};
constexpr std::uint8_t DensityLoad[]{
    0x41, 0x89, 0xB6, 0xC0, 0x02, 0x00, 0x00, 0x8B, 0x4C, 0x90, 0x24, 0x41, 0x89, 0x8E, 0xB8, 0x02, 0x00, 0x00,
};
constexpr std::uint8_t PopulationDifficulty[]{0x41, 0x0F, 0xB6, 0x8E, 0x04, 0x01, 0x00, 0x00};
constexpr std::uint8_t PopulationBank[]{0x0F, 0xB6, 0x8B, 0x06, 0x01, 0x00, 0x00};
constexpr std::uint8_t LevelsStride[]{0x48, 0x69, 0x44, 0x24, 0x60, 0x8C, 0x01, 0x00, 0x00, 0x48, 0x03, 0x07};
constexpr std::uint8_t DensityCheck[]{0x3B, 0x8D, 0xB8, 0x02, 0x00, 0x00, 0x0F, 0x8F, 0x1C, 0x02, 0x00, 0x00};
constexpr std::uint8_t SpawnPackEntry[]{
    0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x5B, 0x08, 0x49, 0x89, 0x6B, 0x10, 0x49, 0x89, 0x73, 0x18,
};
constexpr std::uint8_t PackCaller[]{
    0x44, 0x88, 0x7C, 0x24, 0x28, 0x44, 0x8B, 0xCF, 0x4D, 0x8B, 0xC5, 0x44, 0x88, 0x74,
    0x24, 0x20, 0x48, 0x8B, 0xD6, 0x48, 0x8B, 0xCB, 0xE8, 0xCC, 0x06, 0x00, 0x00,
};
constexpr std::uint8_t PackSizeRead[]{
    0x0F, 0xB6, 0x9C, 0x24, 0xB8, 0x00, 0x00, 0x00, 0x84, 0xDB, 0x0F, 0x84, 0xB5, 0x01,
    0x00, 0x00, 0x40, 0x3A, 0xDF,
};
constexpr std::uint8_t PackMemberCaller[]{
    0xBF, 0x10, 0x00, 0x00, 0x00, 0x0F, 0x1F, 0x40, 0x00, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x66, 0x89, 0x7C, 0x24, 0x30, 0x45, 0x8B, 0xCE, 0xC7, 0x44, 0x24, 0x28, 0x03, 0x00,
    0x00, 0x00, 0x4C, 0x8B, 0xC6, 0x49, 0x8B, 0xD7, 0x44, 0x89, 0x64, 0x24, 0x20,
    0x48, 0x8B, 0xCD, 0xE8, 0x1D, 0x72, 0xF8, 0xFF,
};
constexpr std::uint8_t SpawnPackMemberEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18,
    0x57, 0x48, 0x83, 0xEC, 0x60, 0x8B, 0x84, 0x24, 0x90, 0x00, 0x00, 0x00,
};
constexpr std::uint8_t PopulationSeedUpdate[]{
    0x8B, 0x83, 0xA8, 0x01, 0x00, 0x00, 0x48, 0x69, 0xC8, 0xC5, 0x90, 0xC6, 0x6A, 0x8B, 0x83,
    0xAC, 0x01, 0x00, 0x00, 0x48, 0x03, 0xC8, 0x48, 0x8B, 0xC1, 0x89, 0x8B, 0xA8, 0x01, 0x00,
    0x00, 0x48, 0xC1, 0xE8, 0x20, 0x89, 0x83, 0xAC, 0x01, 0x00, 0x00,
};

constexpr std::uint8_t SpawnPresetMonsterEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18,
    0x44, 0x89, 0x4C, 0x24, 0x20,
};
constexpr std::uint8_t SpawnPresetMonsterCaller[]{
    0x66, 0x89, 0x4C, 0x24, 0x30, 0x45, 0x8B, 0xCE, 0x44, 0x89, 0x64, 0x24, 0x28,
    0x48, 0x8B, 0xCE, 0x44, 0x8B, 0xC7, 0x44, 0x89, 0x7C, 0x24, 0x20, 0xE8, 0x7E, 0xC1, 0x07, 0x00,
};
constexpr std::uint8_t ActivateServerRoomEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA, 0x48, 0x8B, 0xF9,
};
constexpr std::uint8_t ActivateRoomPresetsAndInactive[]{
    0x48, 0x8B, 0xD3, 0x48, 0x8B, 0xCF, 0xE8, 0xC4, 0xAE, 0x08, 0x00,
    0x48, 0x8B, 0xD3, 0x48, 0x8B, 0xCF, 0xE8, 0xB9, 0xF4, 0x0F, 0x00,
};
constexpr std::uint8_t ActivateRoomInactiveOnly[]{
    0x48, 0x8B, 0xD3, 0x48, 0x8B, 0xCF, 0xE8, 0x7D, 0xF4, 0x0F, 0x00,
};
constexpr std::uint8_t RoomFirstUnit[]{0x48, 0x8B, 0x81, 0xA8, 0x00, 0x00, 0x00};
constexpr std::uint8_t RoomNextUnit[]{0x48, 0x8B, 0x81, 0x60, 0x01, 0x00, 0x00};

bool VerifyPopulationLayout(const D2RL::PluginContext* context) noexcept {
    return isSinglePlayer != nullptr
        && context->CheckExpectedBytes(0x2EFD00, RoomLookup, sizeof(RoomLookup))
        && context->CheckExpectedBytes(0x361011, RoomArea, sizeof(RoomArea))
        && context->CheckExpectedBytes(0x447D55, PopulationDifficulty, sizeof(PopulationDifficulty))
        && context->CheckExpectedBytes(0x50AD7D, PopulationBank, sizeof(PopulationBank));
}

bool InstallCheckpointAuras(const D2RL::PluginContext* context, const D2RL::MutationService* mutations) noexcept {
    if (!VerifyPopulationLayout(context)
        || !context->CheckExpectedBytes(PresetMonsterReturnRva - sizeof(SpawnPresetMonsterCaller),
                                        SpawnPresetMonsterCaller, sizeof(SpawnPresetMonsterCaller))
        || !EnableMonsterAuraSkills(context)) {
        context->LogWarn("Descent checkpoint auras unavailable. Could not verify the preset monster or aura helpers.");
        return true;
    }
    const auto status = InstallHook(context, mutations, SpawnPresetMonsterRva, SpawnPresetMonsterEntry,
                                    SpawnPresetMonster, originalSpawnPresetMonster);
    if (status != HookStatus::Installed) {
        context->LogWarn("Could not install the Descent checkpoint aura hook.");
        return status != HookStatus::MissingOriginal;
    }
    context->LogInfo("Enabled one level 5 aura per Descent checkpoint encounter.");

    if (!context->CheckExpectedBytes(0x4042C1, ActivateRoomPresetsAndInactive, sizeof(ActivateRoomPresetsAndInactive))
        || !context->CheckExpectedBytes(0x404308, ActivateRoomInactiveOnly, sizeof(ActivateRoomInactiveOnly))
        || !context->CheckExpectedBytes(0x3E225E, RoomFirstUnit, sizeof(RoomFirstUnit))
        || !context->CheckExpectedBytes(0x34B4CE, RoomNextUnit, sizeof(RoomNextUnit))) {
        context->LogWarn("Descent checkpoint aura restoration unavailable. Could not verify the active room layout.");
        return true;
    }
    const auto restoreStatus = InstallHook(context, mutations, ActivateServerRoomRva, ActivateServerRoomEntry,
                                           ActivateServerRoom, originalActivateServerRoom);
    if (restoreStatus != HookStatus::Installed) context->LogWarn("Could not install checkpoint aura restoration.");
    return restoreStatus != HookStatus::MissingOriginal;
}

bool InstallPackReinforcements(const D2RL::PluginContext* context, const D2RL::MutationService* mutations) noexcept {
    if (originalPopulateMonsters.load(std::memory_order_acquire) == nullptr
        || !context->CheckExpectedBytes(PopulatePackReturnRva - sizeof(PackCaller), PackCaller, sizeof(PackCaller))
        || !context->CheckExpectedBytes(0x50B819, PackSizeRead, sizeof(PackSizeRead))
        || !context->CheckExpectedBytes(0x50B99F, PackMemberCaller, sizeof(PackMemberCaller))
        || !context->CheckExpectedBytes(SpawnPackMemberRva, SpawnPackMemberEntry, sizeof(SpawnPackMemberEntry))
        || !context->CheckExpectedBytes(0x50AEC0, PopulationSeedUpdate, sizeof(PopulationSeedUpdate))) {
        context->LogWarn("Descent pack reinforcements unavailable. Could not verify the ordinary pack spawn path.");
        return true;
    }
    spawnPackMember = reinterpret_cast<SpawnPackMemberFn>(context->exeBase + SpawnPackMemberRva);
    const auto status = InstallHook(context, mutations, SpawnPackRva, SpawnPackEntry, SpawnPack, originalSpawnPack);
    if (status == HookStatus::Installed) {
        D2RL::LogInfoF(context, "Enabled Descent pack reinforcements at %g%% on floor 1 to %g%% on floor %u.",
                      FirstFloorReinforcementChance * 100.0 / ReinforcementRollSize,
                      LastFloorReinforcementChance * 100.0 / ReinforcementRollSize, DungeonFloorCount);
        EnablePackModifiers(context);
    } else {
        context->LogWarn("Could not install the Descent pack reinforcement hook.");
    }
    return status != HookStatus::MissingOriginal;
}

bool InstallDensity(const D2RL::PluginContext* context, const D2RL::MutationService* mutations) noexcept {
    if (originalGenerateMaze.load(std::memory_order_acquire) == nullptr
        || !VerifyPopulationLayout(context)
        || !context->CheckExpectedBytes(0x4042E2, PopulationCaller, sizeof(PopulationCaller))
        || !context->CheckExpectedBytes(0x50ADFA, PopulationRegion, sizeof(PopulationRegion))
        || !context->CheckExpectedBytes(0x508740, RegionLookup, sizeof(RegionLookup))
        || !context->CheckExpectedBytes(0x508916, DensityLoad, sizeof(DensityLoad))
        || !context->CheckExpectedBytes(0x32C56E, LevelsStride, sizeof(LevelsStride))
        || !context->CheckExpectedBytes(0x50AF03, DensityCheck, sizeof(DensityCheck))) {
        context->LogWarn("Descent density unavailable. Could not verify the monster population path or game seed.");
        return true;
    }
    const auto status = InstallHook(context, mutations, PopulateMonstersRva, PopulateMonstersEntry,
                                    PopulateMonsters, originalPopulateMonsters);
    if (status == HookStatus::Installed) {
        context->LogInfo("Descent monster density rolls enabled for offline Hell.");
    } else {
        context->LogWarn("Could not install the Descent monster density hook.");
    }
    return status != HookStatus::MissingOriginal;
}

bool InstallMazeReroll(const D2RL::PluginContext* context, const D2RL::MutationService* mutations,
                       const D2RL::LifecycleService* lifecycle) noexcept {
    if (!context->CheckExpectedBytes(0x3271CE, LevelSeedSetup, sizeof(LevelSeedSetup))
        || !context->CheckExpectedBytes(GenerateMazeReturnRva - sizeof(GenerateMazeCaller), GenerateMazeCaller,
                                        sizeof(GenerateMazeCaller))
        || !context->CheckExpectedBytes(InitLowSeedRva, InitLowSeedEntry, sizeof(InitLowSeedEntry))
        || !context->CheckExpectedBytes(IsSinglePlayerRva, IsSinglePlayerEntry, sizeof(IsSinglePlayerEntry))
        || !context->CheckExpectedBytes(0x326ED0, DifficultyStore, sizeof(DifficultyStore))) {
        context->LogWarn("Descent map rerolls unavailable. The native maze seed or session mode could not be verified.");
        return true;
    }
    constexpr auto unregisterEnd = offsetof(D2RL::LifecycleService, unregisterGameplayEventListener);
    if (!D2RL::HasLifecycleServiceField(lifecycle, unregisterEnd) || !lifecycle->registerGameplayEventListener
        || !lifecycle->unregisterGameplayEventListener) {
        context->LogWarn("Descent map rerolls require gameplay lifecycle events.");
        return true;
    }
    initLowSeed = reinterpret_cast<InitLowSeedFn>(context->exeBase + InitLowSeedRva);
    isSinglePlayer = reinterpret_cast<IsSinglePlayerFn>(context->exeBase + IsSinglePlayerRva);
    using namespace D2RL::Lifecycle;
    const GameplayEventListener listener{
        .structSize = GameplayEventListenerSize, .flags = 0, .kind = GameplayEventKind::GameLeft,
        .reserved = 0, .callback = OnGameLeft, .userData = nullptr,
    };
    ListenerHandle leftHandle{};
    if (lifecycle->registerGameplayEventListener(context, &listener, &leftHandle) != Result::Success) {
        context->LogWarn("Descent map rerolls could not subscribe to game exit.");
        return true;
    }
    const auto status = InstallHook(context, mutations, GenerateMazeRva, GenerateMazeEntry, GenerateMaze, originalGenerateMaze);
    if (status != HookStatus::Installed) {
        lifecycle->unregisterGameplayEventListener(context, leftHandle);
        context->LogWarn("Could not install the Descent map reroll hook.");
        return status != HookStatus::MissingOriginal;
    }
    context->LogInfo("Descent map rerolls enabled for offline games.");
    return true;
}

}  // namespace

std::uint32_t DungeonSessionSeed() noexcept {
    return dungeonSessionSeed.load(std::memory_order_acquire);
}

bool InstallDungeonLayout(const D2RL::PluginContext* context, const D2RL::MutationService* mutations) noexcept {
    if (!context->CheckExpectedBytes(0x3E75BC, MazeCaller, sizeof(MazeCaller))
        || !context->CheckExpectedBytes(SetSpecialRoomRva, SpecialRoomEntry, sizeof(SpecialRoomEntry))
        || !context->CheckExpectedBytes(0x3EB8F1, RoomPresetWrite, sizeof(RoomPresetWrite))
        || !context->CheckExpectedBytes(CryptStairRulesRva, CryptStairs, sizeof(CryptStairs))) {
        context->LogWarn("Dungeon stairs unavailable. The native maze layout could not be verified.");
        return true;
    }
    const D2RL::LifecycleService* lifecycle = nullptr;
    constexpr auto registerEnd = offsetof(D2RL::LifecycleService, unregisterDataTablesLoadedListener);
    constexpr auto getTableEnd = offsetof(D2RL::DataTableService, getRow);
    if (context->QueryService(&lifecycle) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasLifecycleServiceField(lifecycle, registerEnd) || !lifecycle->registerDataTablesLoadedListener
        || context->QueryService(&tables) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasDataTableServiceField(tables, getTableEnd) || !tables->getTable) {
        context->LogWarn("Dungeon stairs unavailable. Lifecycle and data table services are required.");
        return true;
    }
    pluginContext = context;
    setSpecialRoom = reinterpret_cast<SetSpecialRoomFn>(context->exeBase + SetSpecialRoomRva);
    const auto status = InstallHook(context, mutations, PositionMazeRva, PositionEntry, PositionMaze, originalPositionMaze);
    if (status != HookStatus::Installed) {
        context->LogError("Could not install the dungeon stair placement hook.");
        return status != HookStatus::MissingOriginal;
    }
    const D2RL::Lifecycle::DataTablesLoadedListener listener{
        .structSize = D2RL::Lifecycle::DataTablesLoadedListenerSize, .flags = 0,
        .callback = OnLayoutTablesLoaded, .userData = nullptr,
    };
    D2RL::Lifecycle::ListenerHandle handle{};
    if (lifecycle->registerDataTablesLoadedListener(context, &listener, &handle) != D2RL::Lifecycle::Result::Success) {
        context->LogWarn("Dungeon stairs unavailable. Could not register for data table loads.");
        return true;
    }
    return InstallMazeReroll(context, mutations, lifecycle) && InstallDensity(context, mutations)
        && InstallPackReinforcements(context, mutations) && InstallCheckpointAuras(context, mutations);
}
