#pragma once

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/plugins/HookSystem.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

inline HANDLE PHANDLE = nullptr;

extern CFunctionHook *g_pRenderWorkspaceHook;
typedef void (*tRenderWorkspace)(void *, PHLMONITOR, PHLWORKSPACE, timespec *,
                                 const CBox &);
