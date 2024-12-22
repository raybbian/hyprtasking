#pragma once

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/plugins/HookSystem.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

inline HANDLE PHANDLE = nullptr;

inline void failNotification(const std::string &reason) {
    HyprlandAPI::addNotification(PHANDLE, "[Hyprtasking] " + reason,
                                 CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
}

inline void infoNotification(const std::string &message) {
    HyprlandAPI::addNotification(PHANDLE, "[Hyprtasking] " + message,
                                 CHyprColor{0.2, 0.2, 1.0, 1.0}, 5000);
}

extern CFunctionHook *g_pRenderWorkspaceHook;
typedef void (*tRenderWorkspace)(void *, PHLMONITOR, PHLWORKSPACE, timespec *,
                                 const CBox &);
