#include <linux/input-event-codes.h>

#include <ctime>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/devices/IKeyboard.hpp>
#include <hyprland/src/macros.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprland/src/managers/LayoutManager.hpp>
#include <hyprland/src/managers/PointerManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/plugins/HookSystem.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/plugins/PluginSystem.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprlang.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Vector2D.hpp>

#include "config.hpp"
#include "globals.hpp"
#include "overview.hpp"
#include "types.hpp"

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

static SDispatchResult dispatch_toggle_view(std::string arg) {
    if (ht_manager == nullptr)
        return {};

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
    }
    return {};
}

static SDispatchResult dispatch_move(std::string arg) {
    if (ht_manager == nullptr)
        return {};
    const PHTVIEW cursor_view = ht_manager->get_view_from_cursor();
    if (cursor_view != nullptr)
        cursor_view->move(arg);
    return {};
}

static SDispatchResult dispatch_kill_hover(std::string arg) {
    if (ht_manager == nullptr)
        return {};
    const PHLWINDOW hovered_window = ht_manager->get_window_from_cursor();
    g_pCompositor->closeWindow(hovered_window);
    return {};
}

static void hook_render_workspace(
    void* thisptr,
    PHLMONITOR monitor,
    PHLWORKSPACE workspace,
    timespec* now,
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
    bool ori_result = ((should_render_window_t)(should_render_window_hook->m_original))(
        thisptr,
        window,
        monitor
    );
    if (ht_manager == nullptr || !ht_manager->has_active_view())
        return ori_result;
    const PHTVIEW view = ht_manager->get_view_from_monitor(monitor);
    if (view == nullptr)
        return ori_result;
    return view->layout->should_render_window(window);
}

static void on_mouse_button(void* thisptr, SCallbackInfo& info, std::any args) {
    if (ht_manager == nullptr)
        return;

    const PHTVIEW cursor_view = ht_manager->get_view_from_cursor();
    if (cursor_view == nullptr)
        return;

    const auto e = std::any_cast<IPointer::SButtonEvent>(args);
    const bool pressed = e.state == WL_POINTER_BUTTON_STATE_PRESSED;

    if (pressed && e.button == BTN_LEFT) {
        info.cancelled = ht_manager->start_window_drag();
    } else if (!pressed && e.button == BTN_LEFT) {
        info.cancelled = ht_manager->end_window_drag();
    } else if (pressed && e.button == BTN_RIGHT) {
        info.cancelled = ht_manager->exit_to_workspace();
    }
}

static void on_mouse_move(void* thisptr, SCallbackInfo& info, std::any args) {
    if (ht_manager == nullptr)
        return;
    ht_manager->on_mouse_move();
}

static void on_mouse_axis(void* thisptr, SCallbackInfo& info, std::any args) {
    if (ht_manager == nullptr)
        return;
    const auto e = std::any_cast<IPointer::SAxisEvent>(
        std::any_cast<std::unordered_map<std::string, std::any>>(args)["event"]
    );
    info.cancelled = ht_manager->on_mouse_axis(e.delta);
}

static void on_swipe_begin(void* thisptr, SCallbackInfo& info, std::any args) {
    if (ht_manager == nullptr)
        return;
    ht_manager->swipe_start();
}

static void on_swipe_update(void* thisptr, SCallbackInfo& info, std::any args) {
    if (ht_manager == nullptr)
        return;
    auto e = std::any_cast<IPointer::SSwipeUpdateEvent>(args);
    info.cancelled = ht_manager->swipe_update(e);
}

static void on_swipe_end(void* thisptr, SCallbackInfo& info, std::any args) {
    if (ht_manager == nullptr)
        return;
    info.cancelled = ht_manager->swipe_end();
}

static void cancel_event(void* thisptr, SCallbackInfo& info, std::any args) {
    if (ht_manager == nullptr || !ht_manager->cursor_view_active())
        return;
    info.cancelled = true;
}

