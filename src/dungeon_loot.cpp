#include "dungeon_layout.h"

#include <array>
#include <bit>
#include <intrin.h>
#include <limits>

namespace {

constexpr std::uintptr_t DropRva = 0x441300;
constexpr std::uintptr_t MonsterDropReturnRva = 0x447E1C;
constexpr std::uintptr_t ApplyPropertiesRva = 0x3D3670;
constexpr std::uintptr_t SuperiorPropertiesReturnRva = 0x58BB8E;
constexpr std::uintptr_t UnitRoomRva = 0x34B440;
constexpr std::uintptr_t RoomLevelRva = 0x2EFC10;
constexpr std::uintptr_t RollRva = 0x367160;
constexpr std::uintptr_t IsSinglePlayerRva = 0x8D4B0;
constexpr std::uintptr_t ItemSeedRva = 0x36CC80;
constexpr std::size_t TreasureRowSize = 0x150;
constexpr std::size_t QualityRowSize = 0x2C;
constexpr std::uint16_t FirstBonusGroup = 27183;
constexpr std::uint32_t BonusWeight = 1000;
constexpr std::uint32_t GearAndGemsWeight = 994;
constexpr std::uint32_t BonusRollSize = 1024;
constexpr std::uint32_t WorldstoneWeight = 30;
constexpr std::uint32_t CorruptedWeight = 20;
constexpr std::uint32_t KeyWeight = 20;
constexpr std::uint8_t Rotw = static_cast<std::uint8_t>(D2RL::DataTables::Bank::Rotw);
constexpr std::uint32_t MonsterUnitType = 1;

// Keep this order in sync with BONUS_ITEM_CODES in the table generator.
enum BonusItem : std::size_t {
    WesternShard, EasternShard, SouthernShard, DeepShard, NorthernShard,
    CorruptedShard, TerrorKey, HateKey, DestructionKey, BonusItemCount,
};
constexpr std::array BonusItemCodes{"xa1", "xa2", "xa3", "xa4", "xa5", "csz", "pk1", "pk2", "pk3"};
constexpr std::array SectionKeys{TerrorKey, BonusItemCount, HateKey, BonusItemCount, BonusItemCount, DestructionKey};
constexpr auto WorldstoneCount = static_cast<std::uint32_t>(CorruptedShard);
static_assert(BonusItemCodes.size() == BonusItemCount && BonusItemCount <= 16);
static_assert(SectionKeys.size() == DungeonSectionCount && WorldstoneWeight % WorldstoneCount == 0);

struct BonusTables {
    std::uint16_t standard{};
    std::uint16_t specialRows{};
    std::uint16_t specialItems{};
    std::uint16_t gearAndGems{};
};
static_assert(sizeof(BonusTables) == sizeof(std::uint64_t));

using DropFn = void(__fastcall*)(void* game, void* source, void* killer, std::uint16_t treasureClass,
    std::int32_t quality, std::int32_t level, std::int32_t options, void** items,
    std::int32_t* count, std::int32_t limit) noexcept;
using ApplyPropertiesFn = std::uintptr_t(__fastcall*)(std::uint32_t mode, std::uintptr_t target,
    void* item, const void* properties, std::int32_t option, std::int32_t layer) noexcept;
using UnitPointerFn = void*(__fastcall*)(void*) noexcept;
using RoomLevelFn = std::uint32_t(__fastcall*)(const void*) noexcept;
using RollFn = std::uint32_t(__fastcall*)(void*, std::uint32_t) noexcept;
using IsSinglePlayerFn = bool(__fastcall*)() noexcept;

const D2RL::PluginContext* context = nullptr;
const D2RL::DataTableService* tables = nullptr;
UnitPointerFn getUnitRoom = nullptr;
RoomLevelFn getRoomLevel = nullptr;
RollFn roll = nullptr;
IsSinglePlayerFn isSinglePlayer = nullptr;
UnitPointerFn getItemSeed = nullptr;
std::atomic<DropFn> originalDrop = nullptr;
std::atomic<ApplyPropertiesFn> originalApplyProperties = nullptr;
std::atomic<BonusTables> bonusTables{};
bool specialEntryLayoutVerified = false;
std::atomic_bool superiorRowsReady = false;
thread_local std::uint32_t lootFloor = 0;

struct LootFloorScope {
    const std::uint32_t previous = lootFloor;
    explicit LootFloorScope(std::uint32_t floor) noexcept { lootFloor = floor; }
    ~LootFloorScope() { lootFloor = previous; }
};

std::uint32_t DropFloor(std::uintptr_t caller, const void* game, void* source) noexcept {
    if (caller != MonsterDropReturnRva || game == nullptr || source == nullptr
        || ReadField<std::uint32_t>(source, 0) != MonsterUnitType
        || ReadField<std::uint8_t>(game, 0x104) != Hell || ReadField<std::uint8_t>(game, 0x106) != Rotw
        || !isSinglePlayer()) {
        return 0;
    }
    return dungeonPlacement.Floor(getRoomLevel(getUnitRoom(source)));
}

std::uint16_t SelectBonusClass(const BonusTables& bonus, std::uint32_t floor, std::uint32_t ticket) noexcept {
    const auto section = (floor - 1) / DungeonSectionSize;
    auto item = static_cast<std::size_t>(BonusItemCount);
    if (ticket < WorldstoneWeight) {
        item = section < WorldstoneCount ? section : ticket / (WorldstoneWeight / WorldstoneCount);
    } else if (ticket < WorldstoneWeight + CorruptedWeight) {
        item = CorruptedShard;
    } else if (ticket < WorldstoneWeight + CorruptedWeight + KeyWeight) {
        item = SectionKeys[section];
    }
    const auto bit = 1U << item;
    if ((bonus.specialItems & bit) == 0) {
        return bonus.standard;
    }
    // The generator omits rows for absent items. Count the rows before this one.
    const auto preceding = std::popcount(bonus.specialRows & (bit - 1));
    return static_cast<std::uint16_t>(bonus.standard + 1 + preceding);
}

std::uint16_t RollBonusClass(const BonusTables& bonus, std::uint32_t floor, void* seed) noexcept {
    if (bonus.gearAndGems != 0) {
        constexpr auto ticketCount = 1U << 16;
        constexpr auto fullPoolTickets = ticketCount / 3;
        auto ticket = roll(seed, ticketCount);
        // The top ticket is outside the three equal ranges. Bound the retry for a stuck seed.
        if (ticket == ticketCount - 1) {
            ticket = roll(seed, ticketCount);
        }
        if (ticket >= fullPoolTickets) {
            return bonus.gearAndGems;
        }
    }
    return bonus.specialItems != 0 ? SelectBonusClass(bonus, floor, roll(seed, BonusRollSize))
                                   : bonus.standard;
}

void __fastcall Drop(void* game, void* source, void* killer,
                     std::uint16_t treasureClass, std::int32_t quality, std::int32_t level,
                     std::int32_t options, void** items, std::int32_t* count, std::int32_t limit) noexcept {
    const auto original = originalDrop.load(std::memory_order_acquire);
    if (original == nullptr) {
        return;
    }
    const auto bonus = bonusTables.load(std::memory_order_acquire);
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - context->exeBase;
    const auto floor = (bonus.standard != 0 || superiorRowsReady.load(std::memory_order_acquire))
        ? DropFloor(caller, game, source) : 0;
    const LootFloorScope scope(floor);
    original(game, source, killer, treasureClass, quality, level, options, items, count, limit);

    // The monster-death caller uses default quality and supplies no output buffer.
    if (floor == 0 || bonus.standard == 0 || treasureClass == 0 || killer == nullptr
        || quality != 0 || items != nullptr || count != nullptr || limit != 0
        || IsDungeonCheckpointBoss(floor, ReadField<std::uint32_t>(source, 4))) {
        return;
    }
    const auto chance = floor + 15;
    auto* seed = static_cast<std::byte*>(source) + 0x28;
    if (roll(seed, BonusRollSize) < chance) {
        original(game, source, killer, RollBonusClass(bonus, floor, seed), 0, level, options, nullptr, nullptr, 1);
    }
}

std::uintptr_t __fastcall ApplyProperties(std::uint32_t mode, std::uintptr_t target, void* item,
                                          const void* row, std::int32_t option, std::int32_t layer) noexcept {
    const auto original = originalApplyProperties.load(std::memory_order_acquire);
    if (original == nullptr) {
        return 0;
    }
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - context->exeBase;
    if (lootFloor == 0 || caller != SuperiorPropertiesReturnRva || mode != 1 || item == nullptr
        || row == nullptr || !superiorRowsReady.load(std::memory_order_acquire)) {
        return original(mode, target, item, row, option, layer);
    }
    auto* seed = getItemSeed(item);
    if (seed == nullptr) {
        return original(mode, target, item, row, option, layer);
    }

    // Copy the row for this call. The game still selects the properties and
    // rolls each value. A biased property draws from the upper half of its range.
    alignas(4) std::array<std::byte, QualityRowSize> adjusted{};
    std::memcpy(adjusted.data(), row, adjusted.size());
    bool biased = false;
    for (std::size_t offset : {0xCU, 0x1CU}) {
        const auto property = ReadField<std::int32_t>(row, offset);
        const auto min = ReadField<std::int32_t>(row, offset + 8);
        const auto max = ReadField<std::int32_t>(row, offset + 12);
        if (property >= 0 && min >= 0 && max > min && max <= std::numeric_limits<std::int16_t>::max()
            && roll(seed, 128) < lootFloor) {
            const auto higherMin = min + (max - min + 1) / 2;
            std::memcpy(adjusted.data() + offset + 8, &higherMin, sizeof(higherMin));
            biased = true;
        }
    }
    return original(mode, target, item, biased ? adjusted.data() : row, option, layer);
}

bool ReadTable(D2RL::DataTables::TableId id, std::size_t rowSize, D2RL::DataTables::TableView& view) noexcept {
    using namespace D2RL::DataTables;
    view.structSize = TableViewSize;
    return tables->getTable(context, Bank::Rotw, id, &view) == Result::Success
        && HasTableViewField(&view, TableViewRequiredSize) && view.rowSize == rowSize && view.rows != nullptr;
}

bool VerifyFloors() noexcept {
    D2RL::DataTables::TableView levels{};
    return ReadTable(D2RL::DataTables::TableId::LevelDefs, 0x9C, levels) && VerifyDungeonLevels(levels);
}

bool MatchesSpecialItem(const std::byte* row, const char* code) noexcept {
    using namespace D2RL::DataTables;
    RowView item{};
    item.structSize = RowViewSize;
    const auto padded = MakeFourCC(code[0], code[1], code[2], ' ');
    const auto unpadded = MakeFourCC(code[0], code[1], code[2]);
    const auto findItem = tables->findRowByCode;
    if (findItem(context, Bank::Rotw, TableId::Items, padded, &item) != Result::Success) {
        item = {};
        item.structSize = RowViewSize;
        if (findItem(context, Bank::Rotw, TableId::Items, unpadded, &item) != Result::Success) {
            return false;
        }
    }
    if (!HasRowViewField(&item, RowViewRequiredSize) || item.row == nullptr || item.bank != Bank::Rotw
        || item.tableId != TableId::Items || ReadField<std::uint64_t>(row, 0x40) != 1
        || ReadField<std::uint32_t>(row, 0x14) != 1 || ReadField<std::int32_t>(row, 0x18) != 1) {
        return false;
    }
    const auto* entry = ReadField<const void*>(row, 0x38);
    // A plain item entry uses the Items row index. The low flags identify nested classes or special qualities.
    return entry != nullptr && (ReadField<std::uint8_t>(entry, 0xC) & 7) == 0
        && ReadField<std::uint32_t>(entry, 8) == item.rowIndex;
}

BonusTables FindBonusTables() noexcept {
    D2RL::DataTables::TableView table{};
    if (!ReadTable(D2RL::DataTables::TableId::TreasureClasses, TreasureRowSize, table)
        || table.rowCount > std::numeric_limits<std::uint16_t>::max()) {
        return {};
    }
    const auto* rows = static_cast<const std::byte*>(table.rows);
    BonusTables bonus{};
    constexpr auto gearSlot = BonusItemCodes.size() + 1;
    for (std::uint32_t i = 2; i + 1 < table.rowCount; ++i) {
        const auto* row = rows + i * table.rowSize;
        // The expansion table merge adds the preceding bank's group count to
        // nonzero groups. Identify the pair of Descent tables together.
        const auto group = ReadField<std::uint16_t>(row, 0);
        if (group < FirstBonusGroup || group + gearSlot > std::numeric_limits<std::int16_t>::max()
            || ReadField<std::uint16_t>(row, 2) != 1 || ReadField<std::int32_t>(row, 0x18) != 1
            || ReadField<std::uint32_t>(row, 0x14) != BonusWeight
            || ReadField<std::uint16_t>(row + table.rowSize, 0) == group) {
            continue;
        }
        const auto* gear = row - table.rowSize;
        if (ReadField<std::uint16_t>(gear, 0) != group + gearSlot
            || ReadField<std::uint16_t>(gear, 2) != gearSlot + 1
            || ReadField<std::uint32_t>(gear, 0x14) != GearAndGemsWeight
            || ReadField<std::int32_t>(gear, 0x18) != 1) continue;
        if (bonus.standard != 0) return {};
        bonus.standard = static_cast<std::uint16_t>(i);
        bonus.gearAndGems = static_cast<std::uint16_t>(i - 1);
    }
    if (bonus.standard == 0 || !specialEntryLayoutVerified
        || !D2RL::HasDataTableServiceField(tables, D2RL::DataTableServiceRequiredSize) || !tables->findRowByCode) {
        return bonus;
    }
    const auto group = ReadField<std::uint16_t>(rows + bonus.standard * table.rowSize, 0);
    auto next = static_cast<std::uint32_t>(bonus.standard) + 1;
    for (std::size_t item = 0; item < BonusItemCodes.size() && next + 1 < table.rowCount; ++item) {
        const auto* row = rows + next * table.rowSize;
        const auto expectedGroup = group + 1U + item;
        if (expectedGroup > std::numeric_limits<std::int16_t>::max()
            || ReadField<std::uint16_t>(row, 0) != expectedGroup || ReadField<std::uint16_t>(row, 2) != item + 2) {
            continue;
        }
        ++next;
        const auto bit = static_cast<std::uint16_t>(1U << item);
        bonus.specialRows |= bit;
        if (ReadField<std::uint16_t>(row + table.rowSize, 0) != expectedGroup && MatchesSpecialItem(row, BonusItemCodes[item])) {
            bonus.specialItems |= bit;
        }
    }
    return bonus;
}

void __cdecl OnLootTablesLoaded(const D2RL::PluginContext* pluginContext,
                                const D2RL::Lifecycle::DataTablesLoadedEvent*, void*) noexcept {
    bonusTables.store({}, std::memory_order_release);
    superiorRowsReady.store(false, std::memory_order_release);
    if (!VerifyFloors()) {
        pluginContext->LogWarn("Descent loot scaling could not verify the dungeon level rows.");
        return;
    }
    const auto bonus = FindBonusTables();
    bonusTables.store(bonus, std::memory_order_release);
    if (bonus.standard == 0) {
        pluginContext->LogWarn("Descent bonus loot could not resolve its treasure class.");
    }

    D2RL::DataTables::TableView quality{};
    if (originalApplyProperties.load(std::memory_order_acquire) != nullptr
        && ReadTable(D2RL::DataTables::TableId::QualityItems, QualityRowSize, quality) && quality.rowCount > 0) {
        superiorRowsReady.store(true, std::memory_order_release);
    }
}

constexpr std::uint8_t DropEntry[]{
    0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC,
    0x98, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x05, 0xAD, 0x9F, 0x58, 0x02, 0x48, 0x33, 0xC4,
};
constexpr std::uint8_t MonsterDropCaller[]{
    0x8B, 0x84, 0x24, 0xC0, 0x00, 0x00, 0x00, 0x33, 0xC9, 0x4D, 0x8B, 0x47, 0x10, 0x45, 0x0F, 0xB7,
    0xCC, 0x89, 0x4C, 0x24, 0x48, 0x48, 0x8B, 0xD5, 0x48, 0x89, 0x4C, 0x24, 0x40, 0x48, 0x89, 0x4C,
    0x24, 0x38, 0x89, 0x44, 0x24, 0x30, 0x8B, 0x84, 0x24, 0xB8, 0x00, 0x00, 0x00, 0x89, 0x44, 0x24,
    0x28, 0x89, 0x4C, 0x24, 0x20, 0x49, 0x8B, 0xCE, 0xE8, 0xE4, 0x94, 0xFF, 0xFF,
};
constexpr std::uint8_t DropBank[]{0x0F, 0xB6, 0x8E, 0x06, 0x01, 0x00, 0x00, 0x44, 0x8B, 0xC7, 0x0F, 0xB7, 0xD5};
constexpr std::uint8_t DifficultyRead[]{0x41, 0x0F, 0xB6, 0x8E, 0x04, 0x01, 0x00, 0x00};
constexpr std::uint8_t UnitRoomEntry[]{0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x13};
constexpr std::uint8_t UnitRoomPath[]{
    0x8B, 0x0B, 0x83, 0xE9, 0x02, 0x74, 0x25, 0x83, 0xE9, 0x02, 0x74, 0x20, 0x83, 0xF9, 0x01,
    0x74, 0x1B, 0x48, 0x8B, 0x4B, 0x38, 0x48, 0x85, 0xC9, 0x74, 0x0A, 0x48, 0x83, 0xC4, 0x20,
    0x5B, 0xE9, 0xAB, 0x67, 0xFF, 0xFF,
};
constexpr std::uint8_t RoomLevelEntry[]{
    0x48, 0x85, 0xC9, 0x75, 0x03, 0x33, 0xC0, 0xC3, 0x48, 0x8B, 0x49, 0x18, 0xE9, 0x9F, 0x13, 0x07, 0x00,
};
constexpr std::uint8_t LevelIdRead[]{0x48, 0x8B, 0x81, 0x90, 0x00, 0x00, 0x00, 0x8B, 0x80, 0xF8, 0x01, 0x00, 0x00, 0xC3};
constexpr std::uint8_t UnitSeedField[]{0x48, 0x8D, 0x41, 0x28, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3};
constexpr std::uint8_t RollEntry[]{
    0x8B, 0x01, 0x4C, 0x69, 0xC0, 0xC5, 0x90, 0xC6, 0x6A, 0x8B, 0x41, 0x04, 0x4C, 0x03, 0xC0, 0x49,
    0x8B, 0xC0, 0x44, 0x89, 0x01, 0x48, 0xC1, 0xE8, 0x20, 0x89, 0x41, 0x04, 0x8D, 0x42, 0xFF, 0x41,
    0x23, 0xC0, 0xC3,
};
constexpr std::uint8_t SinglePlayerEntry[]{0x83, 0x3D, 0x3D, 0x5F, 0x99, 0x02, 0x00, 0x0F, 0x94, 0xC0, 0xC3};
constexpr std::uint8_t TreasureRowLayout[]{
    0x4C, 0x69, 0x44, 0x24, 0x68, 0x50, 0x01, 0x00, 0x00, 0x4C, 0x03, 0x07, 0x85, 0xDB, 0x7E, 0x30,
    0x41, 0x0F, 0xB7, 0x00, 0x66, 0x85, 0xC0, 0x74, 0x27, 0x49, 0x8D, 0x88, 0x50, 0x01, 0x00, 0x00,
    0x66, 0x39, 0x01, 0x75, 0x1B, 0x0F, 0xBF, 0x41, 0x02, 0x3B, 0xC3, 0x7F, 0x13,
};
constexpr std::uint8_t TreasureEntries[]{
    0x49, 0x8B, 0x5D, 0x38, 0x45, 0x33, 0xFF, 0x49, 0x8B, 0x4D, 0x40, 0x48, 0x6B, 0xC1, 0x20,
};
constexpr std::uint8_t TreasureEntryItem[]{
    0xF6, 0x43, 0x0C, 0x04, 0x44, 0x8B, 0x7B, 0x08, 0x90, 0x0F, 0x84, 0x82, 0x01, 0x00, 0x00,
};
constexpr std::uint8_t ApplyEntry[]{
    0x48, 0x89, 0x54, 0x24, 0x10, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x57, 0x48,
    0x8B, 0xEC, 0x48, 0x83, 0xEC, 0x70, 0x48, 0x63, 0xD9, 0x4D, 0x8B, 0xE1, 0x49, 0x8B, 0xF8,
};
constexpr std::uint8_t SuperiorCaller[]{
    0x33, 0xC0, 0x4C, 0x8B, 0xCD, 0x89, 0x44, 0x24, 0x28, 0x4D, 0x8B, 0xC6, 0x33, 0xD2, 0x89, 0x44,
    0x24, 0x20, 0x8D, 0x48, 0x01, 0xE8, 0xE2, 0x7A, 0xE4, 0xFF,
};
constexpr std::uint8_t SuperiorRowStride[]{0x48, 0x6B, 0x44, 0x24, 0x50, 0x2C, 0x48, 0x03, 0x03};
constexpr std::uint8_t SuperiorPropertyOffset[]{0x41, 0x8B, 0xC6, 0x48, 0xC1, 0xE0, 0x04, 0x48, 0x83, 0xC0, 0x0C, 0x49, 0x03, 0xC4};
constexpr std::uint8_t ItemSeedEntry[]{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x74, 0x0A, 0xE8, 0x3D,
    0xED, 0xFD, 0xFF, 0x83, 0xF8, 0x04, 0x74, 0x19,
};
constexpr std::uint8_t ItemSeedField[]{0x48, 0x83, 0xC0, 0x04, 0x48, 0x83, 0xC4};

bool VerifyDropLayout(const D2RL::PluginContext* ctx) noexcept {
    return ctx->CheckExpectedBytes(0x447DDF, MonsterDropCaller, sizeof(MonsterDropCaller))
        && ctx->CheckExpectedBytes(0x44142B, DropBank, sizeof(DropBank))
        && ctx->CheckExpectedBytes(0x447D55, DifficultyRead, sizeof(DifficultyRead))
        && ctx->CheckExpectedBytes(UnitRoomRva, UnitRoomEntry, sizeof(UnitRoomEntry))
        && ctx->CheckExpectedBytes(0x34B461, UnitRoomPath, sizeof(UnitRoomPath))
        && ctx->CheckExpectedBytes(RoomLevelRva, RoomLevelEntry, sizeof(RoomLevelEntry))
        && ctx->CheckExpectedBytes(0x360FC0, LevelIdRead, sizeof(LevelIdRead))
        && ctx->CheckExpectedBytes(0x34A20B, UnitSeedField, sizeof(UnitSeedField))
        && ctx->CheckExpectedBytes(RollRva, RollEntry, sizeof(RollEntry))
        && ctx->CheckExpectedBytes(IsSinglePlayerRva, SinglePlayerEntry, sizeof(SinglePlayerEntry))
        && ctx->CheckExpectedBytes(0x39730D, TreasureRowLayout, sizeof(TreasureRowLayout));
}

bool VerifySuperiorLayout(const D2RL::PluginContext* ctx) noexcept {
    return ctx->CheckExpectedBytes(0x58BB74, SuperiorCaller, sizeof(SuperiorCaller))
        && ctx->CheckExpectedBytes(0x313DBF, SuperiorRowStride, sizeof(SuperiorRowStride))
        && ctx->CheckExpectedBytes(0x3D3A59, SuperiorPropertyOffset, sizeof(SuperiorPropertyOffset))
        && ctx->CheckExpectedBytes(ItemSeedRva, ItemSeedEntry, sizeof(ItemSeedEntry))
        && ctx->CheckExpectedBytes(0x36CCE6, ItemSeedField, sizeof(ItemSeedField));
}

}  // namespace

