#include "dungeon_layout.h"

#include <intrin.h>

namespace {

constexpr std::uintptr_t RelocateRva = 0x491CA0;
constexpr std::uintptr_t StairRelocateReturnRva = 0x4921CD;
constexpr std::uintptr_t RoomLevelRva = 0x2EFC10;
constexpr std::uintptr_t LevelDefRva = 0x32C200;
constexpr std::uintptr_t PlayerDataRva = 0x34B240;
constexpr std::uintptr_t QuestStateRva = 0x325C50;
constexpr std::uintptr_t IsSinglePlayerRva = 0x8D4B0;
constexpr std::uintptr_t SetSelectionRva = 0xF2B40;
constexpr std::uintptr_t ClientQuestRecordRva = 0x2A48778;
constexpr std::uintptr_t PrepareLightsRva = 0xE17F00;
constexpr std::uintptr_t RenderRegistryRva = 0x9124E0;
constexpr std::uintptr_t PointLightTypeRva = 0x799EB0;
constexpr std::uintptr_t GetUnitRoomRva = 0x34B440;
constexpr std::uint32_t TileUnit = 5;
constexpr std::uint32_t BaalQuest = 40;
constexpr std::uint32_t RewardGranted = 0;
constexpr auto Rotw = static_cast<std::uint8_t>(D2RL::DataTables::Bank::Rotw);

using RelocateFn = int(__fastcall*)(void*, void*, void*, int, int, int, int) noexcept;
using RoomLevelFn = std::uint32_t(__fastcall*)(const void*) noexcept;
using LevelDefFn = const void*(__fastcall*)(std::uint8_t, std::uint32_t) noexcept;
using PlayerDataFn = const void*(__fastcall*)(const void*) noexcept;
using QuestStateFn = int(__fastcall*)(const void*, std::uint32_t, std::uint32_t) noexcept;
using IsSinglePlayerFn = bool(__fastcall*)() noexcept;
using GetUnitRoomFn = const void*(__fastcall*)(const void*) noexcept;
using SetSelectionFn = void(__fastcall*)(std::uint32_t, const void*) noexcept;
using PrepareLightsFn = void(__fastcall*)(void*, void*, void*, void*, void*, void*, void*) noexcept;
using RenderRegistryFn = const void*(__fastcall*)() noexcept;
using PointLightTypeFn = std::uint32_t(__fastcall*)() noexcept;

const D2RL::PluginContext* pluginContext = nullptr;
RoomLevelFn roomLevel = nullptr;
LevelDefFn levelDef = nullptr;
PlayerDataFn playerData = nullptr;
QuestStateFn questState = nullptr;
IsSinglePlayerFn isSinglePlayer = nullptr;
GetUnitRoomFn getUnitRoom = nullptr;
RenderRegistryFn renderRegistry = nullptr;
PointLightTypeFn pointLightType = nullptr;
std::atomic<RelocateFn> originalRelocate = nullptr;
std::atomic<SetSelectionFn> originalSetSelection = nullptr;
std::atomic<PrepareLightsFn> originalPrepareLights = nullptr;
std::atomic_bool doorSelected = false;

bool HasBaalReward(const void* record) noexcept {
    return record != nullptr && ReadField<const void*>(record, 0) != nullptr
        && questState(record, BaalQuest, RewardGranted) != 0;
}

bool IsDescentArea(const void* game, std::uint32_t area) noexcept {
    const auto& placement = dungeonPlacement;
    if (game == nullptr || placement.firstArea == 0 || area < placement.firstArea
        || area > placement.firstArea + DungeonFloorCount) {
        return false;
    }
    const auto bank = ReadField<std::uint8_t>(game, 0x106);
    const auto* def = levelDef(bank, area);
    return def != nullptr && ReadField<std::uint32_t>(def, 4) == BaalQuest
        && ReadField<std::uint32_t>(def, 8) == placement.firstLayer + area - placement.firstArea;
}

bool CanEnterDescent(const void* game, const void* player) noexcept {
    if (!isSinglePlayer() || ReadField<std::uint8_t>(game, 0x106) != Rotw
        || ReadField<std::uint8_t>(game, 0x104) != Hell) {
        return false;
    }
    // The native portal handler reads the current difficulty's quest record here.
    const auto* data = playerData(player);
    const auto* record = data == nullptr ? nullptr : ReadField<const void*>(data, 0x40 + Hell * sizeof(void*));
    return HasBaalReward(record);
}

void __fastcall SetSelection(std::uint32_t playerIndex, const void* unit) noexcept {
    const auto original = originalSetSelection.load(std::memory_order_acquire);
    if (original == nullptr) {
        return;
    }
    bool selected = false;
    const auto& placement = dungeonPlacement;
    if (placement.firstArea != 0 && playerIndex == 0 && unit != nullptr
        && ReadField<std::uint32_t>(unit, 0) == TileUnit
        && ReadField<std::uint32_t>(unit, 4) == placement.doorWarp) {
        const auto* room = getUnitRoom(unit);
        const auto bank = ReadField<std::uint8_t>(unit, 0x1BD);
        if (room != nullptr && roomLevel(room) == HarrogathArea && bank == Rotw) {
            const auto* town = levelDef(bank, HarrogathArea);
            const auto slotOffset = placement.doorSlot * sizeof(std::uint32_t);
            if (town != nullptr && ReadField<std::uint32_t>(town, 0x48 + slotOffset) == placement.firstArea
                && ReadField<std::uint32_t>(town, 0x68 + slotOffset) == placement.doorWarp) {
                const auto* drlgRoom = ReadField<const void*>(room, 0x18);
                const auto* level = drlgRoom == nullptr ? nullptr : ReadField<const void*>(drlgRoom, 0x90);
                const auto* drlg = level == nullptr ? nullptr : ReadField<const void*>(level, 0x1C8);
                const auto* clientRecord = reinterpret_cast<const void* const*>(pluginContext->exeBase + ClientQuestRecordRva);
                selected = drlg != nullptr && ReadField<std::uint8_t>(drlg, 0x830) == Hell
                    && isSinglePlayer() && HasBaalReward(*clientRecord);
                if (!selected) {
                    unit = nullptr;
                }
            }
        }
    }
    original(playerIndex, unit);
    if (playerIndex == 0) {
        doorSelected.store(selected, std::memory_order_release);
    }
}

struct PointLightColor {
    float power;
    float red;
    float green;
    float blue;
};

std::byte* FindDoorLight() noexcept {
    // Resolve the same point-light component storage as the native lighting pass.
    const auto* registry = ReadField<const void*>(renderRegistry(), 0x68);
    if (registry == nullptr) {
        return nullptr;
    }
    constexpr std::size_t StorageSlotSize = 0x28;
    const auto begin = ReadField<std::uintptr_t>(registry, 0);
    const auto end = ReadField<std::uintptr_t>(registry, 8);
    const auto type = pointLightType();
    if (begin == 0 || end < begin || type >= (end - begin) / StorageSlotSize) {
        return nullptr;
    }
    const auto* storage = ReadField<const void*>(reinterpret_cast<const void*>(begin + type * StorageSlotSize), 0x20);
    if (storage == nullptr) {
        return nullptr;
    }
    constexpr std::size_t PointLightSize = 0x3C;
    auto* first = ReadField<std::byte*>(storage, 0x38);
    const auto last = ReadField<std::uintptr_t>(storage, 0x40);
    const auto address = reinterpret_cast<std::uintptr_t>(first);
    if (first == nullptr || last < address || (last - address) % PointLightSize != 0
        || (last - address) / PointLightSize > 4096) {
        return nullptr;
    }
    std::byte* door = nullptr;
    for (std::size_t offset = 0; offset < last - address; offset += PointLightSize) {
        auto* light = first + offset;
        // Match the radius and attenuation assigned to the dormant town asset.
        if (ReadField<float>(light, 0x0C) < 0.00001f
            && ReadField<float>(light, 0x1C) == 14.375f && ReadField<float>(light, 0x20) == 3.1875f) {
            if (door != nullptr) {
                return nullptr;
            }
            door = light;
        }
    }
    return door;
}

void __fastcall PrepareLights(void* view, void* lightBuffer, void* gridBuffer, void* settingsBuffer,
                              void* camera, void* graph, void* output) noexcept {
    const auto original = originalPrepareLights.load(std::memory_order_acquire);
    if (original == nullptr) {
        return;
    }
    auto* door = doorSelected.load(std::memory_order_acquire) ? FindDoorLight() : nullptr;
    PointLightColor old{};
    if (door != nullptr) {
        old = ReadField<PointLightColor>(door, 0x0C);
        // The renderer stores power and power-scaled RGB in these four fields.
        constexpr PointLightColor HoverLight{215.0f, 215.0f, 189.0f, 145.0f};
        std::memcpy(door + 0x0C, &HoverLight, sizeof(HoverLight));
    }
    original(view, lightBuffer, gridBuffer, settingsBuffer, camera, graph, output);
    if (door != nullptr) {
        std::memcpy(door + 0x0C, &old, sizeof(old));
    }
}

void __cdecl ClearDoorSelection(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
    doorSelected.store(false, std::memory_order_release);
}

int __fastcall Relocate(void* game, void* unit, void* room, int x, int y,
                       int collisionMode, int movementMode) noexcept {
    const auto original = originalRelocate.load(std::memory_order_acquire);
    if (original == nullptr) {
        return 0;
    }
    const auto area = room == nullptr || unit == nullptr || ReadField<std::uint32_t>(unit, 0) != 0
        ? 0 : roomLevel(room);
    const bool descent = IsDescentArea(game, area);
    if (descent && !CanEnterDescent(game, unit)) {
        D2RL::LogInfoF(pluginContext, "Blocked Descent entry to area %u. Offline Hell Baal completion required.", area);
        return 0;
    }
    return original(game, unit, room, x, y, collisionMode, movementMode);
}

constexpr std::uint8_t RelocateEntry[]{
    0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
    0x48, 0x8D, 0x6C, 0x24, 0xF9, 0x48, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00,
};
constexpr std::uint8_t StairRelocateCall[]{
    0x48, 0x8B, 0xCF, 0x44, 0x89, 0x6C, 0x24, 0x28, 0x44, 0x89, 0x64, 0x24, 0x20,
    0xE8, 0xD3, 0xFA, 0xFF, 0xFF,
};
constexpr std::uint8_t RoomLevelEntry[]{
    0x48, 0x85, 0xC9, 0x75, 0x03, 0x33, 0xC0, 0xC3, 0x48, 0x8B, 0x49, 0x18, 0xE9, 0x9F, 0x13, 0x07, 0x00,
};
constexpr std::uint8_t RoomLevelRead[]{0x48, 0x8B, 0x81, 0x90, 0, 0, 0, 0x8B, 0x80, 0xF8, 0x01, 0, 0, 0xC3};
constexpr std::uint8_t LevelDefEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x55, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x63, 0xDA, 0x0F, 0xB6, 0xF1,
};
constexpr std::uint8_t LevelDefStride[]{0x48, 0x69, 0x44, 0x24, 0x60, 0x9C, 0, 0, 0, 0x48, 0x03, 0x07};
constexpr std::uint8_t PlayerDataEntry[]{0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9};
constexpr std::uint8_t PlayerDataRead[]{0x48, 0x8B, 0x43, 0x10, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3};
constexpr std::uint8_t QuestRecordRead[]{
    0x48, 0x8B, 0x49, 0x10, 0xE8, 0xF6, 0xB9, 0xDB, 0xFF, 0x0F, 0xB6, 0x8F, 0x04, 0x01, 0, 0,
    0x4C, 0x8B, 0x74, 0xC8, 0x40,
};
constexpr std::uint8_t QuestBankRead[]{0x48, 0x8B, 0x08, 0x0F, 0xB6, 0x89, 0x06, 0x01, 0, 0, 0xE8, 0x0D, 0xC9, 0xD9, 0xFF};
constexpr std::uint8_t QuestStateEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20,
    0x41, 0x8B, 0xF0, 0x8B, 0xDA, 0x48, 0x8B, 0xF9,
};
constexpr std::uint8_t QuestStateRead[]{0x48, 0x8B, 0x0F, 0xC1, 0xE3, 0x04, 0x8D, 0x14, 0x1E};
constexpr std::uint8_t IsSinglePlayerEntry[]{0x83, 0x3D, 0x3D, 0x5F, 0x99, 0x02, 0, 0x0F, 0x94, 0xC0, 0xC3};
constexpr std::uint8_t SetSelectionEntry[]{
    0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC, 0x40,
    0x44, 0x8B, 0xF1, 0x48, 0x8B, 0xF2, 0x8B, 0xF9,
};
constexpr std::uint8_t ClientQuestRead[]{
    0x48, 0x8B, 0x0D, 0x51, 0x3B, 0x93, 0x02, 0x45, 0x33, 0xC0, 0x41, 0x8D, 0x50, 0x29,
    0xE8, 0x1D, 0x10, 0x21, 0,
};
constexpr std::uint8_t GetUnitRoomEntry[]{0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x13};
constexpr std::uint8_t TileRoomRead[]{0x48, 0x8B, 0x43, 0x38, 0x48, 0x8B, 0, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3};
constexpr std::uint8_t UnitBankRead[]{0x0F, 0xB6, 0x81, 0xBD, 0x01, 0, 0};
constexpr std::uint8_t ClientDrlgRead[]{0x0F, 0xB6, 0x85, 0x38, 0x08, 0, 0, 0x88, 0x87, 0x30, 0x08, 0, 0};
constexpr std::uint8_t ClientDrlgLevelRead[]{0x48, 0x8B, 0x92, 0xC8, 0x01, 0, 0};
constexpr std::uint8_t SelectTileCall[]{
    0x41, 0x83, 0xF8, 0x03, 0x0F, 0x85, 0xE5, 0x01, 0, 0, 0x84, 0xDB, 0x0F, 0x85, 0xDD, 0x01, 0, 0,
};
constexpr std::uint8_t PrepareLightsEntry[]{
    0x4C, 0x8B, 0xDC, 0x55, 0x53, 0x56, 0x57, 0x41, 0x56, 0x49, 0x8D, 0xAB, 0x38, 0xF8, 0xFF, 0xFF,
    0x48, 0x81, 0xEC, 0xA0, 0x08, 0, 0,
};
constexpr std::uint8_t RenderRegistryEntry[]{
    0x48, 0x83, 0xEC, 0x28, 0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0, 0, 0, 0x8B, 0x0D, 0xD5, 0x1F, 0xCA, 0x02,
};
constexpr std::uint8_t RenderRegistryUse[]{
    0xE8, 0x49, 0xA5, 0xAF, 0xFF, 0x48, 0x8B, 0xC8, 0xE8, 0x31, 0xA5, 0xAF, 0xFF, 0x48, 0x8B, 0xF0,
};
constexpr std::uint8_t RenderRegistryOffset[]{0x48, 0x8D, 0x41, 0x68, 0xC3};
constexpr std::uint8_t PointLightTypeEntry[]{
    0x48, 0x83, 0xEC, 0x28, 0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0, 0, 0, 0x8B, 0x0D, 0x05, 0xA6, 0xE1, 0x02,
};
constexpr std::uint8_t PointLightStorageRead[]{
    0x4C, 0x3B, 0xC2, 0x73, 0x6A, 0x4B, 0x8D, 0x04, 0x80, 0x4D, 0x8B, 0x5C, 0xC3, 0x20,
};
constexpr std::uint8_t PointLightStride[]{
    0x48, 0x8B, 0x5B, 0x08, 0x8B, 0xD6, 0x48, 0x8B, 0xCB, 0xE8, 0x29, 0x0B, 0x3D, 0xFF,
    0x48, 0x6B, 0xF0, 0x3C, 0x48, 0x8B, 0x47, 0x30, 0x48, 0x03, 0x73, 0x38,
};
constexpr std::uint8_t PointLightEndWrite[]{0x48, 0x83, 0x47, 0x08, 0x3C};
constexpr std::uint8_t PointLightColorWrite[]{
    0xF3, 0x0F, 0x11, 0x53, 0x0C, 0x0F, 0x28, 0xC2, 0xF3, 0x41, 0x0F, 0x59, 0x07, 0x0F, 0x28, 0xCA,
    0xF3, 0x41, 0x0F, 0x59, 0x4F, 0x04, 0xF3, 0x41, 0x0F, 0x59, 0x57, 0x08, 0x0F, 0x14, 0xC1, 0xF2, 0x0F, 0x11, 0x43, 0x10,
};

