#include <linux/input-event-codes.h>

#include <array>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/state/GlobalWindowController.hpp>
#include <hyprland/src/desktop/state/ViewState.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/macros.hpp>
#include <hyprland/src/managers/SessionLockManager.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/pointer/PointerManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>

#include "config.hpp"
#include "manager.hpp"
#include "overview.hpp"

bool HTManager::start_window_drag() {
    const auto view = get_view_from_id(press_monitor);
    const auto monitor = view ? view->get_monitor() : nullptr;
    const auto workspace = find_workspace(press_workspace);
    const auto window = press_window.lock();
    if (!monitor || monitor->m_activeSpecialWorkspace || !view->active || view->closing || !workspace
        || workspace->m_monitor.lock() != monitor || !window || !window->mapped()
        || window->m_workspace != workspace || g_layoutManager->dragController()->target())
        return false;

    const Vector2D mouse = g_pInputManager->getMouseCoordsInternal();
    const Vector2D grab = view->layout->global_to_local_ws_unscaled(press_position, press_workspace)
        + monitor->m_position;
    previous_workspace = monitor->m_activeWorkspace;
    drag_source_workspace = workspace;
    original_window_box = window->layoutTarget()->position();
    originally_floating = window->isFloating();
    monitor->changeWorkspace(workspace, true);
    Pointer::mgr()->warpTo(grab);
    g_layoutManager->beginDragTarget(window->layoutTarget(), MBIND_MOVE);
    const bool started = g_layoutManager->dragController()->target() != nullptr;
    Pointer::mgr()->warpTo(mouse);

    if (!started && previous_workspace)
        monitor->changeWorkspace(previous_workspace, true);
    return started;
}

void HTManager::cancel_interaction() {
    if (interaction == Interaction::Idle || interaction == Interaction::CompletingDrop
        || interaction == Interaction::Cancelled)
        return;

    const bool dragging = interaction == Interaction::DraggingWindow;
    interaction = Interaction::Cancelled;
    if (!dragging)
        return;

    const auto view = get_view_from_id(press_monitor);
    const auto monitor = view ? view->get_monitor() : nullptr;
    const auto window = press_window.lock();
    const auto target = g_layoutManager->dragController()->target();
    if (target && target->window() == window) {
        const auto& source = drag_source_workspace;
        if (source && window && window->mapped() && window->m_workspace != source)
            Desktop::globalWindowController()->moveWindowToWorkspace(window, source);
        const Vector2D mouse = g_pInputManager->getMouseCoordsInternal();
        if (monitor && window && window->mapped()) {
            const auto grab = view->layout->global_to_local_ws_unscaled(press_position, press_workspace)
                + monitor->m_position;
            Pointer::mgr()->warpTo(grab);
            target->setPositionGlobal(original_window_box);
        }
        g_layoutManager->endDragTarget();
        if (window && window->mapped() && originally_floating && window->isFloating())
            g_layoutManager->setTargetGeom(original_window_box, window->layoutTarget());
        Pointer::mgr()->warpTo(mouse);
    }
    if (const auto workspace = previous_workspace; monitor && workspace
        && workspace->m_monitor.lock() == monitor)
        monitor->changeWorkspace(workspace, true);
}

bool HTManager::on_mouse_button(unsigned int button, bool pressed) {
    if (interaction != Interaction::Idle) {
        if (button != press_button)
            return false;
        if (pressed)
            return true;

        const auto action = interaction;
        if (action == Interaction::DraggingWindow) {
            const auto destination = get_view_from_cursor();
            const auto position = g_pInputManager->getMouseCoordsInternal();
            const auto monitor = destination ? destination->get_monitor() : nullptr;
            const auto id = destination ? destination->layout->get_ws_id_from_global(position)
                                        : WORKSPACE_INVALID;
            const auto workspace = find_workspace(id);
            if (!g_layoutManager->dragController()->target()
                || !monitor || !destination->active || destination->closing
                || monitor->m_activeSpecialWorkspace || !destination->layout->should_manage_mouse()
                || id == WORKSPACE_INVALID || (workspace && workspace->m_monitor.lock() != monitor)) {
                cancel_interaction();
            } else {
                interaction = Interaction::CompletingDrop;
                end_window_drag();
            }
        } else if (action == Interaction::PointerPressed && !moved_past_threshold(
                       g_pInputManager->getMouseCoordsInternal())) {
            interaction = Interaction::CompletingDrop;
            const auto source = get_view_from_id(press_monitor);
            if (source && source->active && !source->closing) {
                for (const auto& view : views) {
                    const auto monitor = view->get_monitor();
                    if (!view->active || view->closing || !monitor)
                        continue;
                    const auto id = view == source ? press_workspace : get_ws_id(monitor->m_activeWorkspace);
                    view->hide(false, id > 0 ? std::optional{id} : std::nullopt);
                }
            }
        }

        interaction = Interaction::Idle;
        press_window.reset();
        previous_workspace.reset();
        drag_source_workspace.reset();
        return true;
    }

    if (!pressed || g_pSessionLockManager->isSessionLocked())
        return false;

    const auto view = get_view_from_cursor();
    if (!view || !view->active || view->closing)
        return false;

    const bool unified = HTConfig::value<Config::INTEGER>("click_drag_enabled");
    const auto drag_button = HTConfig::value<Config::INTEGER>("drag_button");
    const auto select_button = HTConfig::value<Config::INTEGER>("select_button");
    const auto position = g_pInputManager->getMouseCoordsInternal();
    const auto id = view->layout->get_ws_id_from_global(position);
    if (!unified && button != drag_button && button == select_button) {
        if (!exit_to_workspace())
            return false;
        interaction = Interaction::Cancelled;
        press_button = button;
        return true;
    }
    if (button != (unified ? BTN_LEFT : drag_button))
        return false;
    if (!view->layout->should_manage_mouse()) {
        if (!unified) {
            hide_all_views();
            interaction = Interaction::Cancelled;
            press_button = button;
        }
        return !unified;
    }
    if (id == WORKSPACE_INVALID) {
        // Consume the matching release as well when pressing between tiles.
        interaction = Interaction::Cancelled;
        press_button = button;
        return true;
    }

    press_button = button;
    press_position = position;
    press_monitor = view->monitor_id;
    press_workspace = id;
    press_window = get_window_from_cursor(false);
    const auto threshold = std::max<Config::INTEGER>(0, HTConfig::value<Config::INTEGER>("click_drag_threshold"));
    press_threshold = static_cast<double>(threshold);

    interaction = Interaction::PointerPressed;
    if (!unified)
        interaction = start_window_drag() ? Interaction::DraggingWindow : Interaction::Cancelled;
    return true;
}

