#include <linux/input-event-codes.h>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/macros.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprland/src/managers/LayoutManager.hpp>
#include <hyprland/src/managers/PointerManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>

#include "config.hpp"
#include "manager.hpp"
#include "overview.hpp"

bool HTManager::start_window_drag() {
    const PHLMONITOR cursor_monitor = g_pCompositor->getMonitorFromCursor();
    const PHTVIEW cursor_view = get_view_from_monitor(cursor_monitor);
    if (cursor_monitor == nullptr || cursor_view == nullptr || !cursor_view->active
        || cursor_view->closing)
        return false;

    if (!cursor_view->layout->should_manage_mouse()) {
        // hide all views if should not manage mouse but active
        hide_all_views();
        return true;
    }

    const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
    const WORKSPACEID workspace_id = cursor_view->layout->get_ws_id_from_global(mouse_coords);
    PHLWORKSPACE cursor_workspace = g_pCompositor->getWorkspaceByID(workspace_id);

    // If left click on non-workspace workspace, do nothing
    if (cursor_workspace == nullptr)
        return false;

    // PHLWORKSPACEREF o_workspace = cursor_monitor->m_activeWorkspace;
    cursor_monitor->changeWorkspace(cursor_workspace, true);

    const Vector2D workspace_coords =
        cursor_view->layout->global_to_local_ws_unscaled(mouse_coords, workspace_id)
        + cursor_monitor->m_position;

    g_pPointerManager->warpTo(workspace_coords);
    g_pKeybindManager->changeMouseBindMode(MBIND_MOVE);
    g_pPointerManager->warpTo(mouse_coords);

    const PHLWINDOW dragged_window = g_pInputManager->m_currentlyDraggedWindow.lock();
    if (dragged_window != nullptr) {
        if (dragged_window->m_draggingTiled) {
            const Vector2D pre_pos = cursor_view->layout->local_ws_unscaled_to_global(
                dragged_window->m_realPosition->value() - dragged_window->m_monitor->m_position,
                workspace_id
            );
            const Vector2D post_pos = cursor_view->layout->local_ws_unscaled_to_global(
                dragged_window->m_realPosition->goal() - dragged_window->m_monitor->m_position,
                workspace_id
            );
            const Vector2D mapped_pre_pos =
                (pre_pos - mouse_coords) / cursor_view->layout->drag_window_scale() + mouse_coords;
            const Vector2D mapped_post_pos =
                (post_pos - mouse_coords) / cursor_view->layout->drag_window_scale() + mouse_coords;

            dragged_window->m_realPosition->setValueAndWarp(mapped_pre_pos);
            *dragged_window->m_realPosition = mapped_post_pos;
        } else {
            g_pInputManager->simulateMouseMovement();
        }
    }

    // if (o_workspace != nullptr)
    //     cursor_monitor->changeWorkspace(o_workspace.lock(), true);

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
    if (!cursor_view->active || cursor_view->closing) {
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
    const PHLWINDOW dragged_window = g_pInputManager->m_currentlyDraggedWindow.lock();
    if (dragged_window == nullptr || g_pInputManager->m_dragMode != MBIND_MOVE) {
        g_pKeybindManager->changeMouseBindMode(MBIND_INVALID);
        return false;
    }

    const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
    Vector2D use_mouse_coords = mouse_coords;
    const WORKSPACEID workspace_id = cursor_view->layout->get_ws_id_from_global(mouse_coords);
    PHLWORKSPACE cursor_workspace = g_pCompositor->getWorkspaceByID(workspace_id);

    // Release on empty dummy workspace, so create and switch to it
    if (cursor_workspace == nullptr && workspace_id != WORKSPACE_INVALID) {
        cursor_workspace = g_pCompositor->createNewWorkspace(workspace_id, cursor_monitor->m_id);
    } else if (workspace_id == WORKSPACE_INVALID) {
        cursor_workspace = dragged_window->m_workspace;
        // Ensure that the mouse coords are snapped to inside the workspace box itself
        use_mouse_coords = cursor_view->layout->get_global_ws_box(cursor_workspace->m_id)
                               .closestPoint(use_mouse_coords);

        Debug::log(
            LOG,
            "[Hyprtasking] Dragging to invalid position, snapping to last ws {}",
            cursor_workspace->m_id
        );
    }

    if (cursor_workspace == nullptr) {
        Debug::log(LOG, "[Hyprtasking] tried to drop on null workspace??");
        g_pKeybindManager->changeMouseBindMode(MBIND_INVALID);
        return false;
    }

    Debug::log(LOG, "[Hyprtasking] trying to drop window on ws {}", cursor_workspace->m_id);

    // PHLWORKSPACEREF o_workspace = cursor_monitor->m_activeWorkspace;
    cursor_monitor->changeWorkspace(cursor_workspace, true);

    g_pCompositor->moveWindowToWorkspaceSafe(dragged_window, cursor_workspace);

    const Vector2D workspace_coords =
        cursor_view->layout->global_to_local_ws_unscaled(use_mouse_coords, cursor_workspace->m_id)
        + cursor_monitor->m_position;

    const Vector2D tp_pos = cursor_view->layout->global_to_local_ws_unscaled(
                                (dragged_window->m_realPosition->value() - use_mouse_coords)
                                        * cursor_view->layout->drag_window_scale()
                                    + use_mouse_coords,
                                cursor_workspace->m_id
                            )
        + cursor_monitor->m_position;
    dragged_window->m_realPosition->setValueAndWarp(tp_pos);

    g_pPointerManager->warpTo(workspace_coords);
    g_pKeybindManager->changeMouseBindMode(MBIND_INVALID);
    g_pPointerManager->warpTo(mouse_coords);

    // otherwise the window leaves blur (?) artifacts on all
    // workspaces
    dragged_window->m_movingToWorkspaceAlpha->setValueAndWarp(1.0);
    dragged_window->m_movingFromWorkspaceAlpha->setValueAndWarp(1.0);

    // if (o_workspace != nullptr)
    //     cursor_monitor->changeWorkspace(o_workspace.lock(), true);

    // Do not return true and cancel the event! Mouse release requires some stuff to be done for
    // floating windows to be unfocused properly
    return false;
}

bool HTManager::exit_to_workspace() {
    const PHTVIEW cursor_view = get_view_from_cursor();
    if (cursor_view == nullptr)
        return false;

    if (!cursor_view->active || !cursor_view->layout->should_manage_mouse())
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

void HTManager::swipe_start() {
    swipe_state = HT_SWIPE_NONE;
    swipe_amt = 0.0;
}

bool HTManager::swipe_update(IPointer::SSwipeUpdateEvent e) {
    const PHTVIEW cursor_view = get_view_from_cursor();
    if (cursor_view == nullptr)
        return false;

    const int ENABLED = HTConfig::value<Hyprlang::INT>("gestures:enabled");
    if (!ENABLED)
        return false;

    const int MOVE_FINGERS = HTConfig::value<Hyprlang::INT>("gestures:move_fingers");
    const float OPEN_DISTANCE = HTConfig::value<Hyprlang::FLOAT>("gestures:open_distance");
    const int OPEN_FINGERS = HTConfig::value<Hyprlang::INT>("gestures:open_fingers");
    const int OPEN_POSITIVE = HTConfig::value<Hyprlang::INT>("gestures:open_positive");

    bool res = false;
    char swipe_direction = 0;
    if (std::abs(e.delta.x) > std::abs(e.delta.y)) {
        swipe_direction = 'h';
    } else if (std::abs(e.delta.y) > std::abs(e.delta.x)) {
        swipe_direction = 'v';
    }

    if (e.fingers == OPEN_FINGERS) {
        if (cursor_view->active || swipe_state == HT_SWIPE_OPEN)
            res = true;

        const float deltaY = OPEN_POSITIVE ? e.delta.y : -e.delta.y;

        if (swipe_state != HT_SWIPE_OPEN) {
            if (swipe_direction != 'v' || cursor_view->closing) {
                return res;
            } else if (!cursor_view->active && deltaY <= 0) {
                cursor_view->show();
                swipe_state = HT_SWIPE_OPEN;
                swipe_amt = OPEN_DISTANCE;
            } else if (cursor_view->active && deltaY > 0) {
                cursor_view->hide(false);
                swipe_state = HT_SWIPE_OPEN;
                swipe_amt = 0.0;
            }
        }

        if (swipe_state == HT_SWIPE_OPEN) {
            swipe_amt += deltaY;
            const float swipe_perc = 1.0 - std::clamp(swipe_amt / OPEN_DISTANCE, 0.01f, 1.0f);
            cursor_view->layout->close_open_lerp(swipe_perc);
        }
    } else if (e.fingers == MOVE_FINGERS) {
        if (swipe_state == HT_SWIPE_MOVE)
            res = true;

        if (swipe_state != HT_SWIPE_MOVE) {
            if (cursor_view->active) {
                return res;
            } else {
                swipe_state = HT_SWIPE_MOVE;
                cursor_view->navigating = true;
            }
        }

        if (swipe_state == HT_SWIPE_MOVE) {
            cursor_view->layout->on_move_swipe(e.delta);
        }
    }
    return res;
}

bool HTManager::swipe_end() {
    const PHTVIEW cursor_view = get_view_from_cursor();
    if (cursor_view == nullptr || swipe_state == HT_SWIPE_NONE)
        return false;

    switch (swipe_state) {
        case HT_SWIPE_OPEN: {
            const float OPEN_DISTANCE = HTConfig::value<Hyprlang::FLOAT>("gestures:open_distance");
            const float swipe_perc = 1.0 - std::clamp(swipe_amt / OPEN_DISTANCE, 0.01f, 1.0f);
            if (swipe_perc >= 0.5) {
                cursor_view->show();
            } else {
                cursor_view->hide(false);
            }
            break;
        }
        case HT_SWIPE_MOVE: {
            // this sets navigating to false
            const WORKSPACEID ws_id = cursor_view->layout->on_move_swipe_end();
            Debug::log(LOG, "[Hyprtasking] Move swipe snapping to ws {}", ws_id);
            cursor_view->move_id(ws_id, false);
            break;
        }
        case HT_SWIPE_NONE:
            break;
    }

    swipe_state = HT_SWIPE_NONE;
    swipe_amt = 0.0;
    return true;
}
