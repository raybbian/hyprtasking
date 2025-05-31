#include "manager.hpp"

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
    for (PHTVIEW view : views) {
        if (view == nullptr)
            continue;
        if (view->get_monitor() != monitor)
            continue;
        return view;
    }
    return nullptr;
}

PHTVIEW HTManager::get_view_from_cursor() {
    return get_view_from_monitor(g_pCompositor->getMonitorFromCursor());
}

PHTVIEW HTManager::get_view_from_id(VIEWID view_id) {
    for (PHTVIEW view : views) {
        if (view == nullptr)
            continue;
        if (view->monitor_id != view_id)
            continue;
        return view;
    }
    return nullptr;
}

PHLWINDOW HTManager::get_window_from_cursor() {
    const PHLMONITOR cursor_monitor = g_pCompositor->getMonitorFromCursor();
    const PHTVIEW cursor_view = get_view_from_monitor(cursor_monitor);
    if (cursor_view == nullptr || cursor_monitor == nullptr)
        return nullptr;

    const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();

    if (!cursor_view->active || !cursor_view->layout->should_manage_mouse()) {
        return g_pCompositor->vectorToWindowUnified(
            mouse_coords,
            RESERVED_EXTENTS | INPUT_EXTENTS | ALLOW_FLOATING
        );
    }

    const WORKSPACEID ws_id = cursor_view->layout->get_ws_id_from_global(mouse_coords);
    const PHLWORKSPACE hovered_workspace = g_pCompositor->getWorkspaceByID(ws_id);
    if (hovered_workspace == nullptr)
        return nullptr;

    const Vector2D ws_coords = cursor_view->layout->global_to_local_ws_unscaled(mouse_coords, ws_id)
        + cursor_monitor->m_position;

    const PHLWORKSPACEREF o_workspace = cursor_monitor->m_activeWorkspace;
    cursor_monitor->changeWorkspace(hovered_workspace, true);

    const PHLWINDOW hovered_window = g_pCompositor->vectorToWindowUnified(
        ws_coords,
        RESERVED_EXTENTS | INPUT_EXTENTS | ALLOW_FLOATING
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
    for (const auto& view : views) {
        if (view == nullptr)
            continue;
        if (view->active)
            return true;
    }
    return false;
}

bool HTManager::cursor_view_active() {
    const PHTVIEW view = get_view_from_cursor();
    if (view == nullptr)
        return false;
    return view->active;
}
