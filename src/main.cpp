#include <linux/input-event-codes.h>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/devices/IKeyboard.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/macros.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/managers/PointerManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/plugins/HookSystem.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/plugins/PluginSystem.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprlang.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Vector2D.hpp>

#include <algorithm>

#include "config.hpp"
#include "globals.hpp"
#include "layout/grid.hpp"
#include "overview.hpp"
#include "types.hpp"
#include "workspace.hpp"

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

static SDispatchResult dispatch_if(std::string arg, bool is_active) {
    if (ht_manager == nullptr)
        return {.passEvent = true, .success = false, .error = "ht_manager is null"};
    PHTVIEW cursor_view = ht_manager->get_view_from_cursor();
    if (cursor_view == nullptr)
        return {.passEvent = true, .success = false, .error = "cursor_view is null"};
    if (cursor_view->active != is_active)
        return {.passEvent = true, .success = false, .error = "predicate not met"};

    const auto DISPATCHSTR = arg.substr(0, arg.find_first_of(' '));

    auto DISPATCHARG = std::string();
    if ((int)arg.find_first_of(' ') != -1)
        DISPATCHARG = arg.substr(arg.find_first_of(' ') + 1);

    const auto DISPATCHER = g_pKeybindManager->m_dispatchers.find(DISPATCHSTR);
    if (DISPATCHER == g_pKeybindManager->m_dispatchers.end())
        return {.success = false, .error = "invalid dispatcher"};

    SDispatchResult res = DISPATCHER->second(DISPATCHARG);

    Log::logger->log(
        LOG,
        "[Hyprtasking] passthrough dispatch: {} : {}{}",
        DISPATCHSTR,
        DISPATCHARG,
        res.success ? "" : " -> " + res.error
    );

    return res;
}

static SDispatchResult dispatch_if_not_active(std::string arg) {
    return dispatch_if(arg, false);
}

static SDispatchResult dispatch_if_active(std::string arg) {
    return dispatch_if(arg, true);
}

static SDispatchResult dispatch_toggle_view(std::string arg) {
    if (ht_manager == nullptr)
        return {.success = false, .error = "ht_manager is null"};

    if (arg == "all") {
        if (ht_manager->has_active_view())
            ht_manager->hide_all_views();
        else
            ht_manager->show_all_views();
    } else if (arg == "cursor") {
        if (ht_manager->cursor_view_active())
            ht_manager->hide_all_views();
        else
            ht_manager->show_cursor_view();
    } else {
        return {.success = false, .error = "invalid arg"};
    }
    return {};
}

static SDispatchResult change_layer(std::string arg, bool move_window);
static SDispatchResult dispatch_move_impl(std::string arg, bool move_window);

static SDispatchResult dispatch_move(std::string arg) {
    return dispatch_move_impl(arg, false);
}

static SDispatchResult dispatch_move_window(std::string arg) {
    return dispatch_move_impl(arg, true);
}

static SDispatchResult dispatch_move_impl(std::string arg, bool move_window) {
    if (ht_manager == nullptr)
        return {.success = false, .error = "ht_manager is null"};
    const PHTVIEW cursor_view = ht_manager->get_view_from_cursor();
    if (cursor_view == nullptr)
        return {.success = false, .error = "cursor_view is null"};
    if (arg == "in") {
        return change_layer("-1", move_window);
    }
    if (arg == "out") {
        return change_layer("+1", move_window);
    }
    if (arg != "up" && arg != "down" && arg != "left" && arg != "right")
        return {.success = false, .error = "invalid arg"};
    cursor_view->move(arg, move_window);
    return {};
}

static void set_layer(PHTVIEW view, int new_layer) {
    if (view == nullptr)
        return;

    // Avoid focus loss while a view is closing.
    if (view->closing)
        return;
    Log::logger->log(
        LOG,
        "[Hyprtasking] View \"{}\", previous layer: {}, new: {}",
        view->get_monitor()->m_name,
        view->layout->layer,
        new_layer
    );
    view->layout->layer = new_layer;
}

