#pragma once

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/plugins/HookSystem.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprutils/math/Vector2D.hpp>

inline HANDLE PHANDLE = nullptr;

const size_t ROWS = 2;

inline CFunctionHook *g_pRenderWorkspaceHook = nullptr;
typedef void (*tRenderWorkspace)(void *thisptr, PHLMONITOR pMonitor,
                                 PHLWORKSPACE pWorkspace, timespec *now,
                                 const CBox &geometry);

inline CFunctionHook *g_pGetMouseCoordsInternalHook = nullptr;
typedef Vector2D (*tGetMouseCoordsInternal)(void *thisptr);

inline void *g_pRenderWindow = nullptr;
typedef Vector2D (*tRenderWindow)(void *thisptr, PHLWINDOW pWindow,
                                  PHLMONITOR pMonitor, timespec *time,
                                  bool decorate, eRenderPassMode mode,
                                  bool ignorePosition, bool ignoreAllGeometry);
