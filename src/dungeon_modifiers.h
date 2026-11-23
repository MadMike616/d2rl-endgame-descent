#pragma once

#include "dungeon_layout.h"

bool EnableMonsterAuraSkills(const D2RL::PluginContext* context) noexcept;
void EnablePackModifiers(const D2RL::PluginContext* context) noexcept;
void ApplyPackModifier(void* leader, std::uint32_t floor, int monsterId, std::uint32_t packSeed) noexcept;
void ApplyCheckpointAura(void* boss, std::uint32_t floor, std::uint32_t sessionSeed) noexcept;