static bool calculate_target_layer(
    std::string arg,
    int original_layer,
    int effective_layers,
    bool loop_layers,
    int& resulting_layer
) {
    resulting_layer = original_layer;
    if (arg[0] == '+' || arg[0] == '-')
        resulting_layer += std::stoi(arg);
    else
        resulting_layer = std::stoi(arg);

    if (resulting_layer >= 0 && resulting_layer < effective_layers)
        return true;
    if (!loop_layers)
        return false;

    resulting_layer %= effective_layers;
    if (resulting_layer < 0)
        resulting_layer += effective_layers;
    return true;
}

static PHLWORKSPACE find_or_create_target_workspace(
    PHTVIEW cursor_view,
    PHLMONITOR monitor,
    SP<HTLayoutGrid> grid_layout,
    int source_cell,
    int target_slot,
    int resulting_layer,
    int original_layer,
    int cols
) {
    set_layer(cursor_view, resulting_layer);
    cursor_view->layout->build_overview_layout(HT_VIEW_CLOSED);

    WORKSPACEID target_ws_id = cursor_view->layout->get_ws_id_from_xy(source_cell % cols, source_cell / cols);
    PHLWORKSPACE target_workspace = nullptr;

    if (target_ws_id == WORKSPACE_INVALID) {
        target_workspace = create_workspace_for_monitor(monitor);
        if (target_workspace == nullptr) {
            set_layer(cursor_view, original_layer);
            cursor_view->layout->build_overview_layout(HT_VIEW_CLOSED);
            return nullptr;
        }
        target_ws_id = target_workspace->m_id;
    } else {
        target_workspace = cursor_view->layout->get_workspace_from_layout(target_ws_id);
    }

    grid_layout->pin_workspace_to_slot(target_ws_id, target_slot);
    cursor_view->layout->build_overview_layout(HT_VIEW_CLOSED);

    if (target_workspace == nullptr)
        target_workspace = cursor_view->layout->get_workspace_from_layout(target_ws_id);

    if (target_workspace != nullptr)
        return target_workspace;

    set_layer(cursor_view, original_layer);
    cursor_view->layout->build_overview_layout(HT_VIEW_CLOSED);
    return nullptr;
}

static void apply_layer_change(
    PHTVIEW cursor_view,
    PHLMONITOR monitor,
    WORKSPACEID source_ws_id,
    PHLWORKSPACE target_workspace,
    bool move_window
) {
    if (move_window) {
        const PHLWINDOW hovered_window = ht_manager->get_window_from_cursor();
        if (hovered_window != nullptr)
            g_pCompositor->moveWindowToWorkspaceSafe(hovered_window, target_workspace);
    }

    const PHLMONITOR ws_monitor = g_pCompositor->getMonitorFromID(target_workspace->monitorID());
    if (ws_monitor != nullptr)
        ws_monitor->changeWorkspace(target_workspace);
    else
        monitor->changeWorkspace(target_workspace);

    cursor_view->mark_workspace(target_workspace->m_id);

    cursor_view->layout->build_overview_layout(HT_VIEW_CLOSED);
    g_pHyprRenderer->damageMonitor(monitor);
    g_pCompositor->scheduleFrameForMonitor(monitor);

    if (!cursor_view->active)
        cursor_view->layout->on_move(source_ws_id, target_workspace->m_id);
}

