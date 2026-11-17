#pragma once

#include <D2RLPlugin/api.h>

#include <atomic>

enum class HookStatus { Unavailable, Installed, MissingOriginal };

template <typename Function, std::size_t EntrySize>
HookStatus InstallHook(const D2RL::PluginContext* context, const D2RL::MutationService* service,
                       std::uintptr_t rva, const std::uint8_t (&entry)[EntrySize], Function hook,
                       std::atomic<Function>& original) noexcept {
    using namespace D2RL::Mutations;
    const InlineHookRequest request{
        .structSize = InlineHookRequestSize, .flags = 0, .rva = rva, .expected = entry,
        .expectedSize = static_cast<std::uint32_t>(EntrySize), .reserved = 0,
        .target = reinterpret_cast<void*>(hook), .reserved2 = 0,
    };
    TransactionHandle transaction = InvalidTransactionHandle;
    OperationHandle operation = InvalidOperationHandle;
    if (service->beginTransaction(context, &transaction) != Result::Success) {
        return HookStatus::Unavailable;
    }
    CommitResult result{};
    result.structSize = CommitResultSize;
    if (service->stageInlineHook(context, transaction, &request, &operation) != Result::Success
        || service->commit(context, transaction, &result) != Result::Success || result.result != Result::Success) {
        service->cancelTransaction(context, transaction);
        return HookStatus::Unavailable;
    }
    void* trampoline = nullptr;
    if (service->getInlineHookOriginal(context, transaction, operation, &trampoline) != Result::Success
        || trampoline == nullptr) {
        return HookStatus::MissingOriginal;
    }
    original.store(reinterpret_cast<Function>(trampoline), std::memory_order_release);
    return HookStatus::Installed;
}

bool InstallBossRoom(const D2RL::PluginContext* context, const D2RL::MutationService* mutations) noexcept;
