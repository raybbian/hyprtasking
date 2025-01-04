#include <linux/input-event-codes.h>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/macros.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprland/src/managers/LayoutManager.hpp>
#include <hyprland/src/managers/PointerManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>

#include "manager.hpp"
#include "overview.hpp"

void HTManager::start_window_drag() {
    const PHLMONITOR cursor_monitor = g_pCompositor->getMonitorFromCursor();
    const PHTVIEW cursor_view = get_view_from_monitor(cursor_monitor);
    if (cursor_monitor == nullptr || cursor_view == nullptr || cursor_view->is_closing()
        || !cursor_view->is_active())
        return;

    const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
    const WORKSPACEID workspace_id = cursor_view->get_ws_id_from_global(mouse_coords);
    PHLWORKSPACE cursor_workspace = g_pCompositor->getWorkspaceByID(workspace_id);

    // If left click on non-workspace workspace, do nothing
    if (cursor_workspace == nullptr)
        return;

    cursor_monitor->changeWorkspace(cursor_workspace);
    cursor_workspace->startAnim(true, false, true);

    const Vector2D workspace_coords =
        cursor_view->global_to_local_ws_unscaled(mouse_coords, workspace_id)
        + cursor_monitor->vecPosition;

    g_pPointerManager->warpTo(workspace_coords);
    g_pKeybindManager->changeMouseBindMode(MBIND_MOVE);
    g_pPointerManager->warpTo(mouse_coords);

    const PHLWINDOW dragged_window = g_pInputManager->currentlyDraggedWindow.lock();
    if (dragged_window == nullptr)
        return;

    if (dragged_window->m_bDraggingTiled) {
        const Vector2D pre_pos = cursor_view->local_ws_unscaled_to_global(
            dragged_window->m_vRealPosition.value() - dragged_window->m_pMonitor->vecPosition,
            workspace_id
        );
        const Vector2D post_pos = cursor_view->local_ws_unscaled_to_global(
            dragged_window->m_vRealPosition.goal() - dragged_window->m_pMonitor->vecPosition,
            workspace_id
        );
        const Vector2D mapped_pre_pos =
            (pre_pos - mouse_coords) / cursor_view->scale.value() + mouse_coords;
        const Vector2D mapped_post_pos =
            (post_pos - mouse_coords) / cursor_view->scale.value() + mouse_coords;

        dragged_window->m_vRealPosition.setValueAndWarp(mapped_pre_pos);
        dragged_window->m_vRealPosition = mapped_post_pos;
    } else {
        g_pInputManager->simulateMouseMovement();
    }
}

void HTManager::end_window_drag() {
    const PHLMONITOR cursor_monitor = g_pCompositor->getMonitorFromCursor();
    const PHTVIEW cursor_view = get_view_from_monitor(cursor_monitor);
    if (cursor_monitor == nullptr || cursor_view == nullptr)
        return;

    const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
    const WORKSPACEID workspace_id = cursor_view->get_ws_id_from_global(mouse_coords);
    PHLWORKSPACE cursor_workspace = g_pCompositor->getWorkspaceByID(workspace_id);

    const PHLWINDOW dragged_window = g_pInputManager->currentlyDraggedWindow.lock();
    if (dragged_window == nullptr || g_pInputManager->dragMode != MBIND_MOVE) {
        g_pKeybindManager->changeMouseBindMode(MBIND_INVALID);
        return;
    }

    // Release on empty dummy workspace, so create and switch to it
    if (cursor_workspace == nullptr && workspace_id != WORKSPACE_INVALID) {
        cursor_workspace = g_pCompositor->createNewWorkspace(workspace_id, cursor_monitor->ID);
    } else if (workspace_id == WORKSPACE_INVALID) {
        cursor_workspace = dragged_window->m_pWorkspace;
    }

    if (cursor_workspace != nullptr) {
        cursor_monitor->changeWorkspace(cursor_workspace);
        cursor_workspace->startAnim(true, false, true);
    } else {
        // TODO: drop on invalid behavior?
        cursor_workspace = cursor_monitor->activeWorkspace;
    }

    g_pCompositor->moveWindowToWorkspaceSafe(dragged_window, cursor_workspace);

    const Vector2D workspace_coords =
        cursor_view->global_to_local_ws_unscaled(mouse_coords, cursor_workspace->m_iID)
        + cursor_monitor->vecPosition;

    const Vector2D tp_pos =
        cursor_view->global_to_local_ws_unscaled(
            (dragged_window->m_vRealPosition.value() - mouse_coords) * cursor_view->scale.value()
                + mouse_coords,
            workspace_id
        )
        + cursor_monitor->vecPosition;

    dragged_window->m_vRealPosition.setValueAndWarp(tp_pos);

    g_pPointerManager->warpTo(workspace_coords);
    g_pKeybindManager->changeMouseBindMode(MBIND_INVALID);
    g_pPointerManager->warpTo(mouse_coords);

    // otherwise the window leaves blur (?) artifacts on all
    // workspaces
    dragged_window->m_fMovingToWorkspaceAlpha.setValueAndWarp(1.0);
}

void HTManager::exit_to_workspace() {
    for (PHTVIEW view : views) {
        if (view == nullptr)
            continue;
        // Prefer hover over anything else if we close with click
        view->do_exit_behavior(true);
        view->hide();
    }
}

void HTManager::on_mouse_move() {
    ;
}