static SDispatchResult change_layer(std::string arg, bool move_window) {
    if (ht_manager == nullptr)
        return {.success = false, .error = "ht_manager is null"};
    if (arg.empty())
        return {.success = false, .error = "invalid arg"};

    const PHTVIEW cursor_view = ht_manager->get_view_from_cursor();
    if (cursor_view == nullptr)
        return {.success = false, .error = "cursor_view is null"};

    if (cursor_view->layout->layout_name() != "grid")
        return {.success = false, .error = "layers are only supported in grid layout"};

    const int ROWS = static_cast<Hyprlang::INT>(HTConfig::value("grid:rows"));
    const int COLS = static_cast<Hyprlang::INT>(HTConfig::value("grid:cols"));
    const int LAYERS = static_cast<Hyprlang::INT>(HTConfig::value("grid:layers"));
    const int LOOP_LAYERS = static_cast<Hyprlang::INT>(HTConfig::value("grid:loop_layers"));
    const int ws_per_layer = std::max(1, ROWS * COLS);

    const PHLMONITOR monitor = cursor_view->get_monitor();
    if (monitor == nullptr)
        return {.success = false, .error = "monitor is null"};
    const PHLWORKSPACE active_workspace = monitor->m_activeWorkspace;
    if (active_workspace == nullptr)
        return {.success = false, .error = "active_workspace is null"};
    const WORKSPACEID source_ws_id = active_workspace->m_id;

    if (!cursor_view->active)
        cursor_view->layout->init_position();

    cursor_view->layout->build_overview_layout(HT_VIEW_CLOSED);
    const auto active_it = cursor_view->layout->overview_layout.find(source_ws_id);
    if (active_it == cursor_view->layout->overview_layout.end() && !cursor_view->active)
        return {.success = false, .error = "active workspace not in layout"};

    int source_cell = 0;
    if (active_it != cursor_view->layout->overview_layout.end()) {
        source_cell = active_it->second.y * COLS + active_it->second.x;
    } else {
        const SP<HTLayoutGrid> grid_layout = Hyprutils::Memory::dynamicPointerCast<HTLayoutGrid, HTLayoutBase>(cursor_view->layout);
        if (grid_layout != nullptr)
            source_cell = grid_layout->last_layer_cell;
    }

    const std::vector<PHLWORKSPACE> monitor_workspaces = cursor_view->layout->get_monitor_workspaces();
    if (monitor_workspaces.empty())
        return {};

    const SP<HTLayoutGrid> grid_layout = Hyprutils::Memory::dynamicPointerCast<HTLayoutGrid, HTLayoutBase>(cursor_view->layout);
    if (grid_layout == nullptr)
        return {.success = false, .error = "grid layout is null"};

    grid_layout->last_layer_cell = source_cell;

    const int configured_layers = std::max(1, LAYERS);
    const int needed_layers = std::max(1, (int)(monitor_workspaces.size() + ws_per_layer - 1) / ws_per_layer);
    const int effective_layers = std::max(configured_layers, needed_layers);

    const int original_layer = cursor_view->layout->layer;
    int resulting_layer = original_layer;
    if (!calculate_target_layer(arg, original_layer, effective_layers, LOOP_LAYERS, resulting_layer))
        return {};

    // Preserve the currently visible grid before changing layers, otherwise a
    // rebuild may auto-pack workspaces and make the same cell point elsewhere.
    for (const auto& [ws_id, ws_layout] : cursor_view->layout->overview_layout) {
        const int slot = original_layer * ws_per_layer + ws_layout.y * COLS + ws_layout.x;
        grid_layout->pin_workspace_to_slot(ws_id, slot);
    }

    const int target_slot = resulting_layer * ws_per_layer + source_cell;
    const PHLWORKSPACE target_workspace = find_or_create_target_workspace(
        cursor_view,
        monitor,
        grid_layout,
        source_cell,
        target_slot,
        resulting_layer,
        original_layer,
        COLS
    );
    if (target_workspace == nullptr)
        return {};

    apply_layer_change(cursor_view, monitor, source_ws_id, target_workspace, move_window);
    return {};
}

static SDispatchResult dispatch_setlayer(std::string arg) {
    return change_layer(arg, false);
}

static SDispatchResult dispatch_setlayerwindow(std::string arg) {
    return change_layer(arg, true);
}

static SDispatchResult dispatch_kill_hover(std::string arg) {
    if (ht_manager == nullptr)
        return {.success = false, .error = "ht_manager is null"};

    const PHTVIEW cursor_view = ht_manager->get_view_from_cursor();
    if (cursor_view == nullptr)
        return {.success = false, .error = "cursor_view is null"};
    // Only use actually hovered window when overview is active
    // Use focused otherwise
    const PHLWINDOW hovered_window = ht_manager->get_window_from_cursor(!cursor_view->active);
    if (hovered_window == nullptr)
        return {.success = false, .error = "hovered_window is null"};
    g_pCompositor->closeWindow(hovered_window);
    return {};
}

