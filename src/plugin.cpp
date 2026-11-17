#include "dungeon_layout.h"
#include "installer.h"

#include <intrin.h>

namespace {

constexpr std::uint32_t NativeSidecarLayerCount = 100;
constexpr std::uintptr_t CurrentLayerRva = 0x2A2CF68;
constexpr std::uintptr_t ReadSidecarRva = 0xD5710;
constexpr std::uintptr_t WriteSidecarRva = 0xD5FE0;
constexpr std::uintptr_t ClearTreeRva = 0xD4D60;

// The read entry includes the RIP-relative reference to the current
// AutomapLayer pointer. The write header starts with its layer ID.
constexpr std::uint8_t ReadEntry[]{
    0x40, 0x55, 0x48, 0x8D, 0xAC, 0x24, 0xF0, 0xFD, 0xFF, 0xFF,
    0x48, 0x81, 0xEC, 0x10, 0x03, 0x00, 0x00, 0x48, 0x8B, 0x05,
    0xA0, 0x5B, 0x8F, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x85,
    0xD0, 0x01, 0x00, 0x00, 0x48, 0x83, 0x3D, 0x2E, 0x78, 0x95, 0x02, 0x00,
};
constexpr std::uint8_t WriteEntry[]{
    0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57,
    0x48, 0x81, 0xEC, 0xD0, 0x01, 0x00, 0x00, 0x48, 0x8B, 0x05, 0xCF,
    0x52, 0x8F, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x84, 0x24, 0xC0, 0x01, 0x00, 0x00,
};
constexpr std::uint8_t ReadIndex[]{
    0x48, 0x63, 0x08, 0x8B, 0x44, 0x8D, 0x40, 0x89, 0x44, 0x24, 0x30, 0x85, 0xC0,
};
constexpr std::uint8_t WriteIndex[]{
    0x48, 0x63, 0x45, 0x00, 0x8B, 0x4C, 0x84, 0x30, 0x89, 0x4C, 0x24, 0x24,
};
constexpr std::uint8_t ClearTreeEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48,
    0x8B, 0x31, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xF6, 0x74, 0x3A,
};
constexpr std::uint8_t SwitchLayer[]{
    0x48, 0x8B, 0x0D, 0xF3, 0xB6, 0x95, 0x02, 0x48, 0x85, 0xC9, 0x74, 0x39,
    0x48, 0x83, 0xC1, 0x08, 0xE8, 0xDD, 0x34, 0x00, 0x00,
    0x48, 0x8B, 0x0D, 0xDE, 0xB6, 0x95, 0x02, 0x48, 0x83, 0xC1, 0x30, 0xE8, 0xCD, 0x34, 0x00, 0x00,
    0x48, 0x8B, 0x0D, 0xCE, 0xB6, 0x95, 0x02, 0x48, 0x83, 0xC1, 0x58, 0xE8, 0xBD, 0x34, 0x00, 0x00,
    0x48, 0x8B, 0x0D, 0xBE, 0xB6, 0x95, 0x02, 0x48, 0x83, 0xE9, 0x80, 0xE8, 0xAD, 0x34, 0x00, 0x00,
    0x48, 0x8B, 0x07, 0x48, 0x89, 0x05, 0xAB, 0xB6, 0x95, 0x02,
};
constexpr std::uint8_t TeardownLayerLoop[]{
    0x4C, 0x8B, 0x3D, 0x92, 0x9C, 0x95, 0x02, 0x4D, 0x85, 0xFF, 0x0F, 0x84, 0x2A, 0x01, 0x00,
    0x00, 0x66, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00, 0x49, 0x8B, 0xF7, 0x4D, 0x8B, 0xBF,
    0xA8, 0x00, 0x00, 0x00,
};

using ReadSidecarFn = void(__fastcall*)(void* path, void* sidecar) noexcept;
using WriteSidecarFn = void(__fastcall*)(const std::uint32_t* header, void* path, void* sidecar) noexcept;
using ClearTreeFn = void(__fastcall*)(void* tree) noexcept;

const D2RL::PluginContext* pluginContext = nullptr;
const std::uint32_t* const* currentLayer = nullptr;
std::atomic<ReadSidecarFn> originalRead = nullptr;
std::atomic<WriteSidecarFn> originalWrite = nullptr;
std::atomic<ClearTreeFn> originalClearTree = nullptr;

void __fastcall ReadSidecar(void* path, void* sidecar) noexcept {
    const auto* layer = *currentLayer;
    if (layer != nullptr && *layer >= NativeSidecarLayerCount) {
        return;
    }
    if (const auto original = originalRead.load(std::memory_order_acquire)) {
        original(path, sidecar);
    }
}

void __fastcall WriteSidecar(const std::uint32_t* header, void* path, void* sidecar) noexcept {
    if (header == nullptr || *header >= NativeSidecarLayerCount) {
        return;
    }
    if (const auto original = originalWrite.load(std::memory_order_acquire)) {
        original(header, path, sidecar);
    }
}

void __fastcall ClearTree(void* tree) noexcept {
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - pluginContext->exeBase;
    // These return addresses belong to the four outgoing trees in SwitchLayer.
    // The native teardown still frees every layer and its trees at game end.
    if (caller == 0xD1883 || caller == 0xD1893 || caller == 0xD18A3 || caller == 0xD18B3) {
        const auto* layer = *currentLayer;
        const auto first = dungeonPlacement.firstLayer;
        if (layer != nullptr && first != 0 && *layer >= first && *layer <= first + DungeonFloorCount) {
            return;
        }
    }
    if (const auto original = originalClearTree.load(std::memory_order_acquire)) {
        original(tree);
    }
}

