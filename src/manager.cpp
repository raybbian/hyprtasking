#include "manager.hpp"

#include <algorithm>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>

#include "overview.hpp"

HTManager::HTManager() {
    swipe_state = HT_SWIPE_NONE;
    swipe_amt = 0.0;
}

PHTVIEW HTManager::get_view_from_monitor(PHLMONITOR monitor) {
    if (monitor == nullptr)
        return nullptr;

    const auto it = std::ranges::find_if(views, [monitor](const PHTVIEW& view) {
        return view != nullptr && view->get_monitor() == monitor;
    });
    return it == views.end() ? nullptr : *it;
}

PHTVIEW HTManager::get_view_from_cursor() {
    return get_view_from_monitor(g_pCompositor->getMonitorFromCursor());
}

PHTVIEW HTManager::get_view_from_id(VIEWID view_id) {
    const auto it = std::ranges::find_if(views, [view_id](const PHTVIEW& view) {
        return view != nullptr && view->monitor_id == view_id;
    });
    return it == views.end() ? nullptr : *it;
}

PHLWINDOW HTManager::get_window_from_cursor(bool return_focused) {
    const PHLMONITOR cursor_monitor = g_pCompositor->getMonitorFromCursor();
    if (cursor_monitor == nullptr)
        return nullptr;

    if (return_focused)
        return cursor_monitor->m_activeWorkspace->getLastFocusedWindow();

    const PHTVIEW cursor_view = get_view_from_monitor(cursor_monitor);
    if (cursor_view == nullptr)
        return nullptr;

    const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();

    if (!cursor_view->active || !cursor_view->layout->should_manage_mouse()) {
        return g_pCompositor->vectorToWindowUnified(
            mouse_coords,
            Desktop::View::RESERVED_EXTENTS | Desktop::View::INPUT_EXTENTS | Desktop::View::ALLOW_FLOATING
        );
    }

    const WORKSPACEID ws_id = cursor_view->layout->get_ws_id_from_global(mouse_coords);
    const PHLWORKSPACE hovered_workspace = cursor_view->layout->get_workspace_from_layout(ws_id);
    if (hovered_workspace == nullptr)
        return nullptr;

    const Vector2D ws_coords = cursor_view->layout->global_to_local_ws_unscaled(mouse_coords, ws_id)
        + cursor_monitor->m_position;

    const PHLWORKSPACEREF o_workspace = cursor_monitor->m_activeWorkspace;
    const PHLMONITOR ws_monitor = g_pCompositor->getMonitorFromID(hovered_workspace->monitorID());
    if (ws_monitor != nullptr)
        ws_monitor->changeWorkspace(hovered_workspace, true);
    else
        cursor_monitor->changeWorkspace(hovered_workspace, true);

    const PHLWINDOW hovered_window = g_pCompositor->vectorToWindowUnified(
        ws_coords,
        Desktop::View::RESERVED_EXTENTS | Desktop::View::INPUT_EXTENTS | Desktop::View::ALLOW_FLOATING
    );

    if (o_workspace != nullptr)
        cursor_monitor->changeWorkspace(o_workspace.lock(), true);

    return hovered_window;
}

void HTManager::show_all_views() {
    for (PHTVIEW view : views) {
        if (view == nullptr)
            continue;
        view->show();
    }
}

void HTManager::hide_all_views() {
    for (PHTVIEW view : views) {
        if (view == nullptr)
            continue;
        view->hide(false);
    }
}

void HTManager::show_cursor_view() {
    const PHTVIEW view = get_view_from_cursor();
    if (view != nullptr)
        view->show();
}

void HTManager::reset() {
    swipe_state = HT_SWIPE_NONE;
    swipe_amt = 0.0;
    views.clear();
}

bool HTManager::has_active_view() {
    return std::ranges::any_of(views, [](const PHTVIEW& view) {
        return view != nullptr && view->active;
    });
}

bool HTManager::cursor_view_active() {
    const PHTVIEW view = get_view_from_cursor();
    if (view == nullptr)
        return false;
    return view->active;
}
