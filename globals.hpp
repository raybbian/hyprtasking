#pragma once

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/plugins/HookSystem.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

inline HANDLE PHANDLE = nullptr;

extern CFunctionHook *g_pRenderWorkspaceHook;
typedef void (*tRenderWorkspace)(void *, PHLMONITOR, PHLWORKSPACE, timespec *,
                                 const CBox &);

extern void *g_pRenderLayer;
typedef void (*tRenderLayer)(void *, PHLLS, PHLMONITOR, timespec *, bool);

extern void *g_pRenderWindow;
typedef void (*tRenderWindow)(void *, PHLWINDOW, PHLMONITOR, timespec *, bool,
                              eRenderPassMode, bool, bool);
