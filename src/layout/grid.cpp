#include "grid.hpp"

#include <algorithm>
#include <hyprland/src/config/shared/complex/ComplexDataTypes.hpp>
#include <hyprutils/animation/AnimationConfig.hpp>
#include <unordered_set>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/AnimatedVariable.hpp>
#include <hyprland/src/managers/animation/AnimationManager.hpp>
#include <hyprland/src/managers/animation/DesktopAnimationManager.hpp>
#include <hyprland/src/config/shared/animation/AnimationTree.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/config/shared/workspace/WorkspaceRuleManager.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/BorderPassElement.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprutils/math/Vector2D.hpp>
#include <hyprutils/utils/ScopeGuard.hpp>

#include "../config.hpp"
#include "../globals.hpp"
#include "../overview.hpp"
#include "../render.hpp"
#include "../types.hpp"
#include "src/layout/target/Target.hpp"

using Hyprutils::Utils::CScopeGuard;
using namespace Render;
using namespace Render::GL;
using namespace Hyprutils::Animation;

HTLayoutGrid::HTLayoutGrid(VIEWID new_view_id) : HTLayoutBase(new_view_id) {
    auto &anim_tree = Config::animationTree();
    g_pAnimationManager->createAnimation(
        {0, 0},
        offset,
        anim_tree->getAnimationPropertyConfig("workspaces"),
        AVARDAMAGE_NONE
    );
    g_pAnimationManager->createAnimation(
        1.f,
        scale,
        anim_tree->getAnimationPropertyConfig("workspaces"),
        AVARDAMAGE_NONE
    );

    refresh_workspace_cache();
    init_position();
}

// Bit layout: [layer:24][y:20][x:20]. Used purely as an unordered_map key;
// limits are implicit and not enforced (grid dims are not validated).
long long HTLayoutGrid::pack_slot(int layer, int x, int y) {
    return ((long long)layer << 40) | ((long long)(uint32_t)y << 20) | (long long)(uint32_t)x;
}

WORKSPACEID HTLayoutGrid::slot_workspace(int layer, int x, int y) {
    const auto it = slot_ws_cache.find(pack_slot(layer, x, y));
    if (it == slot_ws_cache.end())
        return WORKSPACE_INVALID;
    return it->second;
}

