#pragma once

#include <hyprland/src/plugins/HookSystem.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

inline HANDLE PHANDLE = nullptr;

inline CFunctionHook *g_pRenderWorkspaceHook = nullptr;
inline CFunctionHook *g_pShouldRenderWindowHook = nullptr;
inline void *g_pRenderWindow = nullptr;
