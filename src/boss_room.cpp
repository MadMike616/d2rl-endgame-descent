#include "dungeon_layout.h"
#include "boss_names.h"
#include "checkpoint_text.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>

namespace {

using D2RL::DataTables::Bank;
using D2RL::DataTables::TableId;

constexpr std::int32_t MonsterUnitType = 1;
constexpr std::int32_t PresetMonsterMode = 1;
constexpr std::uintptr_t InitRoomRva = 0x3F38D0;
constexpr std::uintptr_t AllocatePresetUnitRva = 0x360BF0;

constexpr auto SectionSize = DungeonSectionSize;
constexpr auto SectionCount = DungeonSectionCount;
enum BossModel : std::size_t {
    Griswold, Smith, Radament, Temptress, Andariel, Duriel, Mephisto, Diablo, Baal,
    BloodRaven, Summoner, Izual, DeSeis, Infector, Vizier, Lister, SoloBossCount,
};
constexpr std::size_t CouncilSize = 6;
constexpr const char* BossModels[]{
    "griswold", "smith", "radament", "temptress", "andariel", "duriel", "mephisto", "diablo", "baal",
    "bloodraven", "summoner", "izual", "deseis", "infector", "vizier", "lister",
    "ismail", "geleb", "toorc", "bremm", "wyand", "maffer",
};
constexpr std::size_t ModelCount = std::size(BossModels);
static_assert(ModelCount == SoloBossCount + CouncilSize);
static_assert(ModelCount == DescentNames::BossCount);
constexpr auto NamesPerModel = DescentNames::RegularNameCount;
struct Escort {
    std::size_t model;
    const char* monster;
    std::size_t count;
};
// Use the original seal encounter families and the pack sizes in SuperUniques.
constexpr Escort SealEscorts[]{
    {DeSeis, "doomknight1", 5}, {Infector, "megademon3", 9}, {Vizier, "fingermage3", 9},
};
constexpr const char* GuardMonsters[SectionCount][2]{
    {"zombie3", "bighead1"}, {"goatman5", "sk_archer3"}, {"goatman3", "cr_archer3"},
    {"brute2", "cr_archer2"}, {"snowyeti2", "succubus4"}, {"bloodlord3", "fingermage4"},
};

struct SpawnPoint {
    std::int32_t x;
    std::int32_t y;
};

struct StairPreset {
    std::uint32_t def;
    std::uint32_t file;
    std::uint32_t mapSize;
    SpawnPoint boss;
    SpawnPoint packCenter;
    SpawnPoint localPack[3];
    SpawnPoint spacing{4, 4};
};

struct EncounterRoom {
    const StairPreset* stair;
    SpawnPoint roomOffset;
    std::int32_t width;
    std::int32_t height;
};

// Boss anchor, escort grid center, and local guard positions on connected floor
// tiles. Check each DS1 file separately because walls change between variants.
constexpr StairPreset CatacombsStairs[]{
    {291, 0, 12, {47, 37}, {40, 38}, {{47, 29}, {44, 31}, {44, 27}}},
    {291, 1, 12, {47, 37}, {40, 39}, {{47, 29}, {44, 31}, {44, 27}}},
    {292, 0, 12, {32, 37}, {25, 38}, {{32, 29}, {29, 31}, {29, 27}}},
    {292, 1, 12, {32, 37}, {25, 38}, {{32, 29}, {29, 31}, {29, 27}}},
    {293, 0, 12, {32, 27}, {25, 24}, {{32, 35}, {29, 33}, {29, 37}}},
    {293, 1, 12, {32, 27}, {39, 27}, {{32, 35}, {29, 33}, {29, 37}}},
    {294, 0, 12, {42, 42}, {35, 43}, {{42, 34}, {39, 36}, {39, 32}}},
    {294, 1, 12, {42, 42}, {35, 44}, {{42, 34}, {39, 36}, {39, 32}}},
};
constexpr StairPreset JailStairs[]{
    {240, 0, 12, {32, 22}, {39, 24}, {{32, 28}, {29, 28}, {30, 32}}},
    {241, 0, 12, {17, 27}, {24, 29}, {{17, 35}, {17, 32}, {19, 38}}},
    {242, 0, 12, {27, 37}, {17, 47}, {{24, 29}, {29, 33}, {29, 23}}},
    {243, 0, 12, {32, 42}, {25, 44}, {{29, 34}, {29, 37}, {29, 31}}},
};
constexpr StairPreset CryptStairs[]{
    {143, 0, 8, {27, 17}, {25, 11}, {{27, 25}, {24, 23}, {24, 27}}, {3, 3}},
    {144, 0, 8, {7, 22}, {14, 23}, {{7, 14}, {10, 16}, {10, 12}}},
    {145, 0, 8, {12, 22}, {16, 29}, {{12, 14}, {14, 17}, {13, 11}}},
    {146, 0, 8, {12, 22}, {19, 23}, {{12, 14}, {15, 16}, {14, 11}}},
};
constexpr StairPreset CaveStairs[]{
    {91, 0, 24, {67, 57}, {74, 58}, {{67, 65}, {64, 63}, {67, 62}}},
    {91, 1, 24, {62, 107}, {71, 108}, {{62, 94}, {58, 103}, {58, 95}}, {3, 4}},
    {92, 0, 24, {47, 52}, {55, 53}, {{47, 60}, {48, 57}, {50, 60}}},
    {92, 1, 24, {47, 37}, {55, 38}, {{47, 45}, {48, 42}, {49, 48}}},
    {93, 0, 24, {42, 37}, {52, 38}, {{42, 45}, {43, 42}, {44, 48}}},
    {93, 1, 24, {42, 37}, {50, 38}, {{42, 45}, {43, 42}, {44, 48}}},
    {94, 0, 24, {12, 97}, {20, 97}, {{12, 84}, {13, 94}, {15, 84}}, {4, 3}},
    {94, 1, 24, {12, 97}, {19, 101}, {{12, 84}, {14, 94}, {15, 84}}},
};
constexpr StairPreset IceStairs[]{
    {1022, 0, 16, {42, 67}, {49, 60}, {{42, 59}, {39, 61}, {39, 57}}},
    {1023, 0, 16, {62, 32}, {54, 36}, {{62, 40}, {61, 37}, {60, 43}}},
    {1024, 0, 16, {62, 7}, {69, 6}, {{62, 15}, {59, 13}, {59, 17}}},
    {1025, 0, 16, {47, 47}, {46, 54}, {{47, 39}, {44, 41}, {44, 37}}},
};
constexpr StairPreset LavaStairs[]{
    {1, 0, 24, {92, 17}, {99, 14}, {{92, 25}, {89, 23}, {89, 27}}},
    {3, 0, 24, {92, 82}, {89, 89}, {{92, 74}, {89, 76}, {89, 72}}},
};
constexpr std::span<const StairPreset> SectionStairs[]{
    CatacombsStairs, JailStairs, CryptStairs, CaveStairs, IceStairs, LavaStairs,
};
static_assert(std::size(SectionStairs) == SectionCount);

struct MonStatsRow {
    std::uint16_t classId;
    char id[48];
    std::byte remaining[0x1FC - sizeof(classId) - sizeof(id)];
};
static_assert(sizeof(MonStatsRow) == 0x1FC);

using InitRoomFn = void(__fastcall*)(std::uint8_t bank, void* room) noexcept;
using AllocatePresetUnitFn = void*(__fastcall*)(std::uint8_t bank, void* room, std::int32_t type,
    std::int32_t classId, std::int32_t mode, std::int32_t x, std::int32_t y) noexcept;

using UnitNameFn = const char*(__fastcall*)(const void* unit) noexcept;
using UnitRoomFn = const void*(__fastcall*)(const void* unit) noexcept;
using RoomLevelFn = std::uint32_t(__fastcall*)(const void* room) noexcept;
using IsSinglePlayerFn = bool(__fastcall*)() noexcept;

const D2RL::DataTableService* tables = nullptr;
AllocatePresetUnitFn allocatePresetUnit = nullptr;
std::atomic<InitRoomFn> originalInitRoom = nullptr;
std::atomic<UnitNameFn> originalUnitName = nullptr;
UnitRoomFn getNameUnitRoom = nullptr;
RoomLevelFn getNameRoomLevel = nullptr;
IsSinglePlayerFn isSinglePlayer = nullptr;
const D2RL::PluginContext* nameContext = nullptr;
const D2RL::LocalizationService* localization = nullptr;
// A model's three name variants are published together.
using BossPool = std::array<std::uint64_t, ModelCount>;
std::array<std::array<std::atomic<std::uint64_t>, ModelCount>, SectionCount> bossClassIds{};
std::array<std::atomic<std::uint32_t>, SectionCount> guardClassIds{};
std::array<std::atomic<std::uint16_t>, std::size(SealEscorts)> escortClassIds{};

std::uint16_t FindClassId(std::span<const MonStatsRow> rows, const char* expected) noexcept {
    std::uint16_t id = 0;
    for (std::size_t index = 1; index < rows.size(); ++index) {
        const auto& row = rows[index];
        if (std::strncmp(row.id, expected, sizeof(row.id)) != 0) {
            continue;
        }
        if (id != 0 || row.classId != index) {
            return 0;
        }
        id = row.classId;
    }
    return id;
}

void BossId(char (&id)[48], unsigned floor, std::size_t model, std::size_t name) noexcept {
    const auto length = floor == 10
        ? std::snprintf(id, sizeof(id), "descent_%s", BossModels[model])
        : std::snprintf(id, sizeof(id), "descent%u_%s", floor, BossModels[model]);
    if (name != 0 && length > 0 && static_cast<std::size_t>(length) < sizeof(id)) {
        std::snprintf(id + length, sizeof(id) - length, "_%zu", name + 1);
    }
}

void __cdecl OnTablesLoaded(const D2RL::PluginContext* context,
                            const D2RL::Lifecycle::DataTablesLoadedEvent*, void*) noexcept {
    using namespace D2RL::DataTables;
    for (auto& models : bossClassIds) {
        for (auto& ids : models) {
            ids.store(0, std::memory_order_release);
        }
    }
    for (auto& ids : guardClassIds) {
        ids.store(0, std::memory_order_release);
    }
    for (auto& id : escortClassIds) {
        id.store(0, std::memory_order_release);
    }
    TableView table{};
    table.structSize = TableViewSize;
    if (tables->getTable(context, Bank::Rotw, TableId::MonStats, &table) != Result::Success) {
        context->LogWarn("Dungeon bosses unavailable. Could not read the RotW monster table.");
        return;
    }
    const auto rows = Rows<MonStatsRow>(&table);
    if (rows.empty() || rows.size() > std::numeric_limits<std::uint16_t>::max()) {
        context->LogWarn("Dungeon bosses unavailable. The monster row layout could not be verified.");
        return;
    }

    for (std::size_t escort = 0; escort < std::size(SealEscorts); ++escort) {
        const auto& spec = SealEscorts[escort];
        const auto id = FindClassId(rows, spec.monster);
        escortClassIds[escort].store(id, std::memory_order_release);
        if (id == 0) {
            D2RL::LogWarnF(context, "Could not resolve the Descent seal escort %s.", spec.monster);
        }
    }
    for (std::size_t section = 0; section < SectionCount; ++section) {
        const auto floor = static_cast<unsigned>(section + 1) * SectionSize;
        for (std::size_t model = 0; model < ModelCount; ++model) {
            std::uint64_t packed = 0;
            const auto nameCount = model < SoloBossCount ? NamesPerModel : 1;
            for (std::size_t name = 0; name < nameCount; ++name) {
                char expected[48]{};
                BossId(expected, floor, model, name);
                const auto id = FindClassId(rows, expected);
                if (id != 0) {
                    packed |= std::uint64_t{id} << (name * 16);
                }
            }
            bossClassIds[section][model].store(packed, std::memory_order_release);
        }
        const auto firstGuard = FindClassId(rows, GuardMonsters[section][0]);
        const auto secondGuard = FindClassId(rows, GuardMonsters[section][1]);
        if (firstGuard != 0 && secondGuard != 0) {
            guardClassIds[section].store(firstGuard | (std::uint32_t{secondGuard} << 16), std::memory_order_release);
        } else {
            D2RL::LogWarnF(context, "Could not resolve both Descent floor %u guard packs.", floor);
        }
    }
}

std::uint16_t BossClassId(std::uint64_t packedIds, std::size_t index) noexcept {
    return static_cast<std::uint16_t>(packedIds >> (index * 16));
}

BossPool LoadBossPool(std::size_t section) noexcept {
    BossPool pool{};
    for (std::size_t model = 0; model < pool.size(); ++model) {
        pool[model] = bossClassIds[section][model].load(std::memory_order_acquire);
    }
    return pool;
}

const DescentNames::Locale& BossNameLocale(std::uint32_t sessionSeed) noexcept {
    // The first floor uses a distinct label in each translation.
    // Spanish (Spain) and Spanish (Mexico) share a label and name set.
    thread_local std::uint32_t previousSeed = 0;
    thread_local const auto* current = &DescentNames::Locales[0];
    if (previousSeed != sessionSeed) {
        previousSeed = sessionSeed;
        current = &DescentNames::Locales[0];
        char label[96]{};
        std::uint32_t requiredSize{};
        if (localization && localization->getStringByKey(nameContext, "d2r:GrailerDescentFloor01",
                label, sizeof(label), &requiredSize) == D2RL::Localization::Result::Success) {
            const auto match = std::ranges::find(DescentNames::Locales, std::string_view(label),
                                                &DescentNames::Locale::firstFloorLabel);
            if (match != std::end(DescentNames::Locales)) current = match;
        }
    }
    return *current;
}

const char* CheckpointBossName(const BossPool& pool, std::uint16_t classId, std::uint32_t floor,
                               std::uint32_t sessionSeed) noexcept {
    if (classId == 0) return nullptr;
    for (std::size_t model = 0; model < pool.size(); ++model) {
        const auto nameCount = model < SoloBossCount ? NamesPerModel : 1;
        for (std::size_t name = 0; name < nameCount; ++name) {
            if (BossClassId(pool[model], name) != classId) continue;
            const auto seed = MixDungeonSeed(sessionSeed ^ (floor * 0xE74D3C2BU)
                ^ (static_cast<std::uint32_t>(model) * 0x759C31D9U));
            const auto regular = model < SoloBossCount ? name : MixDungeonSeed(seed ^ 0xB1A89FC5U) % NamesPerModel;
            return SelectCheckpointText(seed, model, BossNameLocale(sessionSeed).names[model][regular]);
        }
    }
    return nullptr;
}

const char* __fastcall UnitName(const void* unit) noexcept {
    const auto sessionSeed = DungeonSessionSeed();
    if (sessionSeed != 0 && unit != nullptr && ReadField<std::int32_t>(unit, 0) == MonsterUnitType
        && ReadField<std::uint8_t>(unit, 0x1BD) == static_cast<std::uint8_t>(Bank::Rotw)
        && isSinglePlayer()) {
        const auto floor = dungeonPlacement.Floor(getNameRoomLevel(getNameUnitRoom(unit)));
        if (floor != 0 && floor % SectionSize == 0) {
            const auto pool = LoadBossPool(floor / SectionSize - 1);
            const auto classId = ReadField<std::uint16_t>(unit, 4);
            if (const auto* replacement = CheckpointBossName(pool, classId, floor, sessionSeed))
                return replacement;
        }
    }
    const auto original = originalUnitName.load(std::memory_order_acquire);
    return original ? original(unit) : nullptr;
}

bool ContainsBoss(std::span<const std::uint64_t> pool, std::uint32_t classId) noexcept {
    if (classId == 0) {
        return false;
    }
    for (auto packed : pool) {
        for (std::size_t name = 0; name < NamesPerModel; ++name) {
            if (BossClassId(packed, name) == classId) {
                return true;
            }
        }
    }
    return false;
}

bool FindEncounterRoom(const void* room, std::span<const StairPreset> stairs, std::uint32_t presetBase, EncounterRoom& result) noexcept {
    const auto* preset = ReadField<const void*>(room, 0x40);
    if (preset == nullptr) {
        return false;
    }
    const auto* map = ReadField<const void*>(preset, 0x08);
    if (map == nullptr) {
        return false;
    }
    const auto def = ReadField<std::uint32_t>(map, 0);
    const auto file = ReadField<std::uint32_t>(map, 4);
    for (const auto& stair : stairs) {
        if (def != presetBase + stair.def || file != stair.file) {
            continue;
        }
        if (ReadField<std::uint32_t>(map, 0x20) != stair.mapSize
            || ReadField<std::uint32_t>(map, 0x24) != stair.mapSize) {
            return false;
        }
        result = {
            &stair,
            {
                5 * (ReadField<std::int32_t>(map, 0x18) - ReadField<std::int32_t>(room, 0x60)),
                5 * (ReadField<std::int32_t>(map, 0x1C) - ReadField<std::int32_t>(room, 0x64)),
            },
            5 * ReadField<std::int32_t>(room, 0x68), 5 * ReadField<std::int32_t>(room, 0x6C),
        };
        return true;
    }
    return false;
}

bool CanAddMonster(const void* room, std::uint16_t classId, const SpawnPoint* point) noexcept {
    const auto* unit = ReadField<const void*>(room, 0x98);
    // The stock rooms have only a few preset units. Bound the scan on a corrupt list.
    for (unsigned count = 0; unit != nullptr && count < 64; ++count) {
        if (ReadField<std::int32_t>(unit, 0x20) == MonsterUnitType
            && ReadField<std::uint32_t>(unit, 0x04) == classId
            && (point == nullptr || (ReadField<std::int32_t>(unit, 8) == point->x
                && ReadField<std::int32_t>(unit, 0x24) == point->y))) {
            return false;
        }
        unit = ReadField<const void*>(unit, 0x10);
    }
    return unit == nullptr;
}

bool LocalSpawn(const EncounterRoom& room, SpawnPoint& point) noexcept {
    point.x += room.roomOffset.x;
    point.y += room.roomOffset.y;
    return point.x >= 0 && point.y >= 0 && point.x < room.width && point.y < room.height;
}

struct Encounter {
    std::uint8_t bank;
    void* room;
    EncounterRoom layout;
    bool AddMonster(std::uint16_t classId, SpawnPoint point, bool boss) const noexcept {
        // A preset map can span several native rooms. Each room owns its subtiles.
        return classId != 0 && LocalSpawn(layout, point) && CanAddMonster(room, classId, boss ? nullptr : &point)
            && allocatePresetUnit(bank, room, MonsterUnitType, classId, PresetMonsterMode, point.x, point.y);
    }

