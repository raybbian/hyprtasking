#include <linux/input-event-codes.h>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/macros.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprland/src/managers/LayoutManager.hpp>
#include <hyprland/src/managers/PointerManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>

#include "manager.hpp"
#include "overview.hpp"

bool HTManager::start_window_drag() {
    const PHLMONITOR cursor_monitor = g_pCompositor->getMonitorFromCursor();
    const PHTVIEW cursor_view = get_view_from_monitor(cursor_monitor);
    if (cursor_monitor == nullptr || cursor_view == nullptr || !cursor_view->is_active()
        || cursor_view->is_closing())
        return false;

    if (!cursor_view->layout->should_manage_mouse()) {
        // hide all views if should not manage mouse
        hide_all_views();
        return false;
    }

    const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
    const WORKSPACEID workspace_id = cursor_view->layout->get_ws_id_from_global(mouse_coords);
    PHLWORKSPACE cursor_workspace = g_pCompositor->getWorkspaceByID(workspace_id);

    // If left click on non-workspace workspace, do nothing
    if (cursor_workspace == nullptr)
        return false;

    cursor_view->act_workspace = cursor_workspace;

    PHLWORKSPACEREF o_workspace = cursor_monitor->activeWorkspace;
    cursor_monitor->changeWorkspace(cursor_workspace, true);

    const Vector2D workspace_coords =
        cursor_view->layout->global_to_local_ws_unscaled(mouse_coords, workspace_id)
        + cursor_monitor->vecPosition;

    g_pPointerManager->warpTo(workspace_coords);
    g_pKeybindManager->changeMouseBindMode(MBIND_MOVE);
    g_pPointerManager->warpTo(mouse_coords);

    const PHLWINDOW dragged_window = g_pInputManager->currentlyDraggedWindow.lock();
    if (dragged_window != nullptr) {
        if (dragged_window->m_bDraggingTiled) {
            const Vector2D pre_pos = cursor_view->layout->local_ws_unscaled_to_global(
                dragged_window->m_vRealPosition.value() - dragged_window->m_pMonitor->vecPosition,
                workspace_id
            );
            const Vector2D post_pos = cursor_view->layout->local_ws_unscaled_to_global(
                dragged_window->m_vRealPosition.goal() - dragged_window->m_pMonitor->vecPosition,
                workspace_id
            );
            const Vector2D mapped_pre_pos =
                (pre_pos - mouse_coords) / cursor_view->layout->drag_window_scale() + mouse_coords;
            const Vector2D mapped_post_pos =
                (post_pos - mouse_coords) / cursor_view->layout->drag_window_scale() + mouse_coords;

            dragged_window->m_vRealPosition.setValueAndWarp(mapped_pre_pos);
            dragged_window->m_vRealPosition = mapped_post_pos;
        } else {
            g_pInputManager->simulateMouseMovement();
        }
    }

    if (o_workspace != nullptr)
        cursor_monitor->changeWorkspace(o_workspace.lock(), true);

    return true;
}