static void hook_render_workspace(
    void* thisptr,
    PHLMONITOR monitor,
    PHLWORKSPACE workspace,
   const Time::steady_tp& now,
    const CBox& geometry
) {
    if (ht_manager == nullptr) {
        ((render_workspace_t)(render_workspace_hook
                                  ->m_original))(thisptr, monitor, workspace, now, geometry);
        return;
    }
    const PHTVIEW view = ht_manager->get_view_from_monitor(monitor);
    // Some render hooks can fire for monitors that do not have a registered
    // hyprtasking view yet, especially around monitor hotplug/reload.
    if (view != nullptr && ((view->navigating) || ht_manager->has_active_view())) {
        view->layout->render();
    } else {
        ((render_workspace_t)(render_workspace_hook
                                  ->m_original))(thisptr, monitor, workspace, now, geometry);
    }
}

static bool hook_should_render_window(void* thisptr, PHLWINDOW window, PHLMONITOR monitor) {
    bool ori_result =
        ((should_render_window_t)(should_render_window_hook->m_original))(thisptr, window, monitor);
    if (ht_manager == nullptr || !ht_manager->has_active_view())
        return ori_result;
    const PHTVIEW view = ht_manager->get_view_from_monitor(monitor);
    if (view == nullptr)
        return ori_result;
    return view->layout->should_render_window(window);
}

static uint32_t hook_is_solitary_blocked(void* thisptr, bool full) {
    PHTVIEW view = ht_manager->get_view_from_cursor();
    if (view == nullptr) {
        Log::logger->log(Log::ERR, "[Hyprtasking] View is nullptr in hook_is_solitary_blocked");
        // Fall back to Hyprland when there is no cursor view instead of
        // dereferencing nullptr below.
        return (*(origIsSolitaryBlocked)is_solitary_blocked_hook->m_original)(thisptr, full);
    }

    if (view->active || view->navigating) {
        return CMonitor::SC_UNKNOWN;
    }
    return (*(origIsSolitaryBlocked)is_solitary_blocked_hook->m_original)(thisptr, full);
}

static void on_mouse_button(IPointer::SButtonEvent e, Event::SCallbackInfo& info) {
    if (ht_manager == nullptr)
        return;

    const PHTVIEW cursor_view = ht_manager->get_view_from_cursor();
    if (cursor_view == nullptr)
        return;

    const bool pressed = e.state == WL_POINTER_BUTTON_STATE_PRESSED;

    const unsigned int drag_button = static_cast<Hyprlang::INT>(HTConfig::value("drag_button"));
    const unsigned int select_button = static_cast<Hyprlang::INT>(HTConfig::value("select_button"));

    if (pressed && e.button == drag_button) {
        info.cancelled = ht_manager->mark_workspace();
    } else if (!pressed && e.button == drag_button) {
        info.cancelled = false;
    } else if (pressed && e.button == select_button) {
        info.cancelled = ht_manager->exit_to_workspace();
    }
}

static void on_mouse_move(Vector2D c, Event::SCallbackInfo& info) {
    if (ht_manager == nullptr)
        return;
    info.cancelled = ht_manager->on_mouse_move();
}

static void on_mouse_axis(IPointer::SAxisEvent e, Event::SCallbackInfo& info) {
    if (ht_manager == nullptr)
        return;
    info.cancelled = ht_manager->on_mouse_axis(e.delta);
}

static void on_swipe_begin(IPointer::SSwipeBeginEvent e, Event::SCallbackInfo& info) {
    if (ht_manager == nullptr)
        return;
    ht_manager->swipe_start();
}

static void on_swipe_update(IPointer::SSwipeUpdateEvent e, Event::SCallbackInfo& info) {
    if (ht_manager == nullptr)
        return;
    info.cancelled = ht_manager->swipe_update(e);
}

static void on_swipe_end(IPointer::SSwipeEndEvent e, Event::SCallbackInfo& info) {
    if (ht_manager == nullptr)
        return;
    info.cancelled = ht_manager->swipe_end();
}

static void cancel_event(Event::SCallbackInfo& info) {
    if (ht_manager == nullptr || !ht_manager->cursor_view_active())
        return;
    info.cancelled = true;
}