bool HTManager::moved_past_threshold(Vector2D position) const {
    return position.distanceSq(press_position) > press_threshold * press_threshold;
}

bool HTManager::end_window_drag() {
    const PHLMONITOR cursor_monitor =
        State::monitorState()->query().vec(g_pInputManager->getMouseCoordsInternal()).run();
    const PHTVIEW cursor_view = get_view_from_monitor(cursor_monitor);
    if (!cursor_view)
        return false;

    const SP<Layout::ITarget> target = g_layoutManager->dragController()->target();
    if (target == nullptr)
        return false;

    // If not dragging window or drag is not move, then we just let go (supposed to prevent it
    // from messing up resize on border, but it should be good because above?)
    const PHLWINDOW dragged_window = target->window();
    if (dragged_window == nullptr || g_layoutManager->dragController()->mode() != MBIND_MOVE) {
        g_layoutManager->endDragTarget();
        return false;
    }

    const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
    const WORKSPACEID workspace_id = cursor_view->layout->get_ws_id_from_global(mouse_coords);
    PHLWORKSPACE cursor_workspace = find_workspace(workspace_id);
    if (!cursor_workspace)
        cursor_workspace = create_workspace(workspace_id, cursor_monitor);
    if (!cursor_workspace || cursor_workspace->m_monitor.lock() != cursor_monitor) {
        interaction = Interaction::DraggingWindow;
        cancel_interaction();
        return false;
    }

    Log::logger->log(
        LOG,
        std::format("[Hyprtasking] trying to drop window on ws {}", get_ws_id(cursor_workspace))
    );

    cursor_monitor->changeWorkspace(cursor_workspace, true);

    Desktop::globalWindowController()->moveWindowToWorkspace(dragged_window, cursor_workspace);
    cursor_workspace->ready();

    // Convert the dragged preview's origin back into the destination layout.
    if (g_layoutManager->dragController()->draggingTiled()) {
        const Vector2D drop_pos = cursor_view->layout->global_to_local_ws_unscaled(
            (dragged_window->positionAnimation()->value() - mouse_coords)
                    * cursor_view->layout->drag_window_scale()
                + mouse_coords,
            get_ws_id(cursor_workspace)
        ) + cursor_monitor->m_position;
        dragged_window->positionAnimation()->setValueAndWarp(drop_pos);
    }

    const Vector2D workspace_coords = cursor_view->layout->global_to_local_ws_unscaled(
                                          mouse_coords,
                                          get_ws_id(cursor_workspace)
                                      )
        + cursor_monitor->m_position;

    Pointer::mgr()->warpTo(workspace_coords);
    g_pInputManager->simulateMouseMovement();
    g_layoutManager->endDragTarget();
    Pointer::mgr()->warpTo(mouse_coords);

    // otherwise the window leaves blur (?) artifacts on all
    // workspaces
    dragged_window->presentation()
        .alpha(Desktop::View::WINDOW_ALPHA_MOVE_TO_WORKSPACE)
        ->setValueAndWarp(1.0);
    dragged_window->presentation()
        .alpha(Desktop::View::WINDOW_ALPHA_MOVE_FROM_WORKSPACE)
        ->setValueAndWarp(1.0);

    return true;
}

