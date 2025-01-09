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
#include "globals.hpp"
#include "layout/grid.hpp"
#include "layout/linear.hpp"

HTView::HTView(MONITORID in_monitor_id) {
    monitor_id = in_monitor_id;
    active = false;
    closing = false;
    navigating = false;

    change_layout(HTConfig::value<Hyprlang::STRING>("layout"));
}

void HTView::change_layout(const std::string& layout_name) {
    if (layout != nullptr && layout->layout_name() == layout_name) {
        layout->init_position();
        return;
    }

    if (layout_name == "grid") {
        layout = makeShared<HTLayoutGrid>(monitor_id);
    } else if (layout_name == "linear") {
        layout = makeShared<HTLayoutLinear>(monitor_id);
    } else {
        fail_exit(
            "Bad overview layout name {}, supported ones are 'grid' and 'linear'",
            layout_name
        );
    }
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

    auto try_get_interacted_id = [this]() {
        const PHLWORKSPACE workspace = act_workspace.lock();
        return workspace == nullptr ? WORKSPACE_INVALID : workspace->m_iID;
    };

    if (exit_on_mouse) {
        const WORKSPACEID hover_ws_id = try_get_hover_id();
        if (hover_ws_id != WORKSPACE_INVALID)
            return hover_ws_id;
    }

    CVarList exit_behavior {HTConfig::value<Hyprlang::STRING>("exit_behavior"), 0, 's', true};
    for (const auto& behavior : exit_behavior) {
        WORKSPACEID switch_to_ws_id = WORKSPACE_INVALID;
        if (behavior == "hovered") {
            switch_to_ws_id = try_get_hover_id();
        } else if (behavior == "original") {
            switch_to_ws_id = try_get_original_id();
        } else if (behavior == "interacted") {
            switch_to_ws_id = try_get_interacted_id();
        } else if (behavior == "active") {
            switch_to_ws_id = monitor->activeWorkspaceID();
        } else {
            Debug::log(WARN, "[Hyprtasking] invalid behavior for exit behavior: {}", behavior);
        }

        if (switch_to_ws_id != WORKSPACE_INVALID)
            return switch_to_ws_id;
    }
    return monitor->activeWorkspaceID();
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
}

void HTView::show() {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;
    const PHLWORKSPACE active_workspace = monitor->activeWorkspace;
    if (active_workspace == nullptr)
        return;

    active = true;
    closing = false;
    ori_workspace = monitor->activeWorkspace;

    layout->on_show();

    g_pInputManager->setCursorImageUntilUnset("left_ptr");

    g_pHyprRenderer->damageMonitor(monitor);
    g_pCompositor->scheduleFrameForMonitor(monitor);
}

void HTView::hide(bool exit_on_mouse) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;
    const PHLWORKSPACE active_workspace = monitor->activeWorkspace;
    if (active_workspace == nullptr)
        return;

    do_exit_behavior(exit_on_mouse);

    closing = true;
    ori_workspace.reset();

    layout->on_hide([this](auto self) {
        active = false;
        closing = false;
    });

    g_pInputManager->unsetCursorImage();

    g_pHyprRenderer->damageMonitor(monitor);
    g_pCompositor->scheduleFrameForMonitor(monitor);
}

void HTView::move(std::string arg, bool move_window) {
    if (closing)
        return;
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;
    const PHLWORKSPACE active_workspace = monitor->activeWorkspace;
    if (active_workspace == nullptr)
        return;

    PHLWINDOW hovered_window = ht_manager->get_window_from_cursor();

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

    const WORKSPACEID id = layout->get_ws_id_from_xy(target_x, target_y);
    PHLWORKSPACE other_workspace = g_pCompositor->getWorkspaceByID(id);

    if (other_workspace == nullptr && id != WORKSPACE_INVALID)
        other_workspace = g_pCompositor->createNewWorkspace(id, monitor->ID);
    if (other_workspace == nullptr)
        return;

    if (move_window) {
        g_pCompositor->moveWindowToWorkspaceSafe(hovered_window, other_workspace);
    }

    monitor->changeWorkspace(other_workspace);

    navigating = true;
    layout->on_move(active_workspace->m_iID, other_workspace->m_iID, [this](auto self) {
        navigating = false;
    });
}

PHLMONITOR HTView::get_monitor() {
    const PHLMONITOR monitor = g_pCompositor->getMonitorFromID(monitor_id);
    if (monitor == nullptr)
        Debug::log(WARN, "[Hyprtasking] Returning null monitor from get_monitor!");
    return monitor;
}
