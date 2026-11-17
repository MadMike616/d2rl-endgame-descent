#pragma once

#include "dungeon_layout.h"

void EnablePackModifiers(const D2RL::PluginContext* context) noexcept;
void ApplyPackModifier(void* leader, std::uint32_t floor, int monsterId, std::uint32_t packSeed) noexcept;