bool HTManager::end_window_drag() {
    const PHLMONITOR cursor_monitor = g_pCompositor->getMonitorFromCursor();
    const PHTVIEW cursor_view = get_view_from_monitor(cursor_monitor);
    if (cursor_monitor == nullptr || cursor_view == nullptr) {
        g_pKeybindManager->changeMouseBindMode(MBIND_INVALID);
        return false;
    }

    // Required if dragging and dropping from active to inactive
    if (!cursor_view->is_active() || cursor_view->is_closing()) {
        g_pKeybindManager->changeMouseBindMode(MBIND_INVALID);
        return false;
    }

    // For linear layout: if dropping on big workspace, just pass on
    if (!cursor_view->layout->should_manage_mouse()) {
        g_pKeybindManager->changeMouseBindMode(MBIND_INVALID);
        return false;
    }

    // If not dragging window or drag is not move, then we just let go (supposed to prevent it
    // from messing up resize on border, but it should be good because above?)
    const PHLWINDOW dragged_window = g_pInputManager->currentlyDraggedWindow.lock();
    if (dragged_window == nullptr || g_pInputManager->dragMode != MBIND_MOVE) {
        g_pKeybindManager->changeMouseBindMode(MBIND_INVALID);
        return false;
    }

    const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
    Vector2D use_mouse_coords = mouse_coords;
    const WORKSPACEID workspace_id = cursor_view->layout->get_ws_id_from_global(mouse_coords);
    PHLWORKSPACE cursor_workspace = g_pCompositor->getWorkspaceByID(workspace_id);

    // Release on empty dummy workspace, so create and switch to it
    if (cursor_workspace == nullptr && workspace_id != WORKSPACE_INVALID) {
        cursor_workspace = g_pCompositor->createNewWorkspace(workspace_id, cursor_monitor->ID);
    } else if (workspace_id == WORKSPACE_INVALID) {
        cursor_workspace = dragged_window->m_pWorkspace;
        // Ensure that the mouse coords are snapped to inside the workspace box itself
        use_mouse_coords = cursor_view->layout->get_global_ws_box(cursor_workspace->m_iID)
                               .closestPoint(use_mouse_coords);

        Debug::log(
            LOG,
            "[Hyprtasking] Dragging to invalid position, snapping to last ws {}",
            cursor_workspace->m_iID
        );
    }

    if (cursor_workspace == nullptr) {
        Debug::log(LOG, "[Hyprtasking] tried to drop on null workspace??");
        g_pKeybindManager->changeMouseBindMode(MBIND_INVALID);
        return false;
    }

    Debug::log(LOG, "[Hyprtasking] trying to drop window on ws {}", cursor_workspace->m_iID);

    cursor_view->act_workspace = cursor_workspace;

    PHLWORKSPACEREF o_workspace = cursor_monitor->activeWorkspace;
    cursor_monitor->changeWorkspace(cursor_workspace, true);

    g_pCompositor->moveWindowToWorkspaceSafe(dragged_window, cursor_workspace);

    const Vector2D workspace_coords =
        cursor_view->layout->global_to_local_ws_unscaled(use_mouse_coords, cursor_workspace->m_iID)
        + cursor_monitor->vecPosition;

    const Vector2D tp_pos = cursor_view->layout->global_to_local_ws_unscaled(
                                (dragged_window->m_vRealPosition.value() - use_mouse_coords)
                                        * cursor_view->layout->drag_window_scale()
                                    + use_mouse_coords,
                                cursor_workspace->m_iID
                            )
        + cursor_monitor->vecPosition;
    dragged_window->m_vRealPosition.setValueAndWarp(tp_pos);

    g_pPointerManager->warpTo(workspace_coords);
    g_pKeybindManager->changeMouseBindMode(MBIND_INVALID);
    g_pPointerManager->warpTo(mouse_coords);

    // otherwise the window leaves blur (?) artifacts on all
    // workspaces
    dragged_window->m_fMovingToWorkspaceAlpha.setValueAndWarp(1.0);

    if (o_workspace != nullptr)
        cursor_monitor->changeWorkspace(o_workspace.lock(), true);

    // Do not return true and cancel the event! Mouse release requires some stuff to be done for
    // floating windows to be unfocused properly
    return false;
}

bool HTManager::exit_to_workspace() {
    const PHTVIEW cursor_view = get_view_from_cursor();
    if (cursor_view == nullptr)
        return false;

    if (!cursor_view->is_active() || !cursor_view->layout->should_manage_mouse())
        return false;

    for (PHTVIEW view : views) {
        if (view == nullptr)
            continue;
        view->hide(true);
    }
    return true;
}

bool HTManager::on_mouse_move() {
    return false;
}

bool HTManager::on_mouse_axis(double delta) {
    const PHTVIEW cursor_view = get_view_from_cursor();
    if (cursor_view == nullptr)
        return false;

    return cursor_view->layout->on_mouse_axis(delta);
}