void HTLayoutGrid::refresh_workspace_cache(
    const std::unordered_set<WORKSPACEID>& extra_off_limits
) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    const int ROWS = HTConfig::value<Hyprlang::INT>("grid:rows");
    const int COLS = HTConfig::value<Hyprlang::INT>("grid:cols");
    const int LAYERS = HTConfig::value<Hyprlang::INT>("grid:layers");
    if (ROWS <= 0 || COLS <= 0 || LAYERS <= 0)
        return;

    const auto prior = ws_slot_cache;

    ws_slot_cache.clear();
    slot_ws_cache.clear();

    std::vector<HTGridSlot> slots;
    slots.reserve((size_t)LAYERS * ROWS * COLS);
    for (int l = 0; l < LAYERS; l++)
        for (int y = 0; y < ROWS; y++)
            for (int x = 0; x < COLS; x++)
                slots.push_back(HTGridSlot {l, x, y});

    std::vector<bool> taken(slots.size(), false);

    auto place = [&](WORKSPACEID id, size_t slot_idx) {
        const HTGridSlot& s = slots[slot_idx];
        ws_slot_cache[id] = s;
        slot_ws_cache[pack_slot(s.layer, s.x, s.y)] = id;
        taken[slot_idx] = true;
    };

    auto find_slot_index = [&](const HTGridSlot& s) -> long long {
        for (size_t i = 0; i < slots.size(); i++)
            if (slots[i].layer == s.layer && slots[i].x == s.x && slots[i].y == s.y)
                return (long long)i;
        return -1;
    };

    auto next_free_slot = [&](size_t& cursor) -> long long {
        while (cursor < slots.size() && taken[cursor])
            cursor++;
        if (cursor >= slots.size())
            return -1;
        return (long long)cursor;
    };

    auto place_with_prior = [&](WORKSPACEID id, size_t& cursor) -> bool {
        if (ws_slot_cache.count(id))
            return false;
        const auto pit = prior.find(id);
        if (pit != prior.end()) {
            const long long idx = find_slot_index(pit->second);
            if (idx >= 0 && !taken[(size_t)idx]) {
                place(id, (size_t)idx);
                return true;
            }
        }
        const long long idx = next_free_slot(cursor);
        if (idx < 0)
            return false;
        place(id, (size_t)idx);
        return true;
    };

    // No two grids may map the same WORKSPACEID, else dragging into a slot
    // could silently switch monitors. extra_off_limits carries IDs already
    // claimed by sibling views in this refresh.
    std::unordered_set<WORKSPACEID> off_limits = extra_off_limits;
    const auto& ws_manager = Config::workspaceRuleMgr();
    const auto& all_rules = ws_manager->getAllWorkspaceRules();
    for (const auto& rule : all_rules) {
        if (rule.m_workspaceId > 0)
            off_limits.insert(rule.m_workspaceId);
    }
    for (const auto& w : g_pCompositor->getWorkspacesCopy()) {
        if (w == nullptr)
            continue;
        if (w->monitorID() != view_id)
            off_limits.insert(w->m_id);
    }

    size_t cursor = 0;

    // Sort by workspaceId so slot assignment doesn't depend on config-line order.
    std::vector<const Config::CWorkspaceRule*> rules_sorted;
    rules_sorted.reserve(all_rules.size());
    for (const auto& r : all_rules)
        rules_sorted.push_back(&r);
    std::sort(rules_sorted.begin(), rules_sorted.end(),
              [](const Config::CWorkspaceRule* a, const Config::CWorkspaceRule* b) {
                  return a->m_workspaceId < b->m_workspaceId;
              });

    for (const Config::CWorkspaceRule* rule : rules_sorted) {
        if (rule->m_workspaceId <= 0)
            continue;
        if (extra_off_limits.count(rule->m_workspaceId))
            continue;
        const auto bound = Config::workspaceRuleMgr()->getBoundMonitorForWS(
            rule->m_workspaceName.starts_with("name:")
                ? rule->m_workspaceName.substr(5)
                : rule->m_workspaceName
        );
        if (bound == nullptr || bound->m_id != view_id)
            continue;
        place_with_prior(rule->m_workspaceId, cursor);
    }

    // Sort by m_id so slot assignment is independent of Hyprland's internal
    // m_workspaces vector order.
    std::vector<PHLWORKSPACE> on_monitor;
    for (const auto& w : g_pCompositor->getWorkspacesCopy()) {
        if (w == nullptr)
            continue;
        if (w->monitorID() != view_id)
            continue;
        if (w->m_id <= 0)
            continue;
        if (extra_off_limits.count(w->m_id))
            continue;
        on_monitor.push_back(w);
    }
    std::sort(on_monitor.begin(), on_monitor.end(),
              [](const PHLWORKSPACE& a, const PHLWORKSPACE& b) {
                  return a->m_id < b->m_id;
              });
    // Settle workspaces that still have a free prior slot before assigning
    // anyone via the cursor — otherwise a migrated workspace with no prior
    // here would steal slot 0 and displace this monitor's resident at (0,0).
    std::vector<PHLWORKSPACE> needs_cursor;
    for (const auto& w : on_monitor) {
        const auto pit = prior.find(w->m_id);
        if (pit != prior.end()) {
            const long long idx = find_slot_index(pit->second);
            if (idx >= 0 && !taken[(size_t)idx]) {
                place(w->m_id, (size_t)idx);
                continue;
            }
        }
        needs_cursor.push_back(w);
    }
    for (const auto& w : needs_cursor)
        place_with_prior(w->m_id, cursor);

    WORKSPACEID synth_candidate = 1;
    auto next_synth = [&]() -> WORKSPACEID {
        while (true) {
            if (off_limits.count(synth_candidate) || ws_slot_cache.count(synth_candidate)) {
                synth_candidate++;
                continue;
            }
            return synth_candidate++;
        }
    };

    for (size_t i = 0; i < slots.size(); i++) {
        if (taken[i])
            continue;
        const WORKSPACEID id = next_synth();
        place(id, i);
    }
}