static void notify_config_changes() {
    const int ROWS = static_cast<Hyprlang::INT>(HTConfig::value("rows"));
    if (ROWS != -1) {
        HyprlandAPI::addNotification(
            PHANDLE,
            "[Hyprtasking] plugin:hyprtasking:rows has moved to plugin:hyprtasking:grid:rows in the config.",
            CHyprColor {1.0, 0.2, 0.2, 1.0},
            20000
        );
    }

    CVarList exit_behavior {HTConfig::value_string("exit_behavior"), 0, 's', true};
    if (exit_behavior.size() != 0) {
        HyprlandAPI::addNotification(
            PHANDLE,
            "[Hyprtasking] plugin:hyprtasking:exit_behavior is deprecated. Hyprtasking will always exit to the active workspace, which is changed when interacting with the plugin.",
            CHyprColor {1.0, 0.2, 0.2, 1.0},
            20000
        );
    }
}

static void register_monitors(bool reset_existing_views) {
    if (ht_manager == nullptr)
        return;

    // Reconnect orphaned views after DPMS/monitor reconnect (m_id changes,
    // but m_name is stable across cycles).
    for (const PHLMONITOR& monitor : g_pCompositor->m_monitors) {
        if (monitor->m_transformedSize.x < 1 || monitor->m_transformedSize.y < 1)
            continue;

        for (PHTVIEW view : ht_manager->views) {
            if (view == nullptr)
                continue;
            if (view->get_monitor() != nullptr)
                continue; // still attached to a live monitor

            // View exists but monitor is gone – check if the *same* physical
            // monitor is reconnecting under a new id.  m_name (e.g. "DP-1")
            // is stable across DPMS cycles, unlike m_id.
            if (view->monitor_name == monitor->m_name) {
                view->monitor_id = monitor->m_id;
                view->monitor_name = monitor->m_name;
                view->reset_for_monitor_change();
                Log::logger->log(
                    LOG,
                    "[Hyprtasking] Reconnected view for monitor {} with new id {}",
                    monitor->m_name,
                    monitor->m_id
                );
                break;
            }
        }
    }

    // Purge views whose monitor no longer exists.  Keep views with a known
    // monitor_name around – the monitor may just be asleep (DPMS) and will
    // reconnect later.  This prevents workspace loss across wake cycles.
    std::erase_if(ht_manager->views, [](const PHTVIEW& view) {
        if (view == nullptr)
            return true;
        if (view->get_monitor() != nullptr)
            return false;
        // Don't erase views whose monitor_name might still reconnect.
        return view->monitor_name.empty();
    });

    for (const PHLMONITOR& monitor : g_pCompositor->m_monitors) {
        if (monitor->m_transformedSize.x < 1 || monitor->m_transformedSize.y < 1)
            continue;

        const PHTVIEW view = ht_manager->get_view_from_monitor(monitor);
        if (view != nullptr) {
            if (reset_existing_views)
                view->reset_for_monitor_change();
            else if (!view->active)
                view->layout->init_position();
            continue;
        }

        auto new_view = makeShared<HTView>(monitor->m_id);
        new_view->monitor_name = monitor->m_name;
        ht_manager->views.push_back(new_view);

        Log::logger->log(
            LOG,
            "[Hyprtasking] Registering view for monitor {} with resolution {}x{}",
            monitor->m_description,
            monitor->m_transformedSize.x,
            monitor->m_transformedSize.y
        );
    }
}

static void register_monitors() {
    register_monitors(false);
}

static void resync_monitor_layouts() {
    register_monitors(true);
}

static void on_config_reloaded() {
    notify_config_changes();

    if (ht_manager == nullptr)
        return;

    // re-init scale and offset for inactive views, change layout if changed
    for (PHTVIEW& view : ht_manager->views) {
        if (view == nullptr)
            continue;
        const Hyprlang::STRING new_layout = HTConfig::value_string("layout");
        if (static_cast<Hyprlang::INT>(HTConfig::value("close_overview_on_reload"))
            || view->layout->layout_name() != new_layout) {
            Log::logger->log(LOG, "[Hyprtasking] Closing overview on config reload");
            view->hide(false);
            view->change_layout(new_layout);
        }
    }
}