bool HTManager::exit_to_workspace() {
    const PHTVIEW cursor_view = get_view_from_cursor();
    if (cursor_view == nullptr)
        return false;

    if (!cursor_view->active || !cursor_view->layout->should_manage_mouse())
        return false;

    for (const auto& view : views) {
        if (view->active && !view->closing)
            view->hide(view == cursor_view);
    }
    return true;
}

bool HTManager::on_key(IKeyboard::SKeyEvent event) {
    if (event.state == WL_KEYBOARD_KEY_STATE_RELEASED && jump_pressed_keys.erase(event.keycode) > 0)
        return true;

    if (event.keycode == KEY_ESC && interaction != Interaction::Idle) {
        if (event.state == WL_KEYBOARD_KEY_STATE_PRESSED) {
            jump_pressed_keys.insert(event.keycode);
            cancel_interaction();
        }
        return true;
    }

    if (!HTConfig::value<Config::INTEGER>("jump:enabled"))
        return false;

    const PHTVIEW cursor_view = get_view_from_cursor();
    if (cursor_view == nullptr || !cursor_view->active)
        return false;

    std::optional<size_t> index;
    bool resolved_keyboard = false;
    for (const auto& keyboard : g_pInputManager->m_keyboards) {
        if (keyboard == nullptr || keyboard->m_xkbState == nullptr
            || !keyboard->getPressed(event.keycode))
            continue;

        resolved_keyboard = true;
        const xkb_keysym_t keysym =
            xkb_keysym_to_lower(xkb_state_key_get_one_sym(keyboard->m_xkbState, event.keycode + 8));
        if (keysym >= XKB_KEY_1 && keysym <= XKB_KEY_9)
            index = keysym - XKB_KEY_1;
        else if (keysym == XKB_KEY_0)
            index = 9;
        else if (keysym >= XKB_KEY_a && keysym <= XKB_KEY_z)
            index = 10 + keysym - XKB_KEY_a;
        break;
    }

    // Physical-key fallback for unusual keyboard implementations without XKB state.
    if (!resolved_keyboard) {
        static constexpr std::array<unsigned int, 36> KEYCODES = {
            KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0, KEY_A, KEY_B,
            KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M, KEY_N,
            KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
        };
        for (size_t i = 0; i < KEYCODES.size(); i++) {
            if (KEYCODES[i] == event.keycode) {
                index = i;
                break;
            }
        }
    }

    if (!index.has_value())
        return false;

    const auto target = cursor_view->layout->jump_target(*index);
    if (!target.has_value())
        return false;

    // Keep consuming the matching release even if the close animation has finished by then.
    jump_pressed_keys.insert(event.keycode);

    // Act only on the initial press.
    if (event.state != WL_KEYBOARD_KEY_STATE_PRESSED || cursor_view->closing)
        return true;

    for (const PHTVIEW& view : views) {
        if (view == nullptr || !view->active)
            continue;
        if (view == cursor_view)
            view->hide(false, *target);
        else
            view->hide(false);
    }
    return true;
}

bool HTManager::on_mouse_move() {
    if (g_pSessionLockManager->isSessionLocked()) {
        cancel_interaction();
        return false;
    }
    if (interaction == Interaction::PointerPressed
        && moved_past_threshold(g_pInputManager->getMouseCoordsInternal())) {
        // Mark the transition before calling Hyprland. Focus changes can
        // synchronously trigger additional pointer events.
        interaction = Interaction::DraggingWindow;
        if (!start_window_drag())
            interaction = Interaction::Cancelled;
    }
    return interaction == Interaction::PointerPressed || interaction == Interaction::Cancelled;
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
    const PHLMONITOR cursor_monitor =
        State::monitorState()->query().vec(g_pInputManager->getMouseCoordsInternal()).run();
    const PHTVIEW cursor_view = get_view_from_monitor(cursor_monitor);
    if (cursor_view == nullptr)
        return false;

    const int ENABLED = HTConfig::value<Config::INTEGER>("gestures:enabled");
    if (!ENABLED)
        return false;

    const unsigned int MOVE_FINGERS = HTConfig::value<Config::INTEGER>("gestures:move_fingers");
    const float OPEN_DISTANCE = HTConfig::value<Config::FLOAT>("gestures:open_distance");
    const unsigned int OPEN_FINGERS = HTConfig::value<Config::INTEGER>("gestures:open_fingers");
    const int OPEN_POSITIVE = HTConfig::value<Config::INTEGER>("gestures:open_positive");

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

                cursor_view->layout->init_position();
                // need to schedule frames for monitor, otherwise the screen doesn't re-render
                g_pHyprRenderer->damageMonitor(cursor_monitor);
                cursor_monitor->scheduleFrame();
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
            const float OPEN_DISTANCE = HTConfig::value<Config::FLOAT>("gestures:open_distance");
            const float swipe_perc = 1.0 - std::clamp(swipe_amt / OPEN_DISTANCE, 0.01f, 1.0f);
            if (swipe_perc >= 0.5) {
                cursor_view->show(false);
            } else {
                cursor_view->hide(false);
            }
            break;
        }
        case HT_SWIPE_MOVE: {
            const WORKSPACEID ws_id = cursor_view->layout->on_move_swipe_end();
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
