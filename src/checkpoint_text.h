#pragma once

#include <cstddef>
#include <cstdint>

const char* SelectCheckpointText(std::uint32_t seed, std::size_t entry, const char* text) noexcept;