bool InstallDoorHover(const D2RL::PluginContext* context, const D2RL::MutationService* mutations) noexcept {
    if (!context->CheckExpectedBytes(0xF2DE6, SelectTileCall, sizeof(SelectTileCall))
        || !context->CheckExpectedBytes(0x114C20, ClientQuestRead, sizeof(ClientQuestRead))
        || !context->CheckExpectedBytes(GetUnitRoomRva, GetUnitRoomEntry, sizeof(GetUnitRoomEntry))
        || !context->CheckExpectedBytes(0x34B48D, TileRoomRead, sizeof(TileRoomRead))
        || !context->CheckExpectedBytes(0x34A103, UnitBankRead, sizeof(UnitBankRead))
        || !context->CheckExpectedBytes(0x326ED0, ClientDrlgRead, sizeof(ClientDrlgRead))
        || !context->CheckExpectedBytes(0x327EC7, ClientDrlgLevelRead, sizeof(ClientDrlgLevelRead))) {
        context->LogWarn("Descent door hover unavailable. The client selection or quest layout could not be verified.");
        return true;
    }
    getUnitRoom = reinterpret_cast<GetUnitRoomFn>(context->exeBase + GetUnitRoomRva);
    const auto status = InstallHook(context, mutations, SetSelectionRva, SetSelectionEntry, SetSelection, originalSetSelection);
    if (status != HookStatus::Installed) {
        context->LogWarn("Could not install the Descent door selection filter.");
        return status != HookStatus::MissingOriginal;
    }
    using namespace D2RL::Lifecycle;
    const D2RL::LifecycleService* lifecycle = nullptr;
    constexpr auto lifecycleEnd = offsetof(D2RL::LifecycleService, unregisterGameplayEventListener);
    if (context->QueryService(&lifecycle) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasLifecycleServiceField(lifecycle, lifecycleEnd) || !lifecycle->registerGameplayEventListener) {
        context->LogWarn("Descent door light requires the gameplay lifecycle service.");
        return true;
    }
    const GameplayEventListener listener{
        .structSize = GameplayEventListenerSize, .flags = 0, .kind = GameplayEventKind::GameLeft,
        .reserved = 0, .callback = ClearDoorSelection, .userData = nullptr,
    };
    ListenerHandle handle{};
    if (lifecycle->registerGameplayEventListener(context, &listener, &handle) != Result::Success
        || !context->CheckExpectedBytes(RenderRegistryRva, RenderRegistryEntry, sizeof(RenderRegistryEntry))
        || !context->CheckExpectedBytes(0xE17F92, RenderRegistryUse, sizeof(RenderRegistryUse))
        || !context->CheckExpectedBytes(0x9124D0, RenderRegistryOffset, sizeof(RenderRegistryOffset))
        || !context->CheckExpectedBytes(PointLightTypeRva, PointLightTypeEntry, sizeof(PointLightTypeEntry))
        || !context->CheckExpectedBytes(0xE1810F, PointLightStorageRead, sizeof(PointLightStorageRead))
        || !context->CheckExpectedBytes(0xE04D09, PointLightStride, sizeof(PointLightStride))
        || !context->CheckExpectedBytes(0xEE1961, PointLightEndWrite, sizeof(PointLightEndWrite))
        || !context->CheckExpectedBytes(0xEE19D9, PointLightColorWrite, sizeof(PointLightColorWrite))) {
        context->LogWarn("Descent door light unavailable. Could not verify the render lifecycle or point light layout.");
        return true;
    }
    renderRegistry = reinterpret_cast<RenderRegistryFn>(context->exeBase + RenderRegistryRva);
    pointLightType = reinterpret_cast<PointLightTypeFn>(context->exeBase + PointLightTypeRva);
    const auto lightStatus = InstallHook(context, mutations, PrepareLightsRva, PrepareLightsEntry,
                                         PrepareLights, originalPrepareLights);
    if (lightStatus != HookStatus::Installed) {
        context->LogWarn("Could not install the Descent door lighting hook.");
        return lightStatus != HookStatus::MissingOriginal;
    }
    context->LogInfo("Enabled quest-gated Harrogath door hover and lighting.");
    return true;
}

}  // namespace

