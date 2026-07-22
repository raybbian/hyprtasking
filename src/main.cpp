#include <linux/input-event-codes.h>

#include <cmath>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/config/shared/actions/ConfigActions.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/devices/IKeyboard.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/macros.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/pointer/PointerManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/plugins/HookSystem.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/plugins/PluginSystem.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/state/MonitorState.hpp>
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

#define DISPATCHER(name) \
static SDispatchResult dispatch_##name(std::string arg); \
static int lua_##name(lua_State* L) { \
    const auto RESULT = dispatch_##name(luaL_optstring(L, 1, ""));   \
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
    const PHTVIEW cursor_view = ht_manager->get_view_from_cursor();
    if (cursor_view == nullptr)
        return {.passEvent = true, .success = false, .error = "cursor_view is null"};
    if (cursor_view->active != is_active) {
        switch (Config::mgr()->type()) {
            // silently exit with no error cuz hyprland
            // does not have support for error silencing on lua side
            case Config::CONFIG_LUA:
                return {};
            case Config::CONFIG_LEGACY:
                return {.passEvent = true, .success = false, .error = "predicate not met"};;
            default:
                return {.success = false, .error = "what config did vaxry cook again??"};
        }
    }
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

    return wrap(closeWindow(hovered_window));
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

typedef void (*render_texture_t)(
    void* thisptr,
    SP<Render::ITexture> tex,
    const CBox& box,
    Render::GL::CHyprOpenGLImpl::STextureRenderData data
);

// True while a tile / dragged-window is being rendered through an active renderModif.
static bool render_modif_scaled() {
    auto& render_modif = g_pHyprRenderer->m_renderData.renderModif;
    return render_modif.enabled && !render_modif.modifs.empty();
}

// True only while hyprtasking is itself driving a scaled render (overview open, or a
// view animating open/closed on this monitor). Native renderModif paths -- workspace
// slides, special workspace, etc. -- are left on Hyprland's normal rendering so the
// fixes below never perturb them.
static bool ht_scaled_render() {
    if (ht_manager == nullptr || !render_modif_scaled())
        return false;
    if (ht_manager->has_active_view())
        return true;
    const auto monitor = g_pHyprRenderer->m_renderData.pMonitor.lock();
    if (monitor == nullptr)
        return false;
    const PHTVIEW view = ht_manager->get_view_from_monitor(monitor);
    return view != nullptr && view->navigating;
}

// Hyprland applies the active renderModif to a texture's quad, but NOT to any of the
// regions it derives the per-surface scissor from. renderTextureInternal scissors to
// m_renderData.clipBox / data.clipRegion when either is set, else to data.damage --
// all in the window's UNtransformed coordinates (and data.damage is only the damaged
// slice of the frame). Under hyprtasking's scaled overview / dragged-window renderModif
// the renderModif already confines each quad to its tile, so the scissor only ever cuts
// contents off at the tile edges. Drop the clip regions and scissor to the whole monitor
// so scaled surfaces are never clipped. Gated on an active renderModif, so normal desktop
// rendering (and its damage tracking) is untouched.
static void hook_render_texture(
    void* thisptr,
    SP<Render::ITexture> tex,
    const CBox& box,
    Render::GL::CHyprOpenGLImpl::STextureRenderData data
) {
    auto& render_data = g_pHyprRenderer->m_renderData;
    auto& render_modif = render_data.renderModif;

    if (!ht_scaled_render() || render_data.pMonitor == nullptr) {
        ((render_texture_t)(render_texture_hook->m_original))(thisptr, tex, box, data);
        return;
    }

    CRegion full_damage;
    full_damage = CBox {
        0, 0, render_data.pMonitor->m_transformedSize.x, render_data.pMonitor->m_transformedSize.y
    };
    data.damage = &full_damage;
    data.clipRegion = {};
    const CBox saved_clip_box = render_data.clipBox;
    render_data.clipBox = CBox {};

    if (data.blur) {
        // The blur backdrop derives both its quad position and its sample UVs from
        // `box`, but Hyprland only applies the renderModif to the quad -- the UVs (and,
        // on the optimized path, the backdrop quad itself) stay untransformed, so the
        // blur lands in the wrong place. Pre-bake the transform into the box and disable
        // the renderModif so the quad and its UVs agree on the tile. The blur source is
        // handled separately by hook_blur_optimizations.
        CBox tbox = box;
        render_modif.applyToBox(tbox);
        const auto saved_modif = render_modif;
        render_modif.enabled = false;
        ((render_texture_t)(render_texture_hook->m_original))(thisptr, tex, tbox, data);
        render_modif = saved_modif;
    } else {
        ((render_texture_t)(render_texture_hook->m_original))(thisptr, tex, box, data);
    }

    render_data.clipBox = saved_clip_box;
}