static void notify_config_changes() {
    static long* const* PROWS =
        (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprtasking:rows")
            ->getDataStaticPtr();
    const int ROWS = **PROWS;

    if (ROWS != -1) {
        HyprlandAPI::addNotification(
            PHANDLE,
            "[Hyprtasking] plugin:hyprtasking:rows has moved to plugin:hyprtasking:grid:rows in the config.",
            CHyprColor {1.0, 0.2, 0.2, 1.0},
            20000
        );
    }
}

static void register_monitors() {
    if (ht_manager == nullptr)
        return;
    for (const PHLMONITOR& monitor : g_pCompositor->m_monitors) {
        const PHTVIEW view = ht_manager->get_view_from_monitor(monitor);
        if (view != nullptr) {
            if (!view->active)
                view->layout->init_position();
            continue;
        }
        ht_manager->views.push_back(makeShared<HTView>(monitor->m_id));

        Debug::log(
            LOG,
            "[Hyprtasking] Registering view for monitor {} with resolution {}x{}",
            monitor->m_description,
            monitor->m_transformedSize.x,
            monitor->m_transformedSize.y
        );
    }
}

static void on_config_reloaded(void* thisptr, SCallbackInfo& info, std::any args) {
    notify_config_changes();

    if (ht_manager == nullptr)
        return;

    // re-init scale and offset for inactive views, change layout if changed
    for (PHTVIEW& view : ht_manager->views) {
        if (view == nullptr)
            continue;
        view->hide(false);
        view->change_layout(HTConfig::value<Hyprlang::STRING>("layout"));
    }
}

static void init_functions() {
    bool success = true;

    static auto FNS1 = HyprlandAPI::findFunctionsByName(PHANDLE, "renderWorkspace");
    if (FNS1.empty())
        fail_exit("No renderWorkspace!");
    render_workspace_hook =
        HyprlandAPI::createFunctionHook(PHANDLE, FNS1[0].address, (void*)hook_render_workspace);
    Debug::log(LOG, "[Hyprtasking] Attempting hook {}", FNS1[0].signature);
    success = render_workspace_hook->hook();

    static auto FNS2 = HyprlandAPI::findFunctionsByName(
        PHANDLE,
        "_ZN13CHyprRenderer18shouldRenderWindowEN9Hyprutils6Memory14CS"
        "haredPointerI7CWindowEENS2_I8CMonitorEE"
    );
    if (FNS2.empty())
        fail_exit("No shouldRenderWindow");
    should_render_window_hook =
        HyprlandAPI::createFunctionHook(PHANDLE, FNS2[0].address, (void*)hook_should_render_window);
    Debug::log(LOG, "[Hyprtasking] Attempting hook {}", FNS2[0].signature);
    success = should_render_window_hook->hook() && success;

    static auto FNS3 = HyprlandAPI::findFunctionsByName(PHANDLE, "renderWindow");
    if (FNS3.empty())
        fail_exit("No renderWindow");
    render_window = FNS3[0].address;

    if (!success)
        fail_exit("Failed initializing hooks");
}

static void register_callbacks() {
    static auto P1 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "mouseButton", on_mouse_button);
    static auto P2 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "mouseMove", on_mouse_move);
    static auto P3 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "mouseAxis", on_mouse_axis);

    // TODO: support touch
    static auto P4 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "touchDown", cancel_event);
    static auto P5 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "touchUp", cancel_event);
    static auto P6 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "touchMove", cancel_event);

    static auto P7 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "swipeBegin", on_swipe_begin);
    static auto P8 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "swipeUpdate", on_swipe_update);
    static auto P9 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "swipeEnd", on_swipe_end);

    static auto P10 =
        HyprlandAPI::registerCallbackDynamic(PHANDLE, "configReloaded", on_config_reloaded);

    static auto P11 = HyprlandAPI::registerCallbackDynamic(
        PHANDLE,
        "monitorAdded",
        [&](void* thisptr, SCallbackInfo& info, std::any data) { register_monitors(); }
    );
}

static void add_dispatchers() {
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtasking:toggle", dispatch_toggle_view);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtasking:move", dispatch_move);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtasking:killhovered", dispatch_kill_hover);
}

static void init_config() {
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtasking:layout", Hyprlang::STRING {"grid"});

    // general
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtasking:bg_color", Hyprlang::INT {0x000000FF});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtasking:gap_size", Hyprlang::FLOAT {8.f});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtasking:border_size", Hyprlang::FLOAT {4.f});
    HyprlandAPI::addConfigValue(
        PHANDLE,
        "plugin:hyprtasking:exit_behavior",
        Hyprlang::STRING {"active hovered interacted original"}
    );

    // swipe
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtasking:gestures:enabled", Hyprlang::INT {1});
    HyprlandAPI::addConfigValue(
        PHANDLE,
        "plugin:hyprtasking:gestures:open_fingers",
        Hyprlang::INT {3}
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

    // grid specific
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtasking:grid:rows", Hyprlang::INT {3});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtasking:grid:cols", Hyprlang::INT {3});

    //linear specifig
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

    // Old config value, warning about updates
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtasking:rows", Hyprlang::INT {-1});

    HyprlandAPI::reloadConfig();
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string HASH = __hyprland_api_get_hash();

    if (HASH != GIT_COMMIT_HASH)
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

    Debug::log(LOG, "[Hyprtasking] Plugin initialized");

    return {"Hyprtasking", "A workspace management plugin", "raybbian", "0.1"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    Debug::log(LOG, "[Hyprtasking] Plugin exiting");

    ht_manager->reset();
}
