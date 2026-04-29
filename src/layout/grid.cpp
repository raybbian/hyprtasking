#include "grid.hpp"

#include <algorithm>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/AnimatedVariable.hpp>
#include <hyprland/src/managers/animation/AnimationManager.hpp>
#include <hyprland/src/managers/animation/DesktopAnimationManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
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

HTLayoutGrid::HTLayoutGrid(VIEWID new_view_id) : HTLayoutBase(new_view_id) {
    g_pAnimationManager->createAnimation(
        {0, 0},
        offset,
        g_pConfigManager->getAnimationPropertyConfig("workspaces"),
        AVARDAMAGE_NONE
    );
    g_pAnimationManager->createAnimation(
        1.f,
        scale,
        g_pConfigManager->getAnimationPropertyConfig("workspaces"),
        AVARDAMAGE_NONE
    );

    init_position();
}

std::string HTLayoutGrid::layout_name() {
    return "grid";
}

void HTLayoutGrid::pin_workspace_to_slot(WORKSPACEID ws_id, int slot) {
    pinned_positions[ws_id] = slot;
}

void HTLayoutGrid::unpin_workspace(WORKSPACEID ws_id) {
    pinned_positions.erase(ws_id);
}

std::tuple<int, int, int> HTLayoutGrid::get_grid_cell_from_global(Vector2D pos) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {-1, -1, -1};

    if (!monitor->logicalBox().containsPoint(pos))
        return {-1, -1, -1};

    build_overview_layout(HT_VIEW_ANIMATING);

    const int ROWS = HTConfig::value<Hyprlang::INT>("grid:rows");
    const int COLS = HTConfig::value<Hyprlang::INT>("grid:cols");

    Vector2D rel = (pos - monitor->m_position) * monitor->m_scale;

    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            CBox box = calculate_ws_box(x, y, HT_VIEW_ANIMATING);
            if (!box.empty() && box.containsPoint(rel))
                return {x, y, layer};
        }
    }

    return {-1, -1, -1};
}

int HTLayoutGrid::get_effective_layer_count(size_t workspace_count) {
    const int ROWS = HTConfig::value<Hyprlang::INT>("grid:rows");
    const int COLS = HTConfig::value<Hyprlang::INT>("grid:cols");
    const int LAYERS = HTConfig::value<Hyprlang::INT>("grid:layers");
    const int ws_per_layer = std::max(1, ROWS * COLS);
    const int configured_layers = std::max(1, LAYERS);

    int max_slot = -1;
    for (const auto& [ws_id, slot] : pinned_positions) {
        if (slot > max_slot)
            max_slot = slot;
    }
    const int needed_by_count = (int)(workspace_count + ws_per_layer - 1) / ws_per_layer;
    const int needed_by_pins = (max_slot >= 0) ? (max_slot / ws_per_layer + 1) : needed_by_count;
    const int needed_layers = std::max(1, needed_by_pins);

    return std::max(configured_layers, needed_layers);
}

int HTLayoutGrid::get_workspace_layer(WORKSPACEID workspace_id) {
    auto pin_it = pinned_positions.find(workspace_id);
    if (pin_it != pinned_positions.end()) {
        const int ROWS = HTConfig::value<Hyprlang::INT>("grid:rows");
        const int COLS = HTConfig::value<Hyprlang::INT>("grid:cols");
        const int ws_per_layer = std::max(1, ROWS * COLS);
        return pin_it->second / ws_per_layer;
    }

    const int ROWS = HTConfig::value<Hyprlang::INT>("grid:rows");
    const int COLS = HTConfig::value<Hyprlang::INT>("grid:cols");
    const int ws_per_layer = std::max(1, ROWS * COLS);
    const std::vector<PHLWORKSPACE> workspaces = get_monitor_workspaces();

    for (size_t i = 0; i < workspaces.size(); i++) {
        if (workspaces[i] != nullptr && workspaces[i]->m_id == workspace_id)
            return (int)i / ws_per_layer;
    }

    return layer;
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

    layer = get_workspace_layer(monitor->m_activeWorkspace->m_id);
    build_overview_layout(HT_VIEW_CLOSED);
    double close_scale = 1.;
    const auto active_it = overview_layout.find(monitor->m_activeWorkspace->m_id);
    if (active_it == overview_layout.end())
        return;
    Vector2D close_pos = -active_it->second.box.pos();

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

    if (monitor->m_activeWorkspace == nullptr)
        return;

    layer = get_workspace_layer(monitor->m_activeWorkspace->m_id);
    build_overview_layout(HT_VIEW_CLOSED);
    *scale = 1.;
    // End workspace to end up on
    const auto active_it = overview_layout.find(monitor->m_activeWorkspace->m_id);
    if (active_it == overview_layout.end())
        return;
    *offset = -active_it->second.box.pos();
}