typedef void (*render_border_t)(
    void* thisptr,
    const CBox& box,
    const Config::CGradientValueData& grad,
    Render::GL::CHyprOpenGLImpl::SBorderRenderData data
);
typedef void (*render_border2_t)(
    void* thisptr,
    const CBox& box,
    const Config::CGradientValueData& grad1,
    const Config::CGradientValueData& grad2,
    float lerp,
    Render::GL::CHyprOpenGLImpl::SBorderRenderData data
);

// renderBorder builds its border ring from `m_renderData.damage ∩ applyToBox(box)`
// (transformed) but then subtracts `box` (UNtransformed) and scissors with the result.
// Under a scaled overview the untransformed interior swallows the whole transformed ring,
// so no window border draws. Pre-bake the transform into the box and pre-scale the border
// size, then disable the renderModif, so every coordinate renderBorder touches lives in
// the same (already-scaled) space. Shared by both renderBorder overloads.
template <typename Fn>
static void render_border_scaled(
    CBox& box,
    Render::GL::CHyprOpenGLImpl::SBorderRenderData& data,
    Fn&& call_original
) {
    auto& render_modif = g_pHyprRenderer->m_renderData.renderModif;
    if (!ht_scaled_render()) {
        call_original();
        return;
    }
    render_modif.applyToBox(box);
    data.borderSize = std::round(data.borderSize * render_modif.combinedScale());
    const auto saved_modif = render_modif;
    render_modif.enabled = false;
    call_original();
    render_modif = saved_modif;
}

static void hook_render_border(
    void* thisptr,
    const CBox& box,
    const Config::CGradientValueData& grad,
    Render::GL::CHyprOpenGLImpl::SBorderRenderData data
) {
    CBox tbox = box;
    render_border_scaled(tbox, data, [&] {
        ((render_border_t)(render_border_hook->m_original))(thisptr, tbox, grad, data);
    });
}

static void hook_render_border2(
    void* thisptr,
    const CBox& box,
    const Config::CGradientValueData& grad1,
    const Config::CGradientValueData& grad2,
    float lerp,
    Render::GL::CHyprOpenGLImpl::SBorderRenderData data
) {
    CBox tbox = box;
    render_border_scaled(tbox, data, [&] {
        ((render_border2_t)(render_border2_hook->m_original))(thisptr, tbox, grad1, grad2, lerp, data);
    });
}

typedef bool (*blur_optimizations_t)(void* thisptr, PHLLS pLayer, PHLWINDOW pWindow);

