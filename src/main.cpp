#include <linux/input-event-codes.h>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/config/shared/actions/ConfigActions.hpp>
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
#include <hyprland/src/config/values/ConfigValues.hpp>
#include <hyprlang.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Vector2D.hpp>
#include <lua.hpp>

#include "config.hpp"
#include "config/ConfigManager.hpp"
#include "globals.hpp"
#include "layout/grid.hpp"
#include "overview.hpp"
#include "types.hpp"

using namespace Config::Actions;
using namespace Config::Values;

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

#define DISPATCHER(name) static int lua_##name(lua_State* L) { \
    const auto RESULT = dispatch("hyprtasking:"#name " " + std::string(luaL_optstring(L, 1, "")));   \
    if (!RESULT.success) \
        return luaL_error(L, "%s", RESULT.error.c_str()); \
    return 0; \
} static SDispatchResult dispatch_##name(std::string arg)

static SDispatchResult dispatch(std::string arg) {
    const auto DISPATCHSTR = arg.substr(0, arg.find_first_of(' '));

    auto DISPATCHARG = std::string();
    if ((int)arg.find_first_of(' ') != -1)
        DISPATCHARG = arg.substr(arg.find_first_of(' ') + 1);

    const auto DISPATCHER = g_pKeybindManager->m_dispatchers.find(DISPATCHSTR);
    if (DISPATCHER == g_pKeybindManager->m_dispatchers.end())
        return {.success = false, .error = "invalid dispatcher: " + arg};

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

// TODO: remove when hyprlang support is dropped
static SDispatchResult dispatch_if(std::string arg, bool is_active) {
    if (ht_manager == nullptr)
        return {.passEvent = true, .success = false, .error = "ht_manager is null"};
    PHTVIEW cursor_view = ht_manager->get_view_from_cursor();
    if (cursor_view == nullptr)
        return {.passEvent = true, .success = false, .error = "cursor_view is null"};
    // silently exit with no error cuz hyprland
    // does not have support for error silencing on lua side
    if (cursor_view->active != is_active)
        return {};
    return dispatch(arg);
}

static void set_layer(PHTVIEW view, int new_layer) {
    if (view == nullptr)
        return;

    // HACK: Prevent no focus when closing the view
    // Makes layers less responsive and less buggy
    // Ideally we would wait for it to close and then update
    // Or update the destination as the offset is changing
    // If you wanna fix this, then test it
    // on a multimonitor setup with this command:
    //   hyprctl dispatch --batch 'dispatch hyprtasking:setlayer -1;
    //   dispatch hyprtasking:move left;
    //   dispatch hyprtasking:toggle cursor;
    //   dispatch hyprtasking:setlayer -1;
    //   dispatch hyprtasking:toggle cursor;
    //   dispatch hyprtasking:toggle cursor;
    //   dispatch hyprtasking:move down;
    //   dispatch hyprtasking:setlayer +1;
    //   dispatch hyprtasking:toggle cursor'
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

static SDispatchResult change_layer(std::string arg, bool move_window) {
    if (ht_manager == nullptr)
        return {.success = false, .error = "ht_manager is null"};

    const PHTVIEW cursor_view = ht_manager->get_view_from_cursor();
    if (cursor_view == nullptr)
        return {.success = false, .error = "cursor_view is null"};

    if (cursor_view->layout->layout_name() != "grid")
        return {.success = false, .error = "layers are only supported in grid layout"};

    const int LAYERS = HTConfig::value<Config::INTEGER>("grid:layers");
    const int LOOP_LAYERS = HTConfig::value<Config::INTEGER>("grid:loop_layers");
    const int original_layer = cursor_view->layout->layer;

    int resulting_layer = original_layer;
    if (arg[0] == '+' || arg[0] == '-') {
        resulting_layer += std::stoi(arg);
    } else {
        resulting_layer = std::stoi(arg);
    }

    if (resulting_layer < 0 || resulting_layer >= LAYERS) {
        if (!LOOP_LAYERS)
            return {};
        resulting_layer = ((resulting_layer % LAYERS) + LAYERS) % LAYERS;
    }

    const PHLMONITOR monitor = cursor_view->get_monitor();
    if (monitor == nullptr)
        return {.success = false, .error = "monitor is null"};
    const PHLWORKSPACE active_workspace = monitor->m_activeWorkspace;
    if (active_workspace == nullptr)
        return {.success = false, .error = "active_workspace is null"};
    const WORKSPACEID source_ws_id = active_workspace->m_id;

    auto* grid = static_cast<HTLayoutGrid*>(cursor_view->layout.get());
    const auto src_it = grid->cache().find(source_ws_id);
    if (src_it == grid->cache().end())
        return {.success = false, .error = "active workspace not in grid cache"};

    const HTGridSlot src_slot = src_it->second;
    const WORKSPACEID target_ws_id =
        grid->slot_workspace(resulting_layer, src_slot.x, src_slot.y);
    if (target_ws_id == WORKSPACE_INVALID)
        return {.success = false, .error = "target slot has no workspace"};

    set_layer(cursor_view, resulting_layer);
    cursor_view->move_id(target_ws_id, move_window);
    return {};
}

DISPATCHER(if_not_active)  {
    return dispatch_if(arg, false);
}

DISPATCHER(if_active) {
    return dispatch_if(arg, true);
}

DISPATCHER(toggle) {
    if (ht_manager == nullptr)
        return {.success = false, .error = "ht_manager is null"};

    if (arg == "all") {
        if (ht_manager->has_active_view())
            ht_manager->hide_all_views();
        else
            ht_manager->show_all_views();
    } else if (arg == "cursor" || arg == "") {
        if (ht_manager->cursor_view_active())
            ht_manager->hide_all_views();
        else
            ht_manager->show_cursor_view();
    } else {
        return {.success = false, .error = "invalid arg: " + arg};
    }
    return {};
}

DISPATCHER(move) {
    if (ht_manager == nullptr)
        return {.success = false, .error = "ht_manager is null"};
    const PHTVIEW cursor_view = ht_manager->get_view_from_cursor();
    if (cursor_view == nullptr)
        return {.success = false, .error = "cursor_view is null"};
    if (arg == "in") {
        return change_layer("-1", false);
    } if (arg == "out") {
        return change_layer("+1", false);
    }
    cursor_view->move(arg, false);
    return {};
}

DISPATCHER(movewindow) {
    if (ht_manager == nullptr)
        return {.success = false, .error = "ht_manager is null"};
    const PHTVIEW cursor_view = ht_manager->get_view_from_cursor();
    if (cursor_view == nullptr)
        return {.success = false, .error = "cursor_view is null"};
    cursor_view->move(arg, true);
    if (arg == "in") {
        return change_layer("-1", true);
    } if (arg == "out") {
        return change_layer("+1", true);
    }
    return {};
}

DISPATCHER(setlayer) {
    return change_layer(arg, false);
}

DISPATCHER(setlayerwindow) {
    return change_layer(arg, true);
}

// Convert ActionResult to SDispatchResult
static SDispatchResult wrap(ActionResult res) {
    if (!res)
        return {.success = false, .error = res.error().message};
    return {.passEvent = res->passEvent};
}

DISPATCHER(killhovered) {
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

    return wrap(closeWindow());
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
    if ((view != nullptr && view->navigating) || ht_manager->has_active_view()) {
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
        (*(origIsSolitaryBlocked)is_solitary_blocked_hook->m_original)(thisptr, full);
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

    const unsigned int drag_button = HTConfig::value<Config::INTEGER>("drag_button");
    const unsigned int select_button = HTConfig::value<Config::INTEGER>("select_button");

    if (pressed && e.button == drag_button) {
        info.cancelled = ht_manager->start_window_drag();
    } else if (!pressed && e.button == drag_button) {
        info.cancelled = ht_manager->end_window_drag();
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

static void register_monitors() {
    if (ht_manager == nullptr)
        return;
    for (const PHLMONITOR& monitor : g_pCompositor->m_monitors) {
        // Skip monitors that haven't finished initializing
        if (monitor->m_transformedSize.x < 1 || monitor->m_transformedSize.y < 1)
            continue;

        const PHTVIEW view = ht_manager->get_view_from_monitor(monitor);
        if (view != nullptr) {
            if (!view->active)
                view->layout->init_position();
            continue;
        }
        ht_manager->views.push_back(makeShared<HTView>(monitor->m_id));

        Log::logger->log(
            LOG,
            "[Hyprtasking] Registering view for monitor {} with resolution {}x{}",
            monitor->m_description,
            monitor->m_transformedSize.x,
            monitor->m_transformedSize.y
        );
    }
    ht_manager->refresh_all_grid_caches();
}

static void on_monitor_removed(PHLMONITOR monitor) {
    if (ht_manager == nullptr || monitor == nullptr)
        return;
    ht_manager->remove_view_for_monitor_id(monitor->m_id);
    ht_manager->refresh_all_grid_caches();
}

static void on_config_reloaded() {
    if (ht_manager == nullptr)
        return;

    // re-init scale and offset for inactive views, change layout if changed
    for (PHTVIEW& view : ht_manager->views) {
        if (view == nullptr)
            continue;
        const Config::STRING new_layout = HTConfig::value<Config::STRING>("layout");
        if (HTConfig::value<Config::INTEGER>("close_overview_on_reload")
            || view->layout->layout_name() != new_layout) {
            Log::logger->log(LOG, "[Hyprtasking] Closing overview on config reload");
            view->hide(false);
            view->change_layout(new_layout);
        }
    }

    ht_manager->refresh_all_grid_caches();
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

    // make sure this signature has "CMonitor"!
    static auto FNS2 = HyprlandAPI::findFunctionsByName(
        PHANDLE,
        "_ZN6Render13IHyprRenderer18shouldRenderWindowEN9Hyprutils6Memory14CSharedPointerIN7Desktop4View7CWindowEEENS3_I8CMonitorEE"
    );
    if (FNS2.empty())
        fail_exit("No shouldRenderWindow");
    should_render_window_hook =
        HyprlandAPI::createFunctionHook(PHANDLE, FNS2[0].address, (void*)hook_should_render_window);
    Log::logger->log(LOG, "[Hyprtasking] Attempting hook {}", FNS2[0].signature);
    success = should_render_window_hook->hook() && success;

    // Right now (in v0.54.0) there are several "renderWindow" functions
    // This is needed so it won't break on update that adds/removes a
    // function with this name
    // This, however, requires checking for signautre changes
    // Use this command to get the signature:
    // strings /usr/bin/hyprland | grep renderWindow
    static auto FNS3 = HyprlandAPI::findFunctionsByName(
        PHANDLE,
        "_ZN6Render13IHyprRenderer12renderWindowEN9Hyprutils6Memory14CSharedPointerIN7Desktop4View7CWindowEEENS3_I8CMonitorEERKNSt6chrono10time_pointINSA_3_V212steady_clockENSA_8durationIlSt5ratioILl1ELl1000000000EEEEEEbNS_15eRenderPassModeEbb"
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

    // TODO: support touch
    static auto P4 = Event::bus()->m_events.input.touch.down.listen([&] (ITouch::SDownEvent e, Event::SCallbackInfo i) { cancel_event(i); } );
    static auto P5 = Event::bus()->m_events.input.touch.up.listen([&] (ITouch::SUpEvent e, Event::SCallbackInfo i) { cancel_event(i); } );
    static auto P6 = Event::bus()->m_events.input.touch.motion.listen([&] (ITouch::SMotionEvent e, Event::SCallbackInfo i) { cancel_event(i); } );
    // static auto P7 = Event::bus()->m_events.input.touch.cancel.listen([&] (ITouch::SCancelEvent e, Event::SCallbackInfo i) { cancel_event(i); } );


    static auto P7 = Event::bus()->m_events.gesture.swipe.begin.listen(on_swipe_begin);
    static auto P8 = Event::bus()->m_events.gesture.swipe.update.listen(on_swipe_update);
    static auto P9 = Event::bus()->m_events.gesture.swipe.end.listen(on_swipe_end);

    static auto P10 = Event::bus()->m_events.config.reloaded.listen(on_config_reloaded);
    static auto P11 = Event::bus()->m_events.monitor.added.listen(register_monitors);
    static auto P12 = Event::bus()->m_events.monitor.removed.listen(on_monitor_removed);
}


// every dispatcher funciton must have DISPATCHER(name) this to work
#define add_dispatcher(name) { \
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtasking:" #name, dispatch_##name); \
    HyprlandAPI::addLuaFunction(PHANDLE, "hyprtasking", #name, lua_##name); \
}

static int lua_is_active(lua_State* L) {
    if (ht_manager == nullptr) {
        luaL_error(L, "%s", "ht_manager is null");
        return false;
    }
    PHTVIEW cursor_view = ht_manager->get_view_from_cursor();
    if (cursor_view == nullptr) {
        luaL_error(L, "%s", "ht_manager is null");
        return false;
    }
    return cursor_view->active;
}

static void add_dispatchers() {
    add_dispatcher(if_not_active);
    add_dispatcher(if_active);
    add_dispatcher(toggle);
    add_dispatcher(move);
    add_dispatcher(movewindow);
    add_dispatcher(killhovered);
    add_dispatcher(setlayer);
    add_dispatcher(setlayerwindow);
    HyprlandAPI::addLuaFunction(PHANDLE, "hyprtasking", "is_active", lua_is_active); \
}

#define addConfigValue(T, config, descr, value) do { \
    SP<Config::Values::IValue> ivalue = makeShared<T>("plugin:hyprtasking:" config, (descr), (value)); \
    const auto RET = Config::mgr()->registerPluginValue(PHANDLE, ivalue); \
    if (!RET) { \
        Log::logger->log(ERR, "[Hyprtasking] could not register value \"{}\"", ivalue->name()); \
    } \
} while (0)

static void init_config() {
    addConfigValue(CStringValue, "layout", "layout", "grid");

    // general
    addConfigValue(CIntValue, "bg_color", "background color", 0x000000FF);
    addConfigValue(CFloatValue, "gap_size", "gap size", 8.f);

    addConfigValue(CFloatValue, "border_size", "border size", 4.f);
    addConfigValue(CIntValue, "exit_on_hovered", "exit on hovered", 0);
    addConfigValue(CIntValue, "warp_on_move_window", "warp on move window", 1);
    addConfigValue(CIntValue, "close_overview_on_reload", "close overview on reload", 1);

    addConfigValue(CIntValue, "drag_button", "drag button", BTN_LEFT);
    addConfigValue(CIntValue, "select_button", "select button", BTN_RIGHT);

    // swipe
    addConfigValue(CIntValue, "gestures:enabled", "enabled", 1);
    addConfigValue(CIntValue, "gestures:move_fingers", "move fingers", 3);
    addConfigValue(CFloatValue, "gestures:move_distance", "move distance", 300.0);
    addConfigValue(CIntValue, "gestures:open_fingers", "open fingers", 4);
    addConfigValue(CFloatValue, "gestures:open_distance", "open distance", 300.0);
    addConfigValue(CIntValue, "gestures:open_positive", "open positive", 1);

    // grid specific
    addConfigValue(CIntValue, "grid:rows", "rows", 3);
    addConfigValue(CIntValue, "grid:cols", "cols", 3);
    addConfigValue(CIntValue, "grid:layers", "layers", 1);
    addConfigValue(CIntValue, "grid:loop_layers", "loop layers", 1);
    addConfigValue(CIntValue, "grid:loop", "loop", 0);
    addConfigValue(CIntValue, "grid:gaps_use_aspect_ratio", "gaps use aspect ratio", 0);

    //linear specific
    addConfigValue(CIntValue, "linear:blur", "blur", 1);
    addConfigValue(CFloatValue, "linear:height", "height", 300.f);
    addConfigValue(CFloatValue, "linear:scroll_speed", "scroll speed", 1.f);
    addConfigValue(CIntValue, "linear:top", "top", 0);

    // HyprlandAPI::reloadConfig();
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
    // prevent crashes
    ht_manager->hide_all_views();
    ht_manager->reset();
}