static void init_functions() {
    bool success = true;

    static auto FNS1 = HyprlandAPI::findFunctionsByName(PHANDLE, "renderWorkspace");
    if (FNS1.empty())
        fail_exit("No renderWorkspace!");
    render_workspace_hook =
        HyprlandAPI::createFunctionHook(PHANDLE, FNS1[0].address, (void*)hook_render_workspace);
    Log::logger->log(LOG, "[Hyprtasking] Attempting hook {}", FNS1[0].signature);
    success = render_workspace_hook->hook();

    static auto FNS2 = HyprlandAPI::findFunctionsByName(
        PHANDLE,
        "_ZN13CHyprRenderer18shouldRenderWindowEN9Hyprutils6Memory14CS"
        "haredPointerIN7Desktop4View7CWindowEEENS2_I8CMonitorEE"
    );
    if (FNS2.empty())
        fail_exit("No shouldRenderWindow");
    should_render_window_hook =
        HyprlandAPI::createFunctionHook(PHANDLE, FNS2[0].address, (void*)hook_should_render_window);
    Log::logger->log(LOG, "[Hyprtasking] Attempting hook {}", FNS2[0].signature);
    success = should_render_window_hook->hook() && success;

    static auto FNS3 = HyprlandAPI::findFunctionsByName(
        PHANDLE,
        "_ZN13CHyprRenderer12renderWindowEN9Hyprutils6Memory14CSha"
        "redPointerIN7Desktop4View7CWindowEEENS2_I8CMonitorEERKNSt"
        "6chrono10time_pointINS9_3_V212steady_clockENS9_8durationI"
        "lSt5ratioILl1ELl1000000000EEEEEEb15eRenderPassModebb"
    );
    if (FNS3.empty())
        fail_exit("No renderWindow");
    render_window = FNS3[0].address;

    static auto FNS4 = HyprlandAPI::findFunctionsByName(PHANDLE, "isSolitaryBlocked");
    if (FNS4.empty())
        fail_exit("No isSolitaryBlocked");

    is_solitary_blocked_hook =
        HyprlandAPI::createFunctionHook(PHANDLE, FNS4[0].address, (void*)hook_is_solitary_blocked);
    Log::logger->log(LOG, "[Hyprtasking] Attempting hook {}", FNS4[0].signature);
    success = is_solitary_blocked_hook->hook() && success;

    if (!success)
        fail_exit("Failed initializing hooks");
}

static void register_callbacks() {
    static auto P1 = Event::bus()->m_events.input.mouse.button.listen(on_mouse_button);
    static auto P2 = Event::bus()->m_events.input.mouse.move.listen(on_mouse_move);
    static auto P3 = Event::bus()->m_events.input.mouse.axis.listen(on_mouse_axis);

    static auto P4 = Event::bus()->m_events.input.touch.down.listen([&] (ITouch::SDownEvent e, Event::SCallbackInfo i) { cancel_event(i); } );
    static auto P5 = Event::bus()->m_events.input.touch.up.listen([&] (ITouch::SUpEvent e, Event::SCallbackInfo i) { cancel_event(i); } );
    static auto P6 = Event::bus()->m_events.input.touch.motion.listen([&] (ITouch::SMotionEvent e, Event::SCallbackInfo i) { cancel_event(i); } );

    static auto P7 = Event::bus()->m_events.gesture.swipe.begin.listen(on_swipe_begin);
    static auto P8 = Event::bus()->m_events.gesture.swipe.update.listen(on_swipe_update);
    static auto P9 = Event::bus()->m_events.gesture.swipe.end.listen(on_swipe_end);

    static auto P10 = Event::bus()->m_events.config.reloaded.listen(on_config_reloaded);
    static auto P11 = Event::bus()->m_events.monitor.added.listen([] (PHLMONITOR monitor) { resync_monitor_layouts(); });
    static auto P12 = Event::bus()->m_events.monitor.removed.listen([] (PHLMONITOR monitor) { resync_monitor_layouts(); });
    static auto P13 = Event::bus()->m_events.monitor.layoutChanged.listen(resync_monitor_layouts);
    static auto P14 = Event::bus()->m_events.workspace.moveToMonitor.listen(
        [] (PHLWORKSPACE workspace, PHLMONITOR monitor) { resync_monitor_layouts(); }
    );
}