std::string HTLayoutGrid::layout_name() {
    return "grid";
}

WORKSPACEID HTLayoutGrid::get_ws_id_in_direction(int x, int y, std::string& direction) {
    const int LOOP = HTConfig::value<Hyprlang::INT>("grid:loop");
    const int ROWS = HTConfig::value<Hyprlang::INT>("grid:rows");
    const int COLS = HTConfig::value<Hyprlang::INT>("grid:cols");

    if (direction == "up") {
        y--;
    } else if (direction == "down") {
        y++;
    } else if (direction == "right") {
        x++;
    } else if (direction == "left") {
        x--;
    } else {
        return WORKSPACE_INVALID;
    }

    if (LOOP) {
        x = (x + COLS) % COLS;
        y = (y + ROWS) % ROWS;
    }
    return get_ws_id_from_xy(x, y);
}

void HTLayoutGrid::on_move_swipe(Vector2D delta) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    const float MOVE_DISTANCE = HTConfig::value<Hyprlang::FLOAT>("gestures:move_distance");
    const int ROWS = HTConfig::value<Hyprlang::INT>("grid:rows");
    const int COLS = HTConfig::value<Hyprlang::INT>("grid:cols");
    const CBox min_ws = calculate_ws_box(0, 0, HT_VIEW_CLOSED);
    const CBox max_ws = calculate_ws_box(COLS - 1, ROWS - 1, HT_VIEW_CLOSED);

    Vector2D new_offset = offset->value() + delta / MOVE_DISTANCE * max_ws.w;
    new_offset = new_offset.clamp(Vector2D {-max_ws.x, -max_ws.y}, Vector2D {-min_ws.x, -min_ws.y});

    offset->resetAllCallbacks();
    offset->setValueAndWarp(new_offset);
}

WORKSPACEID HTLayoutGrid::on_move_swipe_end() {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return WORKSPACE_INVALID;

    build_overview_layout(HT_VIEW_CLOSED);
    WORKSPACEID closest = WORKSPACE_INVALID;
    double closest_dist = 1e9;
    for (const auto& [ws_id, box] : overview_layout) {
        const float dist_sq = offset->value().distanceSq(Vector2D {-box.box.x, -box.box.y});
        if (dist_sq < closest_dist) {
            closest_dist = dist_sq;
            closest = ws_id;
        }
    }
    return closest;
}

void HTLayoutGrid::close_open_lerp(float perc) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    double open_scale =
        calculate_ws_box(0, 0, HT_VIEW_OPENED).w / monitor->m_transformedSize.x; // 1 / ROWS
    Vector2D open_pos = {0, 0};

    build_overview_layout(HT_VIEW_CLOSED);
    double close_scale = 1.;
    Vector2D close_pos = -overview_layout[monitor->m_activeWorkspace->m_id].box.pos();

    double new_scale = std::lerp(close_scale, open_scale, perc);
    Vector2D new_pos = Vector2D {
        std::lerp(close_pos.x, open_pos.x, perc),
        std::lerp(close_pos.y, open_pos.y, perc)
    };

    scale->resetAllCallbacks();
    offset->resetAllCallbacks();
    scale->setValueAndWarp(new_scale);
    offset->setValueAndWarp(new_pos);
}

void HTLayoutGrid::on_show(CallbackFun on_complete) {
    CScopeGuard x([this, &on_complete] {
        if (on_complete != nullptr)
            offset->setCallbackOnEnd(on_complete);
    });

    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    *scale = calculate_ws_box(0, 0, HT_VIEW_OPENED).w / monitor->m_transformedSize.x; // 1 / ROWS
    // Offset for the whole grid of workspaces
    *offset = {0, 0};
}

void HTLayoutGrid::on_hide(CallbackFun on_complete) {
    CScopeGuard x([this, &on_complete] {
        if (on_complete != nullptr)
            offset->setCallbackOnEnd(on_complete);
    });

    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    build_overview_layout(HT_VIEW_CLOSED);
    *scale = 1.;
    // End workspace to end up on
    *offset = -overview_layout[monitor->m_activeWorkspace->m_id].box.pos();
}

