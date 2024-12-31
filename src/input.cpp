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

    cursor_monitor->changeWorkspace(cursor_workspace, true);
    cursor_workspace->startAnim(true, false, true);
    cursor_workspace->m_bVisible = true;

    const Vector2D workspace_coords =
        cursor_view->global_pos_to_ws_global(mouse_coords, workspace_id);

    g_pPointerManager->warpTo(workspace_coords);
    g_pKeybindManager->changeMouseBindMode(MBIND_MOVE);
    g_pPointerManager->warpTo(mouse_coords);

    const PHLWINDOW dragged_window = g_pInputManager->currentlyDraggedWindow.lock();
    if (dragged_window == nullptr)
        return;
    const Vector2D real_center = cursor_view->ws_global_pos_to_global(
        dragged_window->m_vRealPosition.value() + dragged_window->m_vRealSize.value() / 2.f,
        workspace_id
    );

    // Guide the window to center on the mouse cursor
    dragged_window_offset.setValueAndWarp(real_center - mouse_coords);
    dragged_window_offset = {0, 0};
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
    if (dragged_window == nullptr) {
        g_pKeybindManager->changeMouseBindMode(MBIND_INVALID);
        return;
    }

    // Release on empty dummy workspace, so create and switch to it
    if (cursor_workspace == nullptr && workspace_id != WORKSPACE_INVALID) {
        cursor_workspace = g_pCompositor->createNewWorkspace(workspace_id, cursor_monitor->ID);
        cursor_monitor->changeWorkspace(cursor_workspace);
    } else if (cursor_workspace != nullptr) {
        cursor_monitor->changeWorkspace(cursor_workspace, true);
        cursor_workspace->startAnim(true, false, true);
        cursor_workspace->m_bVisible = true;
    } else {
        // TODO: drop on invalid behavior?
        cursor_workspace = cursor_monitor->activeWorkspace;
    }

    g_pCompositor->moveWindowToWorkspaceSafe(dragged_window, cursor_workspace);

    const Vector2D workspace_coords =
        cursor_view->global_pos_to_ws_global(mouse_coords, cursor_workspace->m_iID);

    // Teleport the tiled dragged window to the mouse cursor on the workspace
    dragged_window->m_vRealPosition.setValueAndWarp(
        workspace_coords - dragged_window->m_vRealSize.value() / 2.
    );

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
    dragged_window_offset.warp();
}