bool InstallDungeonEntrance(const D2RL::PluginContext* context, const D2RL::MutationService* mutations) noexcept {
    if (!context->CheckExpectedBytes(StairRelocateReturnRva - sizeof(StairRelocateCall), StairRelocateCall, sizeof(StairRelocateCall))
        || !context->CheckExpectedBytes(RoomLevelRva, RoomLevelEntry, sizeof(RoomLevelEntry))
        || !context->CheckExpectedBytes(0x360FC0, RoomLevelRead, sizeof(RoomLevelRead))
        || !context->CheckExpectedBytes(LevelDefRva, LevelDefEntry, sizeof(LevelDefEntry))
        || !context->CheckExpectedBytes(0x32C2CE, LevelDefStride, sizeof(LevelDefStride))
        || !context->CheckExpectedBytes(PlayerDataRva, PlayerDataEntry, sizeof(PlayerDataEntry))
        || !context->CheckExpectedBytes(0x34B27A, PlayerDataRead, sizeof(PlayerDataRead))
        || !context->CheckExpectedBytes(0x58F841, QuestRecordRead, sizeof(QuestRecordRead))
        || !context->CheckExpectedBytes(0x58F8E4, QuestBankRead, sizeof(QuestBankRead))
        || !context->CheckExpectedBytes(QuestStateRva, QuestStateEntry, sizeof(QuestStateEntry))
        || !context->CheckExpectedBytes(0x325C7F, QuestStateRead, sizeof(QuestStateRead))
        || !context->CheckExpectedBytes(IsSinglePlayerRva, IsSinglePlayerEntry, sizeof(IsSinglePlayerEntry))) {
        context->LogError("Descent entrance gate could not verify the native quest and relocation layouts.");
        return false;
    }
    pluginContext = context;
    roomLevel = reinterpret_cast<RoomLevelFn>(context->exeBase + RoomLevelRva);
    levelDef = reinterpret_cast<LevelDefFn>(context->exeBase + LevelDefRva);
    playerData = reinterpret_cast<PlayerDataFn>(context->exeBase + PlayerDataRva);
    questState = reinterpret_cast<QuestStateFn>(context->exeBase + QuestStateRva);
    isSinglePlayer = reinterpret_cast<IsSinglePlayerFn>(context->exeBase + IsSinglePlayerRva);
    if (InstallHook(context, mutations, RelocateRva, RelocateEntry, Relocate, originalRelocate) != HookStatus::Installed) {
        context->LogError("Could not install the Descent entrance gate.");
        return false;
    }
    context->LogInfo("Enabled the Hell Baal completion requirement for Descent.");
    return InstallDoorHover(context, mutations);
}