void HTLayoutGrid::on_move(WORKSPACEID old_id, WORKSPACEID new_id, CallbackFun on_complete) {
    CScopeGuard x([this, &on_complete] {
        if (on_complete != nullptr)
            offset->setCallbackOnEnd(on_complete);
    });

    const PHTVIEW par_view = ht_manager->get_view_from_id(view_id);
    if (par_view == nullptr || par_view->active)
        return;

    const PHLWORKSPACE old_workspace = g_pCompositor->getWorkspaceByID(old_id);
    const PHLWORKSPACE new_workspace = g_pCompositor->getWorkspaceByID(new_id);
    if (old_workspace == nullptr || new_workspace == nullptr)
        return;

    // prevent the thing from animating
    old_workspace->m_renderOffset->warp();
    new_workspace->m_renderOffset->warp();

    layer = get_workspace_layer(new_id);
    build_overview_layout(HT_VIEW_CLOSED);
    *scale = 1.;
    // Target workspace to animate to
    const auto target_it = overview_layout.find(new_id);
    if (target_it == overview_layout.end())
        return;
    *offset = -target_it->second.box.pos();
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

    layer = get_workspace_layer(monitor->m_activeWorkspace->m_id);
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
    const std::vector<PHLWORKSPACE> workspaces = get_monitor_workspaces();
    const int ws_per_layer = std::max(1, ROWS * COLS);
    const int effective_layers = get_effective_layer_count(workspaces.size());
    layer = std::clamp(layer, 0, effective_layers - 1);

    overview_layout.clear();

    for (auto it = pinned_positions.begin(); it != pinned_positions.end(); ) {
        if (g_pCompositor->getWorkspaceByID(it->first) == nullptr)
            it = pinned_positions.erase(it);
        else
            ++it;
    }

    const int layer_start = layer * ws_per_layer;
    const int layer_end = layer_start + ws_per_layer;

    std::vector<bool> slot_occupied(ws_per_layer, false);
    std::unordered_map<WORKSPACEID, int> ws_to_slot;

    for (PHLWORKSPACE workspace : workspaces) {
        if (workspace == nullptr)
            continue;

        auto pin_it = pinned_positions.find(workspace->m_id);
        if (pin_it != pinned_positions.end()) {
            int slot = pin_it->second;
            if (slot >= layer_start && slot < layer_end) {
                int local_i = slot - layer_start;
                slot_occupied[local_i] = true;
                ws_to_slot[workspace->m_id] = slot;
            }
        }
    }

    int next_free = layer_start;
    for (PHLWORKSPACE workspace : workspaces) {
        if (workspace == nullptr)
            continue;
        if (ws_to_slot.contains(workspace->m_id))
            continue;

        auto pin_it = pinned_positions.find(workspace->m_id);
        if (pin_it != pinned_positions.end()) {
            // Skip workspaces pinned to other layers
            continue;
        }

        while (next_free < layer_end && slot_occupied[next_free - layer_start])
            ++next_free;

        if (next_free >= layer_end)
            break;

        slot_occupied[next_free - layer_start] = true;
        ws_to_slot[workspace->m_id] = next_free;
        ++next_free;
    }


    for (const auto& [ws_id, slot] : ws_to_slot) {
        int local_i = slot - layer_start;
        int x = local_i % COLS;
        int y = local_i / COLS;
        PHLWORKSPACE workspace = g_pCompositor->getWorkspaceByID(ws_id);
        if (workspace == nullptr)
            continue;

        const CBox ws_box = calculate_ws_box(x, y, stage);
        overview_layout[ws_id] = HTWorkspace {
            x,
            y,
            ws_box,
            ws_id,
            workspace->m_name,
            monitor->m_id,
        };
    }
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

    static auto PACTIVECOL = CConfigValue<Hyprlang::CUSTOMTYPE>("general:col.active_border");
    static auto PINACTIVECOL = CConfigValue<Hyprlang::CUSTOMTYPE>("general:col.inactive_border");

    auto* const ACTIVECOL = (CGradientValueData*)(PACTIVECOL.ptr())->getData();
    auto* const INACTIVECOL = (CGradientValueData*)(PINACTIVECOL.ptr())->getData();

    const float BORDERSIZE = HTConfig::value<Hyprlang::FLOAT>("border_size");

    const auto time = Time::steadyNow();


    g_pHyprRenderer->damageMonitor(monitor);
    g_pHyprOpenGL->m_renderData.pCurrentMonData->blurFBShouldRender = true;
    CBox monitor_box = {{0, 0}, monitor->m_transformedSize};

    CRectPassElement::SRectData data;
    data.color = CHyprColor {HTConfig::value<Hyprlang::INT>("bg_color")}.stripA();
    data.box = monitor_box;
    g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(data));

    // Do a dance with active workspaces: Hyprland will only properly render the
    // current active one so make the workspace active before rendering it, etc
    const PHLWORKSPACE start_workspace = monitor->m_activeWorkspace;
    if (start_workspace == nullptr)
        return;

    g_pDesktopAnimationManager->startAnimation(
        start_workspace,
        CDesktopAnimationManager::ANIMATION_TYPE_OUT,
        false,
        true
    );
    start_workspace->m_visible = false;

    build_overview_layout(HT_VIEW_ANIMATING);

    const int ROWS = HTConfig::value<Hyprlang::INT>("grid:rows");
    const int COLS = HTConfig::value<Hyprlang::INT>("grid:cols");

    std::unordered_map<int, WORKSPACEID> grid_to_ws;
    for (const auto& [ws_id, ws_layout] : overview_layout) {
        grid_to_ws[ws_layout.y * COLS + ws_layout.x] = ws_id;
    }

    CBox global_mon_box = {monitor->m_position, monitor->m_transformedSize};
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            const int grid_key = y * COLS + x;
            const bool has_ws = grid_to_ws.contains(grid_key);

            CBox ws_box;
            WORKSPACEID ws_id = WORKSPACE_INVALID;
            if (has_ws) {
                ws_id = grid_to_ws[grid_key];
                const auto& ws_layout = overview_layout[ws_id];
                ws_box = ws_layout.box;
            } else {
                ws_box = calculate_ws_box(x, y, HT_VIEW_ANIMATING);
            }

            if (ws_box.width < 0.01 || ws_box.height < 0.01)
                continue;

            // renderModif translation used by renderWorkspace is weird so need
            // to scale the translation up as well. Geometry is also calculated from pixel size and not transformed size??
            CBox render_box = {{ws_box.pos() / scale->value()}, ws_box.size()};
            if (monitor->m_transform % 2 == 1)
                std::swap(render_box.w, render_box.h);

            // render active one last
            if (has_ws && ws_id == start_workspace->m_id && start_workspace != nullptr)
                continue;

            CBox global_box = {ws_box.pos() + monitor->m_position, ws_box.size()};
            if (global_box.expand(BORDERSIZE).intersection(global_mon_box).empty())
                continue;

            const CGradientValueData border_col =
                (has_ws && start_workspace->m_id == ws_id) ? *ACTIVECOL : *INACTIVECOL;
            CBox border_box = ws_box;

            CBorderPassElement::SBorderData data;
            data.box = border_box;
            data.grad1 = border_col;
            data.borderSize = BORDERSIZE;
            g_pHyprRenderer->m_renderPass.add(makeUnique<CBorderPassElement>(data));

            if (has_ws) {
                const PHLWORKSPACE workspace = get_workspace_from_layout(ws_id);

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
                    ((render_workspace_t)(render_workspace_hook->m_original))(
                        g_pHyprRenderer.get(),
                        monitor,
                        workspace,
                        time,
                        render_box
                    );
                }
            } else {
                ((render_workspace_t)(render_workspace_hook->m_original))(
                    g_pHyprRenderer.get(),
                    monitor,
                    nullptr,
                    time,
                    render_box
                );
            }
        }
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
    const auto active_it = overview_layout.find(start_workspace->m_id);
    if (start_workspace != nullptr && active_it != overview_layout.end()) {
        CBox ws_box = active_it->second.box;
        // make sure box is not empty
        if (ws_box.width > 0.01 && ws_box.height > 0.01) {
            // renderModif translation used by renderWorkspace is weird so need
            // to scale the translation up as well. Geometry is also calculated from pixel size and not transformed size??
            CBox render_box = {{ws_box.pos() / scale->value()}, ws_box.size()};
            if (monitor->m_transform % 2 == 1)
                std::swap(render_box.w, render_box.h);

            const CGradientValueData border_col =
                monitor->m_activeWorkspace->m_id == start_workspace->m_id ? *ACTIVECOL
                                                                          : *INACTIVECOL;
            CBox border_box = ws_box;

            CBorderPassElement::SBorderData data;
            data.box = border_box;
            data.grad1 = border_col;
            data.borderSize = BORDERSIZE;
            g_pHyprRenderer->m_renderPass.add(makeUnique<CBorderPassElement>(data));

            ((render_workspace_t)(render_workspace_hook->m_original))(
                g_pHyprRenderer.get(),
                monitor,
                start_workspace,
                time,
                render_box
            );
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