    void AddBoss(const BossPool& pool, std::size_t model, std::size_t name, SpawnPoint point) const noexcept {
        const auto classId = BossClassId(pool[model], name);
        AddMonster(classId, point, true);
    }

    void AddPack(std::uint16_t classId, std::span<const SpawnPoint> points) const noexcept {
        for (auto point : points) {
            AddMonster(classId, point, false);
        }
    }
};

void __fastcall InitializeRoom(std::uint8_t bank, void* room) noexcept {
    const auto original = originalInitRoom.load(std::memory_order_acquire);
    if (original == nullptr) {
        return;
    }
    original(bank, room);

    if (bank != static_cast<std::uint8_t>(Bank::Rotw)
        || ReadField<std::uint32_t>(room, 0x74) != PresetRoomType) {
        return;
    }
    const auto* level = ReadField<const void*>(room, 0x90);
    if (level == nullptr) {
        return;
    }
    const auto floor = dungeonPlacement.Floor(ReadField<std::uint32_t>(level, 0x1F8));
    if (floor == 0 || floor % SectionSize != 0) {
        return;
    }
    const auto section = floor / SectionSize - 1;
    const auto* drlg = ReadField<const void*>(level, 0x1C8);
    if (drlg == nullptr || ReadField<std::uint8_t>(drlg, 0x830) != Hell) {
        return;
    }
    EncounterRoom layout{};
    const auto presetBase = section == SectionCount - 1 ? dungeonPlacement.firstLavaPreset : 0;
    if (!FindEncounterRoom(room, SectionStairs[section], presetBase, layout)) {
        return;
    }
    const auto pool = LoadBossPool(section);
    std::array<std::size_t, SoloBossCount + 1> models{};
    std::size_t modelCount = 0;
    std::array<std::uint16_t, SoloBossCount> escorts{};
    std::array<std::size_t, SoloBossCount> escortSizes{};
    for (std::size_t escort = 0; escort < std::size(SealEscorts); ++escort) {
        const auto& spec = SealEscorts[escort];
        escorts[spec.model] = escortClassIds[escort].load(std::memory_order_acquire);
        escortSizes[spec.model] = spec.count;
    }
    for (std::size_t model = 0; model < SoloBossCount; ++model) {
        if (pool[model] != 0 && (escortSizes[model] == 0 || escorts[model] != 0)) {
            models[modelCount++] = model;
        }
    }
    constexpr auto CouncilEncounter = SoloBossCount;
    if (std::all_of(pool.begin() + SoloBossCount, pool.end(), [](auto ids) { return ids != 0; })) {
        models[modelCount++] = CouncilEncounter;
    }
    if (modelCount == 0) {
        return;
    }

    // Both room generators must agree on the pool roll without advancing either RNG.
    const auto sessionSeed = DungeonSessionSeed();
    const auto mapSeed = sessionSeed != 0 ? sessionSeed : ReadField<std::uint32_t>(drlg, 0x840);
    auto seed = mapSeed ^ (floor * 0x9E3779B9U);
    seed ^= seed >> 16;
    seed *= 0x7FEB352DU;
    seed ^= seed >> 15;
    const auto model = models[seed % modelCount];
    const Encounter encounter{bank, room, layout};
    const auto& stair = *layout.stair;
    const auto [x, y] = stair.packCenter;
    const auto [dx, dy] = stair.spacing;
    const SpawnPoint pack[]{
        {x, y}, {x - dx, y}, {x + dx, y}, {x, y - dy}, {x, y + dy},
        {x - dx, y - dy}, {x + dx, y - dy}, {x - dx, y + dy}, {x + dx, y + dy},
    };
    const auto guards = guardClassIds[section].load(std::memory_order_acquire);

    if (model == CouncilEncounter) {
        encounter.AddBoss(pool, SoloBossCount, 0, stair.boss);
        for (std::size_t member = 1; member < CouncilSize; ++member) {
            encounter.AddBoss(pool, SoloBossCount + member, 0, pack[member - 1]);
        }
    } else {
        std::array<std::size_t, NamesPerModel> names{};
        std::size_t nameCount = 0;
        for (std::size_t name = 0; name < names.size(); ++name) {
            if (BossClassId(pool[model], name) != 0) {
                names[nameCount++] = name;
            }
        }
        encounter.AddBoss(pool, model, names[(seed / modelCount) % nameCount], stair.boss);

        if (escortSizes[model] != 0) {
            encounter.AddPack(escorts[model], std::span{pack}.first(escortSizes[model]));
        } else {
            encounter.AddPack(static_cast<std::uint16_t>(guards), std::span{pack}.first<3>());
        }
    }
    encounter.AddPack(static_cast<std::uint16_t>(guards >> 16), stair.localPack);
}

constexpr std::uint8_t InitEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x0F, 0xB6, 0xF9,
    0x48, 0x8B, 0xDA, 0x48, 0x8D, 0x4A, 0x30, 0x8B, 0x52, 0x08, 0xE8, 0x24, 0x38,
    0xF7, 0xFF, 0x8B, 0x53, 0x74, 0x83, 0xEA, 0x01, 0x74, 0x17, 0x83, 0xFA, 0x01, 0x75, 0x1E,
};
constexpr std::uint8_t AllocateEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC,
    0x20, 0x41, 0x8B, 0xF9, 0x41, 0x8B, 0xD8, 0x48, 0x8B, 0xF2, 0xE8, 0xD3, 0xA6, 0x07, 0x00,
};
constexpr std::uint8_t AllocateFields[]{
    0x89, 0x58, 0x20, 0x89, 0x78, 0x04, 0x89, 0x08, 0x8B, 0x44, 0x24, 0x58, 0x41, 0x89,
    0x40, 0x08, 0x8B, 0x44, 0x24, 0x60, 0x41, 0x89, 0x40, 0x24, 0x48, 0x85, 0xF6, 0x74,
    0x25, 0x48, 0x8B, 0x86, 0x98, 0x00, 0x00, 0x00, 0x49, 0x89, 0x40, 0x10, 0x49, 0x8B,
    0xC0, 0x4C, 0x89, 0x86, 0x98, 0x00, 0x00, 0x00,
};
constexpr std::uint8_t LevelIdGetter[]{
    0x48, 0x8B, 0x81, 0x90, 0x00, 0x00, 0x00, 0x8B, 0x80, 0xF8, 0x01, 0x00, 0x00, 0xC3,
};
constexpr std::uint8_t RoomInitCaller[]{
    0x48, 0x8B, 0xDA, 0x48, 0x8B, 0x82, 0x90, 0x00, 0x00, 0x00, 0x0F, 0xB6, 0xF1, 0x48,
    0x8B, 0xB8, 0xC8, 0x01, 0x00, 0x00, 0x75, 0x05, 0xE8, 0xA1, 0x78, 0x03, 0x00, 0x48,
    0x8B, 0xD3, 0x40, 0x0F, 0xB6, 0xCE, 0xE8, 0xC5, 0xA8, 0x0C, 0x00,
};
constexpr std::uint8_t PresetMapFields[]{
    0x4C, 0x8B, 0x71, 0x40, 0x33, 0xFF, 0x8B, 0x41, 0x60, 0x48, 0x8B, 0xF1, 0x44, 0x8B,
    0xE7, 0x49, 0x8B, 0x56, 0x08, 0x2B, 0x42, 0x18, 0x48, 0x8B, 0x6A, 0x10, 0x89, 0x44,
    0x24, 0x20, 0x8B, 0x41, 0x64, 0x2B, 0x42, 0x1C, 0x89, 0x44, 0x24, 0x24, 0x8B, 0x41,
    0x68, 0xFF, 0xC0, 0x89, 0x44, 0x24, 0x28, 0x8B, 0x41, 0x6C, 0xFF, 0xC0, 0x89, 0x44,
    0x24, 0x2C, 0x44, 0x8B, 0x6A, 0x20,
};
constexpr std::uint8_t MapSeedStore[]{0x89, 0x9F, 0x40, 0x08, 0x00, 0x00, 0x8B, 0x85, 0x28, 0x08, 0x00, 0x00};
constexpr std::uint8_t DifficultyLoad[]{0x44, 0x0F, 0xB6, 0x8D, 0x30, 0x08, 0x00, 0x00, 0x45, 0x33, 0xDB};

