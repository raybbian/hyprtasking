#pragma once

#include <hyprland/src/plugins/HookSystem.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include "manager.hpp"

inline HANDLE PHANDLE = nullptr;

inline CFunctionHook* render_workspace_hook = nullptr;
inline CFunctionHook* should_render_window_hook = nullptr;
inline void* render_window = nullptr;

inline std::unique_ptr<HTManager> ht_manager;