bool InstallAutomapRetention(const D2RL::PluginContext* context, const D2RL::MutationService* service) noexcept {
    if (!context->CheckExpectedBytes(0xD186E, SwitchLayer, sizeof(SwitchLayer))
        || !context->CheckExpectedBytes(0xD32C7, TeardownLayerLoop, sizeof(TeardownLayerLoop))) {
        context->LogWarn("Dungeon automap retention unavailable. The native layer lifecycle could not be verified.");
        return true;
    }
    const auto status = InstallHook(context, service, ClearTreeRva, ClearTreeEntry, ClearTree, originalClearTree);
    if (status == HookStatus::Installed) {
        context->LogInfo("Enabled in-game dungeon automap retention.");
    } else {
        context->LogError("Could not install in-game dungeon automap retention.");
    }
    return status != HookStatus::MissingOriginal;
}

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .abiVersion = D2RL_PLUGIN_ABI_VERSION,
    .id = "endgame-descent",
    .name = "Endgame Descent",
    .version = "1.0.0",
    .author = "MadMike",
    .description = "Connects the Descent mazes, retains automaps, and places bosses at section ends.",
    .flags = D2RL::PluginFlags::Shared | D2RL::PluginFlags::ModScopedOnly | D2RL::PluginFlags::NativeHooks,
    .reserved = {},
};

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    using namespace D2RL::Mutations;
    if (context == nullptr || context->GetApi() == nullptr
        || context->loadScope != D2RL::LoadScope::Mod || context->exeBase == 0) {
        return false;
    }
    const auto check = D2RL::ApiCheckExpectedBytes(context->GetApi());
    if (check == nullptr || !check(context, 0xD579D, ReadIndex, sizeof(ReadIndex))
        || !check(context, 0xD606C, WriteIndex, sizeof(WriteIndex))) {
        context->LogError("Dungeon automap guard refused the native sidecar layout.");
        return false;
    }
    const D2RL::MutationService* service = nullptr;
    if (context->QueryService(&service) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasMutationServiceField(service, D2RL::MutationServiceRequiredSize)
        || !service->beginTransaction || !service->stageInlineHook || !service->commit
        || !service->getInlineHookOriginal || !service->cancelTransaction) {
        context->LogError("Dungeon automap guard requires MutationService v1.");
        return false;
    }
    if (InstallDescentData(*context) == InstallResult::Failed) return false;

    const InlineHookRequest readHook{
        .structSize = InlineHookRequestSize, .flags = 0, .rva = ReadSidecarRva, .expected = ReadEntry,
        .expectedSize = sizeof(ReadEntry), .reserved = 0, .target = reinterpret_cast<void*>(ReadSidecar), .reserved2 = 0,
    };
    const InlineHookRequest writeHook{
        .structSize = InlineHookRequestSize, .flags = 0, .rva = WriteSidecarRva, .expected = WriteEntry,
        .expectedSize = sizeof(WriteEntry), .reserved = 0, .target = reinterpret_cast<void*>(WriteSidecar), .reserved2 = 0,
    };
    TransactionHandle transaction = InvalidTransactionHandle;
    OperationHandle readOperation = InvalidOperationHandle;
    OperationHandle writeOperation = InvalidOperationHandle;
    if (service->beginTransaction(context, &transaction) != Result::Success) {
        context->LogError("Dungeon automap guard could not begin its hook transaction.");
        return false;
    }
    if (service->stageInlineHook(context, transaction, &readHook, &readOperation) != Result::Success
        || service->stageInlineHook(context, transaction, &writeHook, &writeOperation) != Result::Success) {
        service->cancelTransaction(context, transaction);
        context->LogError("Dungeon automap guard could not stage its native hooks.");
        return false;
    }
    pluginContext = context;
    currentLayer = reinterpret_cast<const std::uint32_t* const*>(context->exeBase + CurrentLayerRva);
    CommitResult result{};
    result.structSize = CommitResultSize;
    if (service->commit(context, transaction, &result) != Result::Success || result.result != Result::Success) {
        service->cancelTransaction(context, transaction);
        context->LogError("Dungeon automap guard could not install both native hooks.");
        return false;
    }
    void* read = nullptr;
    void* write = nullptr;
    if (service->getInlineHookOriginal(context, transaction, readOperation, &read) != Result::Success
        || service->getInlineHookOriginal(context, transaction, writeOperation, &write) != Result::Success
        || read == nullptr || write == nullptr) {
        context->LogError("Dungeon automap guard could not obtain its original game functions.");
        return false;
    }
    originalRead.store(reinterpret_cast<ReadSidecarFn>(read), std::memory_order_release);
    originalWrite.store(reinterpret_cast<WriteSidecarFn>(write), std::memory_order_release);
    context->LogInfo("Dungeon automap guard enabled for layers above 99.");
    return InstallAutomapRetention(context, service)
        && InstallDungeonEntrance(context, service)
        && InstallDungeonLayout(context, service)
        && InstallBossRoom(context, service)
        && InstallDungeonLoot(context, service);
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {}
