#include "overview.hpp"

#include <ctime>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/macros.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/math/Box.hpp>

#include "config.hpp"
#include "layout/grid.hpp"

HTView::HTView(MONITORID in_monitor_id) {
    monitor_id = in_monitor_id;
    active = false;
    closing = false;
    navigating = false;

    layout = makeShared<HTLayoutGrid>(monitor_id);
}

WORKSPACEID HTView::get_exit_workspace_id(bool exit_on_mouse) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr) //???
        return WORKSPACE_INVALID;

    auto try_get_hover_id = [this, &monitor]() {
        const PHLMONITOR cursor_monitor = g_pCompositor->getMonitorFromCursor();
        if (cursor_monitor != monitor)
            return WORKSPACE_INVALID;

        const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
        return layout->get_ws_id_from_global(mouse_coords);
    };

    auto try_get_original_id = [this]() {
        const PHLWORKSPACE workspace = ori_workspace.lock();
        return workspace == nullptr ? WORKSPACE_INVALID : workspace->m_iID;
    };

    if (exit_on_mouse) {
        const WORKSPACEID hover_ws_id = try_get_hover_id();
        if (hover_ws_id != WORKSPACE_INVALID)
            return hover_ws_id;
    }

    CVarList exit_behavior {HTConfig::exit_behavior(), 0, 's', true};
    for (const auto& behavior : exit_behavior) {
        WORKSPACEID switch_to_ws_id = WORKSPACE_INVALID;
        if (behavior == "hovered") {
            switch_to_ws_id = try_get_hover_id();
        } else if (behavior == "original") {
            switch_to_ws_id = try_get_original_id();
        } else if (behavior == "interacted") {
            switch_to_ws_id = monitor->activeWorkspaceID();
        } else {
            Debug::log(WARN, "[Hyprtasking] invalid behavior for exit behavior: {}", behavior);
        }

        if (switch_to_ws_id != WORKSPACE_INVALID)
            return switch_to_ws_id;
    }
    return WORKSPACE_INVALID;
}

void HTView::do_exit_behavior(bool exit_on_mouse) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr) //???
        return;
    const WORKSPACEID ws_id = get_exit_workspace_id(exit_on_mouse);
    PHLWORKSPACE workspace = g_pCompositor->getWorkspaceByID(ws_id);

    if (workspace == nullptr && ws_id != WORKSPACE_INVALID)
        workspace = g_pCompositor->createNewWorkspace(ws_id, monitor->ID);
    if (workspace == nullptr)
        return;

    monitor->changeWorkspace(workspace);
    workspace->m_vRenderOffset.warp();

    // For some reason, this line fixes a bug that happens when you open the overview on one
    // monitor, drag it to another monitor, drag it back to the open overview, and then try to
    // exit or move to a different workspace containing windows (which fails).
    monitor->activeWorkspace = workspace;
}

void HTView::show() {
    if (active)
        return;
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    active = true;
    ori_workspace = monitor->activeWorkspace;

    layout->on_show();

    g_pInputManager->setCursorImageUntilUnset("left_ptr");

    g_pHyprRenderer->damageMonitor(monitor);
    g_pCompositor->scheduleFrameForMonitor(monitor);
}

void HTView::hide(bool exit_on_mouse) {
    if (closing || !active)
        return;
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    do_exit_behavior(exit_on_mouse);

    closing = true;
    ori_workspace.reset();

    layout->on_hide([this](void*) {
        active = false;
        closing = false;
    });

    g_pInputManager->unsetCursorImage();

    g_pHyprRenderer->damageMonitor(monitor);
    g_pCompositor->scheduleFrameForMonitor(monitor);
}

void HTView::move(std::string arg) {
    if (closing)
        return;
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;
    const PHLWORKSPACE active_workspace = monitor->activeWorkspace;
    if (active_workspace == nullptr)
        return;

    layout->build_overview_layout(HT_VIEW_CLOSED);
    const auto ws_layout = layout->overview_layout[active_workspace->m_iID];

    int target_x = ws_layout.x;
    int target_y = ws_layout.y;
    if (arg == "up") {
        target_y--;
    } else if (arg == "down") {
        target_y++;
    } else if (arg == "right") {
        target_x++;
    } else if (arg == "left") {
        target_x--;
    }

    for (const auto [id, other_layout] : layout->overview_layout) {
        if (other_layout.x == target_x && other_layout.y == target_y) {
            PHLWORKSPACE other_workspace = g_pCompositor->getWorkspaceByID(id);
            if (other_workspace == nullptr && id != WORKSPACE_INVALID)
                other_workspace = g_pCompositor->createNewWorkspace(id, monitor->ID);

            if (other_workspace == nullptr)
                break;

            monitor->changeWorkspace(other_workspace);
            other_workspace->m_vRenderOffset.warp();

            const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
            const PHLWINDOW hovered_window = g_pCompositor->vectorToWindowUnified(
                mouse_coords,
                RESERVED_EXTENTS | INPUT_EXTENTS | ALLOW_FLOATING
            );
            if (hovered_window)
                g_pCompositor->focusWindow(hovered_window);

            if (!active) {
                navigating = true;
                layout->on_move(id, [this](void*) { navigating = false; });
            }
            break;
        }
    }
}

PHLMONITOR HTView::get_monitor() {
    const PHLMONITOR monitor = g_pCompositor->getMonitorFromID(monitor_id);
    if (monitor == nullptr)
        Debug::log(WARN, "[Hyprtasking] Returning null monitor from get_monitor!");
    return monitor;
}

bool HTView::is_active() {
    return active;
}

bool HTView::is_closing() {
    return closing;
}

bool HTView::is_navigating() {
    return navigating;
}