bool InstallBossNames(const D2RL::PluginContext* context, const D2RL::MutationService* mutations) noexcept {
    constexpr std::uintptr_t unitNameRva = 0x9A1B0;
    constexpr std::uintptr_t unitRoomRva = 0x34B440;
    constexpr std::uintptr_t roomLevelRva = 0x2EFC10;
    constexpr std::uintptr_t singlePlayerRva = 0x8D4B0;
    constexpr std::uint8_t nameEntry[]{
        0x40, 0x53, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x40, 0x4C, 0x8B, 0xF1, 0xE8, 0x1F, 0xFF, 0x2A, 0x00,
    };
    constexpr std::uint8_t nameCaller[]{0xE8, 0xC5, 0xB6, 0xFC, 0xFF, 0x48, 0x8B, 0xD8};
    constexpr std::uint8_t monsterBranch[]{
        0x45, 0x8B, 0x06, 0x0F, 0xB6, 0xD8, 0x45, 0x85, 0xC0, 0x0F, 0x84, 0x60, 0x02, 0x00, 0x00,
        0x41, 0x83, 0xE8, 0x01, 0x0F, 0x84, 0x13, 0x02, 0x00, 0x00,
    };
    constexpr std::uint8_t classRead[]{0x41, 0x0F, 0xBF, 0x56, 0x04, 0x0F, 0xB6, 0xCB};
    constexpr std::uint8_t bankRead[]{0x0F, 0xB6, 0x81, 0xBD, 0x01, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x28, 0xC3};
    constexpr std::uint8_t roomEntry[]{
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x13,
    };
    constexpr std::uint8_t roomPath[]{
        0x8B, 0x0B, 0x83, 0xE9, 0x02, 0x74, 0x25, 0x83, 0xE9, 0x02, 0x74, 0x20, 0x83, 0xF9, 0x01,
        0x74, 0x1B, 0x48, 0x8B, 0x4B, 0x38, 0x48, 0x85, 0xC9, 0x74, 0x0A, 0x48, 0x83, 0xC4, 0x20,
        0x5B, 0xE9, 0xAB, 0x67, 0xFF, 0xFF,
    };
    constexpr std::uint8_t roomLevelEntry[]{
        0x48, 0x85, 0xC9, 0x75, 0x03, 0x33, 0xC0, 0xC3, 0x48, 0x8B, 0x49, 0x18, 0xE9, 0x9F, 0x13, 0x07, 0x00,
    };
    constexpr std::uint8_t singlePlayerEntry[]{0x83, 0x3D, 0x3D, 0x5F, 0x99, 0x02, 0x00, 0x0F, 0x94, 0xC0, 0xC3};
    if (!context->CheckExpectedBytes(0xCEAE6, nameCaller, sizeof(nameCaller))
        || !context->CheckExpectedBytes(0x9A1C1, monsterBranch, sizeof(monsterBranch))
        || !context->CheckExpectedBytes(0x9A412, classRead, sizeof(classRead))
        || !context->CheckExpectedBytes(0x34A103, bankRead, sizeof(bankRead))
        || !context->CheckExpectedBytes(unitRoomRva, roomEntry, sizeof(roomEntry))
        || !context->CheckExpectedBytes(0x34B461, roomPath, sizeof(roomPath))
        || !context->CheckExpectedBytes(roomLevelRva, roomLevelEntry, sizeof(roomLevelEntry))
        || !context->CheckExpectedBytes(0x360FC0, LevelIdGetter, sizeof(LevelIdGetter))
        || !context->CheckExpectedBytes(singlePlayerRva, singlePlayerEntry, sizeof(singlePlayerEntry))) {
        context->LogWarn("Checkpoint name display unavailable. The native unit name path could not be verified.");
        return true;
    }
    const D2RL::LocalizationService* service = nullptr;
    if (context->QueryService(&service) == D2RL::ServiceQueryResult::Success
        && D2RL::HasLocalizationServiceField(service, D2RL::LocalizationServiceRequiredSize) && service->getStringByKey) {
        localization = service;
    }
    nameContext = context;
    getNameUnitRoom = reinterpret_cast<UnitRoomFn>(context->exeBase + unitRoomRva);
    getNameRoomLevel = reinterpret_cast<RoomLevelFn>(context->exeBase + roomLevelRva);
    isSinglePlayer = reinterpret_cast<IsSinglePlayerFn>(context->exeBase + singlePlayerRva);
    const auto status = InstallHook(context, mutations, unitNameRva, nameEntry, UnitName, originalUnitName);
    if (status != HookStatus::Installed) context->LogWarn("Could not install checkpoint name display.");
    return status != HookStatus::MissingOriginal;
}

}  // namespace

