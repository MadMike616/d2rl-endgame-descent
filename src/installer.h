#pragma once

namespace D2RL { struct PluginContext; }

enum class InstallResult { Ready, RestartRequired, Failed };

InstallResult InstallDescentData(const D2RL::PluginContext& context) noexcept;