// The optimized blur path samples the precomputed monitor blur framebuffer, which holds
// the pre-overview desktop and is sampled at the wrong place once tiles are scaled. Force
// the fresh path during a scaled render so each window's blur is taken from the current
// framebuffer (the overview as drawn so far), clipped to its tile.
static bool hook_blur_optimizations(void* thisptr, PHLLS pLayer, PHLWINDOW pWindow) {
    if (ht_scaled_render())
        return false;
    return ((blur_optimizations_t)(blur_optimizations_hook->m_original))(thisptr, pLayer, pWindow);
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
    // No manager/view for the cursor monitor (e.g. during teardown): defer to
    // Hyprland. Falling through here would dereference a null view.
    PHTVIEW view = ht_manager == nullptr ? nullptr : ht_manager->get_view_from_cursor();
    if (view == nullptr)
        return (*(origIsSolitaryBlocked)is_solitary_blocked_hook->m_original)(thisptr, full);

    if (view->active || view->navigating) {
        return Monitor::CMonitor::SC_UNKNOWN;
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

static void on_key(IKeyboard::SKeyEvent e, Event::SCallbackInfo& info) {
    if (ht_manager == nullptr)
        return;
    info.cancelled = ht_manager->on_key(e);
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
    for (const PHLMONITOR& monitor : State::monitorState()->monitors()) {
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

    // Specific renderTexture overload taking STextureRenderData (several functions
    // share the "renderTexture" name). Used to keep the per-surface scissor in sync
    // with the active renderModif so scaled overview renders aren't culled.
    static auto FNS_RT = HyprlandAPI::findFunctionsByName(
        PHANDLE,
        "_ZN6Render2GL15CHyprOpenGLImpl13renderTextureEN9Hyprutils6Memory14CSharedPointerINS_8ITextureEEERKNS2_4Math4CBoxENS1_18STextureRenderDataE"
    );
    if (FNS_RT.empty())
        fail_exit("No renderTexture");
    render_texture_hook =
        HyprlandAPI::createFunctionHook(PHANDLE, FNS_RT[0].address, (void*)hook_render_texture);
    Log::logger->log(LOG, "[Hyprtasking] Attempting hook {}", FNS_RT[0].signature);
    success = render_texture_hook->hook() && success;

    // Both renderBorder overloads, kept in sync with the active renderModif so scaled
    // overview window borders aren't culled (see render_border_scaled).
    static auto FNS_RB = HyprlandAPI::findFunctionsByName(
        PHANDLE,
        "_ZN6Render2GL15CHyprOpenGLImpl12renderBorderERKN9Hyprutils4Math4CBoxERKN6Config18CGradientValueDataENS1_17SBorderRenderDataE"
    );
    if (FNS_RB.empty())
        fail_exit("No renderBorder");
    render_border_hook =
        HyprlandAPI::createFunctionHook(PHANDLE, FNS_RB[0].address, (void*)hook_render_border);
    Log::logger->log(LOG, "[Hyprtasking] Attempting hook {}", FNS_RB[0].signature);
    success = render_border_hook->hook() && success;

    static auto FNS_RB2 = HyprlandAPI::findFunctionsByName(
        PHANDLE,
        "_ZN6Render2GL15CHyprOpenGLImpl12renderBorderERKN9Hyprutils4Math4CBoxERKN6Config18CGradientValueDataESA_fNS1_17SBorderRenderDataE"
    );
    if (FNS_RB2.empty())
        fail_exit("No renderBorder (lerp)");
    render_border2_hook =
        HyprlandAPI::createFunctionHook(PHANDLE, FNS_RB2[0].address, (void*)hook_render_border2);
    Log::logger->log(LOG, "[Hyprtasking] Attempting hook {}", FNS_RB2[0].signature);
    success = render_border2_hook->hook() && success;

    // Force the fresh blur path during scaled renders (see hook_blur_optimizations).
    static auto FNS_BO = HyprlandAPI::findFunctionsByName(
        PHANDLE,
        "_ZN6Render13IHyprRenderer29shouldUseNewBlurOptimizationsEN9Hyprutils6Memory14CSharedPointerIN7Desktop4View13CLayerSurfaceEEENS3_INS5_7CWindowEEE"
    );
    if (FNS_BO.empty())
        fail_exit("No shouldUseNewBlurOptimizations");
    blur_optimizations_hook =
        HyprlandAPI::createFunctionHook(PHANDLE, FNS_BO[0].address, (void*)hook_blur_optimizations);
    Log::logger->log(LOG, "[Hyprtasking] Attempting hook {}", FNS_BO[0].signature);
    success = blur_optimizations_hook->hook() && success;

    // Hyprland 0.56 moved CMonitor into the Monitor namespace; keep the
    // mangled signature in sync with Monitor::CMonitor.
    static auto FNS2 = HyprlandAPI::findFunctionsByName(
        PHANDLE,
        "_ZN6Render13IHyprRenderer18shouldRenderWindowEN9Hyprutils6Memory14CSharedPointerIN7Desktop4View7CWindowEEENS3_IN7Monitor8CMonitorEEE"
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
        "_ZN6Render13IHyprRenderer12renderWindowEN9Hyprutils6Memory14CSharedPointerIN7Desktop4View7CWindowEEENS3_IN7Monitor8CMonitorEEERKNSt6chrono10time_pointINSB_3_V212steady_clockENSB_8durationIlSt5ratioILl1ELl1000000000EEEEEEbNS_15eRenderPassModeEbb"
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
    static auto PKEY = Event::bus()->m_events.input.keyboard.key.listen(on_key);

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
    if (ht_manager == nullptr)
        return luaL_error(L, "%s", "ht_manager is null");
    PHTVIEW cursor_view = ht_manager->get_view_from_cursor();
    if (cursor_view == nullptr)
        return luaL_error(L, "%s", "cursor_view is null");
    lua_pushboolean(L, cursor_view->active);
    return 1;
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
    HyprlandAPI::addLuaFunction(PHANDLE, "hyprtasking", "is_active", lua_is_active);
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

    // keyboard workspace jump labels
    addConfigValue(CIntValue, "jump:enabled", "enable keyboard workspace jump labels", 0);
    addConfigValue(CIntValue, "jump:label_color", "jump label color", 0xFFFFFFFF);
    addConfigValue(CIntValue, "jump:label_background", "jump label background", 0x000000CC);
    addConfigValue(CIntValue, "jump:label_size", "jump label font size", 32);

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