bool IsDungeonCheckpointBoss(std::uint32_t floor, std::uint32_t classId) noexcept {
    if (floor == 0 || floor > DungeonFloorCount || floor % SectionSize != 0) return false;
    const auto pool = LoadBossPool(floor / SectionSize - 1);
    return ContainsBoss(pool, classId);
}

bool IsCheckpointAuraBearer(std::uint32_t floor, std::uint32_t classId) noexcept {
    if (floor == 0 || floor > DungeonFloorCount || floor % SectionSize != 0) return false;
    const auto pool = LoadBossPool(floor / SectionSize - 1);
    // The first Council member is the one placed at the main boss position.
    return ContainsBoss(std::span{pool}.first<SoloBossCount + 1>(), classId);
}

bool InstallBossRoom(const D2RL::PluginContext* context, const D2RL::MutationService* mutations) noexcept {
    if (!context->CheckExpectedBytes(AllocatePresetUnitRva, AllocateEntry, sizeof(AllocateEntry))
        || !context->CheckExpectedBytes(0x360C22, AllocateFields, sizeof(AllocateFields))
        || !context->CheckExpectedBytes(0x360FC0, LevelIdGetter, sizeof(LevelIdGetter))
        || !context->CheckExpectedBytes(0x328FE4, RoomInitCaller, sizeof(RoomInitCaller))
        || !context->CheckExpectedBytes(0x3DE44B, PresetMapFields, sizeof(PresetMapFields))
        || !context->CheckExpectedBytes(0x326EAD, MapSeedStore, sizeof(MapSeedStore))
        || !context->CheckExpectedBytes(0x327680, DifficultyLoad, sizeof(DifficultyLoad))) {
        context->LogWarn("Dungeon boss unavailable. The native room layout could not be verified.");
        return true;
    }
    const D2RL::LifecycleService* lifecycle = nullptr;
    constexpr auto registerEnd = offsetof(D2RL::LifecycleService, unregisterDataTablesLoadedListener);
    constexpr auto getTableEnd = offsetof(D2RL::DataTableService, getRow);
    if (context->QueryService(&lifecycle) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasLifecycleServiceField(lifecycle, registerEnd) || !lifecycle->registerDataTablesLoadedListener
        || context->QueryService(&tables) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasDataTableServiceField(tables, getTableEnd) || !tables->getTable) {
        context->LogWarn("Dungeon boss unavailable. Lifecycle and data table services are required.");
        return true;
    }

    allocatePresetUnit = reinterpret_cast<AllocatePresetUnitFn>(context->exeBase + AllocatePresetUnitRva);
    const auto status = InstallHook(context, mutations, InitRoomRva, InitEntry, InitializeRoom, originalInitRoom);
    if (status != HookStatus::Installed) {
        context->LogError("Could not install the dungeon boss encounter hook.");
        return status != HookStatus::MissingOriginal;
    }
    const D2RL::Lifecycle::DataTablesLoadedListener listener{
        .structSize = D2RL::Lifecycle::DataTablesLoadedListenerSize, .flags = 0,
        .callback = OnTablesLoaded, .userData = nullptr,
    };
    D2RL::Lifecycle::ListenerHandle handle{};
    if (lifecycle->registerDataTablesLoadedListener(context, &listener, &handle) != D2RL::Lifecycle::Result::Success) {
        context->LogWarn("Dungeon boss unavailable. Could not register for data table loads.");
        return true;
    }
    context->LogInfo("Enabled dungeon boss encounters.");
    return InstallBossNames(context, mutations);
}
