#pragma once

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/math/Box.hpp>

typedef void (*render_workspace_t)(
    void* thisptr,
    PHLMONITOR pMonitor,
    PHLWORKSPACE pWorkspace,
    timespec* now,
    const CBox& geometry
);

typedef bool (*should_render_window_t)(void* thisptr, PHLWINDOW pWindow, PHLMONITOR pMonitor);

typedef void (*render_window_t)(
    void* thisptr,
    PHLWINDOW pWindow,
    PHLMONITOR pMonitor,
    timespec* time,
    bool decorate,
    eRenderPassMode mode,
    bool ignorePosition,
    bool ignoreAllGeometry
);

typedef long VIEWID;
