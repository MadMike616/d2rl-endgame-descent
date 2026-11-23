#pragma once

#include "native_hook.h"

#include <cstring>

inline constexpr std::uint32_t DungeonFloorCount = 60;
inline constexpr std::uint32_t DungeonSectionSize = 10;
inline constexpr std::size_t DungeonSectionCount = DungeonFloorCount / DungeonSectionSize;
static_assert(DungeonFloorCount % DungeonSectionSize == 0);
inline constexpr std::uint32_t DefaultDungeonArea = 138;
inline constexpr std::uint32_t DefaultDungeonLayer = 100;
inline constexpr std::uint32_t DefaultDungeonPreset = 1092;
inline constexpr std::uint32_t HarrogathArea = 109;
inline constexpr std::uint32_t PresetRoomType = 2;
inline constexpr std::uint8_t Hell = 2;

constexpr std::uint32_t MixDungeonSeed(std::uint32_t seed) noexcept {
    seed ^= seed >> 16;
    seed *= 0x7FEB352DU;
    seed ^= seed >> 15;
    seed *= 0x846CA68BU;
    return seed ^ (seed >> 16);
}

struct DungeonPlacement {
    std::uint32_t firstArea{};
    std::uint32_t firstLayer{};
    std::uint32_t firstLavaPreset{};
    std::uint32_t doorWarp{};
    std::uint32_t doorSlot{};

    std::uint32_t Floor(std::uint32_t area) const noexcept {
        if (firstArea == 0 || area < firstArea || area >= firstArea + DungeonFloorCount) return 0;
        return area - firstArea + 1;
    }
};

// The installer resolves this from the mod's own tables before hooks are installed.
inline DungeonPlacement dungeonPlacement{};

template <typename Value>
Value ReadField(const void* object, std::size_t offset) noexcept {
    Value value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}

inline bool VerifyDungeonLevels(const D2RL::DataTables::TableView& levels) noexcept {
    const auto& placement = dungeonPlacement;
    if (placement.firstArea == 0 || levels.rows == nullptr || levels.rowSize != 0x9C
        || levels.rowCount <= placement.firstArea + DungeonFloorCount) return false;
    const auto* rows = static_cast<const std::byte*>(levels.rows);
    const auto* town = rows + HarrogathArea * levels.rowSize;
    const auto* first = rows + placement.firstArea * levels.rowSize;
    const auto slotOffset = placement.doorSlot * sizeof(std::uint32_t);
    if (ReadField<std::uint32_t>(town, 0x48 + slotOffset) != placement.firstArea
        || ReadField<std::uint32_t>(town, 0x68 + slotOffset) != placement.doorWarp
        || ReadField<std::uint32_t>(first, 0x48) != HarrogathArea) return false;
    for (std::uint32_t index = 0; index <= DungeonFloorCount; ++index) {
        const auto* row = first + index * levels.rowSize;
        if (ReadField<std::uint32_t>(row, 8) != placement.firstLayer + index) return false;
    }
    return true;
}

bool InstallDungeonLayout(const D2RL::PluginContext* context, const D2RL::MutationService* mutations) noexcept;
bool InstallDungeonLoot(const D2RL::PluginContext* context, const D2RL::MutationService* mutations) noexcept;
bool InstallDungeonEntrance(const D2RL::PluginContext* context, const D2RL::MutationService* mutations) noexcept;
bool IsDungeonCheckpointBoss(std::uint32_t floor, std::uint32_t classId) noexcept;
bool IsCheckpointAuraBearer(std::uint32_t floor, std::uint32_t classId) noexcept;
std::uint32_t DungeonSessionSeed() noexcept;