void HTLayoutGrid::on_move(WORKSPACEID old_id, WORKSPACEID new_id, CallbackFun on_complete) {
    CScopeGuard x([this, &on_complete] {
        if (on_complete != nullptr)
            offset->setCallbackOnEnd(on_complete);
    });

    const PHTVIEW par_view = ht_manager->get_view_from_id(view_id);
    if (par_view == nullptr || par_view->active)
        return;

    // prevent the thing from animating
    g_pCompositor->getWorkspaceByID(old_id)->m_renderOffset->warp();
    g_pCompositor->getWorkspaceByID(new_id)->m_renderOffset->warp();

    build_overview_layout(HT_VIEW_CLOSED);
    *scale = 1.;
    // Target workspace to animate to
    *offset = -overview_layout[new_id].box.pos();
}

bool HTLayoutGrid::should_render_window(PHLWINDOW window) {
    bool ori_result = HTLayoutBase::should_render_window(window);

    const PHLMONITOR monitor = get_monitor();
    if (window == nullptr || monitor == nullptr)
        return ori_result;

    const SP<Layout::ITarget> target = g_layoutManager->dragController()->target();
    if (target != nullptr && window == target->window())
        return false;

    PHLWORKSPACE workspace = window->m_workspace;
    if (workspace == nullptr)
        return false;

    CBox window_box = get_global_window_box(window, window->workspaceID());
    if (window_box.empty())
        return false;
    if (window_box.intersection(monitor->logicalBox()).empty())
        return false;

    return ori_result;
}

float HTLayoutGrid::drag_window_scale() {
    return scale->value();
}

void HTLayoutGrid::init_position() {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    if (monitor->m_activeWorkspace == nullptr)
        return;

    // Sync to the layer of whatever workspace is currently active on this
    // monitor. Fresh views (e.g. after monitor reconnect) start at layer 0,
    // so without this the overview would open on the wrong layer.
    const auto sit = ws_slot_cache.find(monitor->m_activeWorkspace->m_id);
    if (sit != ws_slot_cache.end())
        layer = sit->second.layer;

    build_overview_layout(HT_VIEW_CLOSED);

    const auto it = overview_layout.find(monitor->m_activeWorkspace->m_id);
    if (it == overview_layout.end() || it->second.box.empty())
        return;

    offset->setValueAndWarp(-it->second.box.pos());
    scale->setValueAndWarp(1.f);
}

CBox HTLayoutGrid::calculate_ws_box(int x, int y, HTViewStage stage) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {};

    // Monitor may not have its final size yet during connect/reconnect
    if (monitor->m_transformedSize.x < 1 || monitor->m_transformedSize.y < 1)
        return {};

    const int ROWS = HTConfig::value<Hyprlang::INT>("grid:rows");
    const int COLS = HTConfig::value<Hyprlang::INT>("grid:cols");
    const int GAPS_USE_ASPECT_RATIO = HTConfig::value<Hyprlang::INT>("grid:gaps_use_aspect_ratio");
    const float GAP_SIZE = HTConfig::value<Hyprlang::FLOAT>("gap_size") * monitor->m_scale;
    const Vector2D gaps = {
        GAP_SIZE,
        GAPS_USE_ASPECT_RATIO
            ? GAP_SIZE * monitor->m_transformedSize.y / monitor->m_transformedSize.x
            : GAP_SIZE
    };

    if (GAP_SIZE > std::min(monitor->m_transformedSize.x, monitor->m_transformedSize.y)
        || GAP_SIZE < 0)
        return {};

    double render_x = (monitor->m_transformedSize.x - gaps.x * (COLS + 1)) / COLS;
    double render_y = (monitor->m_transformedSize.y - gaps.y * (ROWS + 1)) / ROWS;
    const double mon_aspect = monitor->m_transformedSize.x / monitor->m_transformedSize.y;
    Vector2D start_offset {};

    // make correct aspect ratio
    if (render_y * mon_aspect > render_x) {
        start_offset.y = (render_y - render_x / mon_aspect) * ROWS / 2.f;
        render_y = render_x / mon_aspect;
    } else if (render_x / mon_aspect > render_y) {
        start_offset.x = (render_x - render_y * mon_aspect) * COLS / 2.f;
        render_x = render_y * mon_aspect;
    }

    float use_scale = scale->value();
    Vector2D use_offset = offset->value();
    if (stage == HT_VIEW_CLOSED) {
        use_scale = 1;
        use_offset = Vector2D {0, 0};
    } else if (stage == HT_VIEW_OPENED) {
        use_scale = render_x / monitor->m_transformedSize.x;
        use_offset = Vector2D {0, 0};
    }

    const Vector2D ws_sz = monitor->m_transformedSize * use_scale;
    return CBox {Vector2D {x, y} * (ws_sz + gaps) + gaps + use_offset + start_offset, ws_sz};
};