bool InstallDungeonLoot(const D2RL::PluginContext* ctx, const D2RL::MutationService* mutations) noexcept {
    if (!VerifyDropLayout(ctx)) {
        ctx->LogWarn("Descent loot scaling could not verify the monster drop path.");
        return true;
    }
    const D2RL::LifecycleService* lifecycle = nullptr;
    if (ctx->QueryService(&lifecycle) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasLifecycleServiceField(lifecycle, offsetof(D2RL::LifecycleService, unregisterDataTablesLoadedListener))
        || !lifecycle->registerDataTablesLoadedListener
        || ctx->QueryService(&tables) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasDataTableServiceField(tables, offsetof(D2RL::DataTableService, getRow)) || !tables->getTable) {
        ctx->LogWarn("Descent loot scaling requires lifecycle and data table services.");
        return true;
    }
    context = ctx;
    specialEntryLayoutVerified = ctx->CheckExpectedBytes(0x44084B, TreasureEntries, sizeof(TreasureEntries))
        && ctx->CheckExpectedBytes(0x440D17, TreasureEntryItem, sizeof(TreasureEntryItem));
    if (!specialEntryLayoutVerified) {
        ctx->LogWarn("Descent could not verify the optional bonus item layout.");
    }
    getUnitRoom = reinterpret_cast<UnitPointerFn>(ctx->exeBase + UnitRoomRva);
    getRoomLevel = reinterpret_cast<RoomLevelFn>(ctx->exeBase + RoomLevelRva);
    roll = reinterpret_cast<RollFn>(ctx->exeBase + RollRva);
    isSinglePlayer = reinterpret_cast<IsSinglePlayerFn>(ctx->exeBase + IsSinglePlayerRva);
    const auto dropStatus = InstallHook(ctx, mutations, DropRva, DropEntry, Drop, originalDrop);
    if (dropStatus != HookStatus::Installed) {
        ctx->LogWarn("Could not install the Descent monster loot hook.");
        return dropStatus != HookStatus::MissingOriginal;
    }
    if (VerifySuperiorLayout(ctx)) {
        getItemSeed = reinterpret_cast<UnitPointerFn>(ctx->exeBase + ItemSeedRva);
        const auto status = InstallHook(ctx, mutations, ApplyPropertiesRva, ApplyEntry, ApplyProperties, originalApplyProperties);
        if (status == HookStatus::MissingOriginal) {
            return false;
        }
        if (status != HookStatus::Installed) {
            ctx->LogWarn("Could not install the Descent superior property hook.");
        }
    } else {
        ctx->LogWarn("Descent superior rolls could not verify the item property path.");
    }
    const D2RL::Lifecycle::DataTablesLoadedListener listener{
        .structSize = D2RL::Lifecycle::DataTablesLoadedListenerSize, .flags = 0,
        .callback = OnLootTablesLoaded, .userData = nullptr,
    };
    D2RL::Lifecycle::ListenerHandle handle{};
    if (lifecycle->registerDataTablesLoadedListener(ctx, &listener, &handle) != D2RL::Lifecycle::Result::Success) {
        ctx->LogWarn("Descent loot scaling could not subscribe to data table loads.");
    }
    return true;
}