static void add_dispatchers() {
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtasking:if_not_active", dispatch_if_not_active);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtasking:if_active", dispatch_if_active);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtasking:toggle", dispatch_toggle_view);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtasking:move", dispatch_move);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtasking:movewindow", dispatch_move_window);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtasking:killhovered", dispatch_kill_hover);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtasking:setlayer", dispatch_setlayer);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtasking:setlayerwindow", dispatch_setlayerwindow);
}

static void init_config() {
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtasking:layout", Hyprlang::STRING {"grid"});

    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtasking:bg_color", Hyprlang::INT {0x000000FF});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtasking:gap_size", Hyprlang::FLOAT {8.f});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtasking:border_size", Hyprlang::FLOAT {4.f});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtasking:exit_on_hovered", Hyprlang::INT {0});
    HyprlandAPI::addConfigValue(
        PHANDLE,
        "plugin:hyprtasking:warp_on_move_window",
        Hyprlang::INT {1}
    );
    HyprlandAPI::addConfigValue(
        PHANDLE,
        "plugin:hyprtasking:close_overview_on_reload",
        Hyprlang::INT {1}
    );

    HyprlandAPI::addConfigValue(
        PHANDLE,
        "plugin:hyprtasking:drag_button",
        Hyprlang::INT {BTN_LEFT}
    );
    HyprlandAPI::addConfigValue(
        PHANDLE,
        "plugin:hyprtasking:select_button",
        Hyprlang::INT {BTN_RIGHT}
    );

    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtasking:gestures:enabled", Hyprlang::INT {1});
    HyprlandAPI::addConfigValue(
        PHANDLE,
        "plugin:hyprtasking:gestures:move_fingers",
        Hyprlang::INT {3}
    );
    HyprlandAPI::addConfigValue(
        PHANDLE,
        "plugin:hyprtasking:gestures:move_distance",
        Hyprlang::FLOAT {300.0}
    );
    HyprlandAPI::addConfigValue(
        PHANDLE,
        "plugin:hyprtasking:gestures:open_fingers",
        Hyprlang::INT {4}
    );
    HyprlandAPI::addConfigValue(
        PHANDLE,
        "plugin:hyprtasking:gestures:open_distance",
        Hyprlang::FLOAT {300.0}
    );
    HyprlandAPI::addConfigValue(
        PHANDLE,
        "plugin:hyprtasking:gestures:open_positive",
        Hyprlang::INT {1}
    );

    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtasking:grid:rows", Hyprlang::INT {3});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtasking:grid:cols", Hyprlang::INT {3});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtasking:grid:layers", Hyprlang::INT {1});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtasking:grid:loop_layers", Hyprlang::INT {1});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtasking:grid:loop", Hyprlang::INT {0});
    HyprlandAPI::addConfigValue(
        PHANDLE,
        "plugin:hyprtasking:grid:gaps_use_aspect_ratio",
        Hyprlang::INT {0}
    );

    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtasking:linear:blur", Hyprlang::INT {1});
    HyprlandAPI::addConfigValue(
        PHANDLE,
        "plugin:hyprtasking:linear:height",
        Hyprlang::FLOAT {300.f}
    );
    HyprlandAPI::addConfigValue(
        PHANDLE,
        "plugin:hyprtasking:linear:scroll_speed",
        Hyprlang::FLOAT {1.f}
    );
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtasking:linear:top", Hyprlang::INT {0});

    // Old config value, warning about updates
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtasking:rows", Hyprlang::INT {-1});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtasking:exit_behavior", Hyprlang::STRING {""});

    HyprlandAPI::reloadConfig();
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string COMPOSITOR_HASH = __hyprland_api_get_hash();
    const std::string CLIENT_HASH = __hyprland_api_get_client_hash();

    if (COMPOSITOR_HASH != CLIENT_HASH)
        fail_exit("Mismatched headers! Can't proceed.");

    if (ht_manager == nullptr)
        ht_manager = std::make_unique<HTManager>();
    else
        ht_manager->reset();

    init_config();
    add_dispatchers();
    register_callbacks();
    init_functions();
    register_monitors();

    Log::logger->log(LOG, "[Hyprtasking] Plugin initialized");

    return {"Hyprtasking", "A workspace management plugin", "raybbian", "0.1"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    Log::logger->log(LOG, "[Hyprtasking] Plugin exiting");

    ht_manager->reset();
}