void HTLayoutGrid::build_overview_layout(HTViewStage stage) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    const int ROWS = HTConfig::value<Hyprlang::INT>("grid:rows");
    const int COLS = HTConfig::value<Hyprlang::INT>("grid:cols");

    const PHLMONITOR last_monitor = Desktop::focusState()->monitor();
    Desktop::focusState()->rawMonitorFocus(monitor);

    overview_layout.clear();
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            const WORKSPACEID ws_id = slot_workspace(layer, x, y);
            if (ws_id == WORKSPACE_INVALID)
                continue;
            CBox ws_box = calculate_ws_box(x, y, stage);
            ws_box.round();
            overview_layout[ws_id] = HTWorkspace {x, y, ws_box};
        }
    }

    if (last_monitor != nullptr)
        Desktop::focusState()->rawMonitorFocus(last_monitor);
}

void HTLayoutGrid::render() {
    HTLayoutBase::render();
    CScopeGuard x([this] { post_render(); });

    const PHTVIEW par_view = ht_manager->get_view_from_id(view_id);
    if (par_view == nullptr)
        return;
    const PHLMONITOR monitor = par_view->get_monitor();
    if (monitor == nullptr)
        return;

    static auto PACTIVECOL = CConfigValue<Config::IComplexConfigValue>("general:col.active_border");
    static auto PINACTIVECOL = CConfigValue<Config::IComplexConfigValue>("general:col.inactive_border");

    auto* const ACTIVECOL = (Config::CGradientValueData*)(PACTIVECOL.ptr());
    auto* const INACTIVECOL = (Config::CGradientValueData*)(PINACTIVECOL.ptr());

    const float BORDERSIZE = HTConfig::value<Hyprlang::FLOAT>("border_size");

    const auto time = Time::steadyNow();


    g_pHyprRenderer->damageMonitor(monitor);
    g_pHyprRenderer->m_renderData.pMonitor->m_blurFBShouldRender = true;
    CBox monitor_box = {{0, 0}, monitor->m_transformedSize};

    CRectPassElement::SRectData data;
    data.color = CHyprColor {HTConfig::value<Hyprlang::INT>("bg_color")}.stripA();
    data.box = monitor_box;
    g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(data));

    // Do a dance with active workspaces: Hyprland will only properly render the
    // current active one so make the workspace active before rendering it, etc
    const PHLWORKSPACE start_workspace = monitor->m_activeWorkspace;

    g_pDesktopAnimationManager->startAnimation(
        start_workspace,
        CDesktopAnimationManager::ANIMATION_TYPE_OUT,
        false,
        true
    );
    start_workspace->m_visible = false;

    build_overview_layout(HT_VIEW_ANIMATING);

    CBox global_mon_box = {monitor->m_position, monitor->m_transformedSize};
    for (const auto& [ws_id, ws_layout] : overview_layout) {
        // Skip if the box is empty
        if (ws_layout.box.width < 0.01 || ws_layout.box.height < 0.01)
            continue;

        // Could be nullptr, in which we render only layers
        const PHLWORKSPACE workspace = g_pCompositor->getWorkspaceByID(ws_id);

        // renderModif translation used by renderWorkspace is weird so need
        // to scale the translation up as well. Geometry is also calculated from pixel size and not transformed size??
        CBox render_box = {{ws_layout.box.pos() / scale->value()}, ws_layout.box.size()};
        if (monitor->m_transform % 2 == 1)
            std::swap(render_box.w, render_box.h);

        // render active one last
        if (workspace == start_workspace && start_workspace != nullptr)
            continue;

        CBox global_box = {ws_layout.box.pos() + monitor->m_position, ws_layout.box.size()};
        if (global_box.expand(BORDERSIZE).intersection(global_mon_box).empty())
            continue;

        const Config::CGradientValueData border_col =
            monitor->m_activeWorkspace->m_id == ws_id ? *ACTIVECOL : *INACTIVECOL;
        CBox border_box = ws_layout.box;

        CBorderPassElement::SBorderData data;
        data.box = border_box;
        data.grad1 = border_col;
        data.borderSize = BORDERSIZE;

        if (workspace != nullptr) {
            monitor->m_activeWorkspace = workspace;
            g_pDesktopAnimationManager->startAnimation(
                workspace,
                CDesktopAnimationManager::ANIMATION_TYPE_IN,
                false,
                true
            );
            workspace->m_visible = true;

            ((render_workspace_t)(render_workspace_hook->m_original))(
                g_pHyprRenderer.get(),
                monitor,
                workspace,
                time,
                render_box
            );

            g_pDesktopAnimationManager->startAnimation(
                workspace,
                CDesktopAnimationManager::ANIMATION_TYPE_OUT,
                false,
                true
            );
            workspace->m_visible = false;
        } else {
            // If pWorkspace is null, then just render the layers
            ((render_workspace_t)(render_workspace_hook->m_original))(
                g_pHyprRenderer.get(),
                monitor,
                workspace,
                time,
                render_box
            );
        }
        g_pHyprRenderer->m_renderPass.add(makeUnique<CBorderPassElement>(data));
    }

    monitor->m_activeWorkspace = start_workspace;
    g_pDesktopAnimationManager->startAnimation(
        start_workspace,
        CDesktopAnimationManager::ANIMATION_TYPE_IN,
        false,
        true
    );
    start_workspace->m_visible = true;

    // Render active workspace last so the dragging window is always on top when let go of
    if (start_workspace != nullptr && overview_layout.count(start_workspace->m_id)) {
        CBox ws_box = overview_layout[start_workspace->m_id].box;
        // make sure box is not empty
        if (ws_box.width > 0.01 && ws_box.height > 0.01) {
            // renderModif translation used by renderWorkspace is weird so need
            // to scale the translation up as well. Geometry is also calculated from pixel size and not transformed size??
            CBox render_box = {{ws_box.pos() / scale->value()}, ws_box.size()};
            if (monitor->m_transform % 2 == 1)
                std::swap(render_box.w, render_box.h);

            const Config::CGradientValueData border_col =
                monitor->m_activeWorkspace->m_id == start_workspace->m_id ? *ACTIVECOL
                                                                          : *INACTIVECOL;
            CBox border_box = ws_box;

            CBorderPassElement::SBorderData data;
            data.box = border_box;
            data.grad1 = border_col;
            data.borderSize = BORDERSIZE;

            ((render_workspace_t)(render_workspace_hook->m_original))(
                g_pHyprRenderer.get(),
                monitor,
                start_workspace,
                time,
                render_box
            );
            g_pHyprRenderer->m_renderPass.add(makeUnique<CBorderPassElement>(data));
        }
    }

    const PHTVIEW cursor_view = ht_manager->get_view_from_cursor();
    if (cursor_view == nullptr)
        return;
    const SP<Layout::ITarget> target = g_layoutManager->dragController()->target();
    if (target == nullptr)
        return;
    const PHLWINDOW dragged_window = target->window();
    if (dragged_window == nullptr)
        return;
    const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
    const CBox window_box = dragged_window->getWindowMainSurfaceBox()
                                .translate(-mouse_coords)
                                .scale(cursor_view->layout->drag_window_scale())
                                .translate(mouse_coords);
    if (!window_box.intersection(monitor->logicalBox()).empty())
        render_window_at_box(dragged_window, monitor, time, window_box);
}
