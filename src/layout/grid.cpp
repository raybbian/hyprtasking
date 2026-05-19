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
#include "../overview.hpp"
#include "../types.hpp"

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

HTLayoutGrid::HTGridCell* HTLayoutGrid::cell_at(int target_layer, int y, int x) {
    if (target_layer < 0 || target_layer >= (int)grid_cells.size())
        return nullptr;
    if (y < 0 || y >= (int)grid_cells[target_layer].size())
        return nullptr;
    if (x < 0 || x >= (int)grid_cells[target_layer][y].size())
        return nullptr;
    return &grid_cells[target_layer][y][x];
}

const HTLayoutGrid::HTGridCell* HTLayoutGrid::cell_at(int target_layer, int y, int x) const {
    if (target_layer < 0 || target_layer >= (int)grid_cells.size())
        return nullptr;
    if (y < 0 || y >= (int)grid_cells[target_layer].size())
        return nullptr;
    if (x < 0 || x >= (int)grid_cells[target_layer][y].size())
        return nullptr;
    return &grid_cells[target_layer][y][x];
}

HTLayoutGrid::HTGridCell* HTLayoutGrid::find_cell(WORKSPACEID ws_id) {
    for (auto& layer_cells : grid_cells) {
        for (auto& row : layer_cells) {
            for (auto& cell : row) {
                if (cell.ws_id == ws_id)
                    return &cell;
            }
        }
    }
    return nullptr;
}

void HTLayoutGrid::pin_workspace_to_slot(WORKSPACEID ws_id, int slot) {
    const int ROWS = HTConfig::value("grid:rows");
    const int COLS = HTConfig::value("grid:cols");
    const int ws_per_layer = std::max(1, ROWS * COLS);

    const int target_layer = slot / ws_per_layer;
    const int local_slot = slot % ws_per_layer;
    const int target_y = local_slot / COLS;
    const int target_x = local_slot % COLS;

    if (target_y < 0 || target_y >= ROWS || target_x < 0 || target_x >= COLS)
        return;

    if (target_layer >= (int)grid_cells.size()) {
        grid_cells.resize(target_layer + 1);
        for (int l = 0; l <= target_layer; l++) {
            grid_cells[l].resize(ROWS);
            for (int y = 0; y < ROWS; y++)
                grid_cells[l][y].resize(COLS);
        }
    }

    if (HTGridCell* cell = find_cell(ws_id)) {
        cell->ws_id = WORKSPACE_INVALID;
        cell->occupied = false;
        cell->is_pinned = false;
    }

    auto& cell = *cell_at(target_layer, target_y, target_x);
    cell.ws_id = ws_id;
    cell.occupied = true;
    cell.is_pinned = true;
}

void HTLayoutGrid::unpin_workspace(WORKSPACEID ws_id) {
    if (HTGridCell* cell = find_cell(ws_id))
        cell->is_pinned = false;
}

std::tuple<int, int, int> HTLayoutGrid::get_grid_cell_from_global(Vector2D pos) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {-1, -1, -1};

    if (!monitor->logicalBox().containsPoint(pos))
        return {-1, -1, -1};

    build_overview_layout(HT_VIEW_ANIMATING);

    const int ROWS = HTConfig::value("grid:rows");
    const int COLS = HTConfig::value("grid:cols");

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
    const int ROWS = HTConfig::value("grid:rows");
    const int COLS = HTConfig::value("grid:cols");
    const int LAYERS = HTConfig::value("grid:layers");
    const int ws_per_layer = std::max(1, ROWS * COLS);
    const int configured_layers = std::max(1, LAYERS);

    const int needed_by_count = (int)(workspace_count + ws_per_layer - 1) / ws_per_layer;
    const int needed_layers = std::max(1, needed_by_count);

    int max_layer = std::max(configured_layers, needed_layers);

    // If the grid has previously been grown by pin_workspace_to_slot,
    // keep enough layers so those pins remain addressable.
    if (!grid_cells.empty())
        max_layer = std::max(max_layer, get_max_occupied_layer() + 1);

    return max_layer;
}

int HTLayoutGrid::get_max_occupied_layer() {
    int max_occupied = 0;
    for (int l = 0; l < (int)grid_cells.size(); l++) {
        for (int y = 0; y < (int)grid_cells[l].size(); y++) {
            for (int x = 0; x < (int)grid_cells[l][y].size(); x++) {
                const HTGridCell* cell = cell_at(l, y, x);
                if (cell != nullptr && cell->ws_id != WORKSPACE_INVALID) {
                    max_occupied = std::max(max_occupied, l);
                    break;
                }
            }
        }
    }
    return max_occupied;
}

int HTLayoutGrid::get_workspace_layer(WORKSPACEID workspace_id) {
    if (const HTGridCell* cell = find_cell(workspace_id))
        for (int l = 0; l < (int)grid_cells.size(); l++)
            for (int y = 0; y < (int)grid_cells[l].size(); y++)
                for (int x = 0; x < (int)grid_cells[l][y].size(); x++)
                    if (cell_at(l, y, x) == cell)
                        return l;
    return layer;
}

WORKSPACEID HTLayoutGrid::get_ws_id_in_direction(int x, int y, std::string& direction) {
    const int LOOP = HTConfig::value("grid:loop");
    const int ROWS = HTConfig::value("grid:rows");
    const int COLS = HTConfig::value("grid:cols");

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

    const float MOVE_DISTANCE = HTConfig::value_float("gestures:move_distance");
    const int ROWS = HTConfig::value("grid:rows");
    const int COLS = HTConfig::value("grid:cols");
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
    for (const auto& [ws_id, ws_layout] : overview_layout) {
        const float dist_sq = offset->value().distanceSq(Vector2D {-ws_layout.box.x, -ws_layout.box.y});
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
        calculate_ws_box(0, 0, HT_VIEW_OPENED).w / monitor->m_transformedSize.x;
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

    *scale = calculate_ws_box(0, 0, HT_VIEW_OPENED).w / monitor->m_transformedSize.x;
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

    if (par_view == nullptr || par_view->active)
        return;

    const PHLWORKSPACE old_workspace = g_pCompositor->getWorkspaceByID(old_id);
    const PHLWORKSPACE new_workspace = g_pCompositor->getWorkspaceByID(new_id);
    if (old_workspace == nullptr || new_workspace == nullptr)
        return;

    old_workspace->m_renderOffset->warp();
    new_workspace->m_renderOffset->warp();

    layer = get_workspace_layer(new_id);
    build_overview_layout(HT_VIEW_CLOSED);
    *scale = 1.;

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

    if (monitor->m_transformedSize.x < 1 || monitor->m_transformedSize.y < 1)
        return {};

    const int ROWS = HTConfig::value("grid:rows");
    const int COLS = HTConfig::value("grid:cols");
    const int GAPS_USE_ASPECT_RATIO = HTConfig::value("grid:gaps_use_aspect_ratio");
    const float GAP_SIZE = HTConfig::value_float("gap_size") * monitor->m_scale;
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

    const int ROWS = HTConfig::value("grid:rows");
    const int COLS = HTConfig::value("grid:cols");
    std::vector<PHLWORKSPACE> workspaces = get_monitor_workspaces();
    std::ranges::sort(workspaces, [](const PHLWORKSPACE& lhs, const PHLWORKSPACE& rhs) {
        if (lhs == nullptr)
            return false;
        if (rhs == nullptr)
            return true;
        return lhs->m_id < rhs->m_id;
    });
    const int effective_layers = get_effective_layer_count(workspaces.size());
    layer = std::clamp(layer, 0, effective_layers - 1);

    overview_layout.clear();

    grid_cells.resize(effective_layers);
    for (int l = 0; l < effective_layers; l++) {
        grid_cells[l].resize(ROWS);
        for (int y = 0; y < ROWS; y++) {
            grid_cells[l][y].resize(COLS);
        }
    }

    for (int l = 0; l < effective_layers; l++) {
        for (int y = 0; y < (int)grid_cells[l].size(); y++) {
            for (int x = 0; x < (int)grid_cells[l][y].size(); x++) {
                HTGridCell* cell = cell_at(l, y, x);
                if (cell != nullptr && cell->ws_id != WORKSPACE_INVALID) {
                    const PHLWORKSPACE workspace = g_pCompositor->getWorkspaceByID(cell->ws_id);
                    if (!is_monitor_workspace(workspace)) {
                        cell->ws_id = WORKSPACE_INVALID;
                        cell->occupied = false;
                        cell->is_pinned = false;
                    }
                }
            }
        }
    }

    for (PHLWORKSPACE workspace : workspaces) {
        if (workspace == nullptr)
            continue;

        if (find_cell(workspace->m_id) != nullptr)
            continue;

        bool placed = false;
        for (int l = 0; l < effective_layers && !placed; l++) {
            for (int y = 0; y < ROWS && !placed; y++) {
                for (int x = 0; x < COLS && !placed; x++) {
                    HTGridCell* cell = cell_at(l, y, x);
                    if (cell != nullptr && cell->ws_id == WORKSPACE_INVALID) {
                        cell->ws_id = workspace->m_id;
                        cell->occupied = true;
                        cell->is_pinned = false;
                        placed = true;
                    }
                }
            }
        }
    }

    if (layer >= 0 && layer < effective_layers) {
        for (int y = 0; y < ROWS; y++) {
            for (int x = 0; x < COLS; x++) {
                const HTGridCell* cell = cell_at(layer, y, x);
                if (cell == nullptr || cell->ws_id == WORKSPACE_INVALID)
                    continue;

                PHLWORKSPACE workspace = g_pCompositor->getWorkspaceByID(cell->ws_id);
                if (workspace == nullptr)
                    continue;

                const CBox ws_box = calculate_ws_box(x, y, stage);
                overview_layout[cell->ws_id] = HTWorkspace {
                    x,
                    y,
                    ws_box,
                    cell->ws_id,
                    workspace->m_name,
                    monitor->m_id,
                };
            }
        }
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

    const int bg_color = HTConfig::value("bg_color");
    const float border_size = cached_border_size;
    const float scale_value = scale->value();

    g_pHyprRenderer->damageMonitor(monitor);
    g_pHyprOpenGL->m_renderData.pCurrentMonData->blurFBShouldRender = true;
    CBox monitor_box = {{0, 0}, monitor->m_transformedSize};

    CRectPassElement::SRectData data;
    data.color = CHyprColor {bg_color}.stripA();
    data.box = monitor_box;
    g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(data));

    const PHLWORKSPACE start_workspace = monitor->m_activeWorkspace;
    if (start_workspace == nullptr)
        return;

    CScopeGuard restore_workspace([&monitor, &start_workspace] {
        if (monitor == nullptr || start_workspace == nullptr)
            return;
        monitor->m_activeWorkspace = start_workspace;
        g_pDesktopAnimationManager->startAnimation(
            start_workspace,
            CDesktopAnimationManager::ANIMATION_TYPE_IN,
            false,
            true
        );
        start_workspace->m_visible = true;
    });

    g_pDesktopAnimationManager->startAnimation(
        start_workspace,
        CDesktopAnimationManager::ANIMATION_TYPE_OUT,
        false,
        true
    );
    start_workspace->m_visible = false;

    build_overview_layout(HT_VIEW_ANIMATING);

    const int ROWS = HTConfig::value("grid:rows");
    const int COLS = HTConfig::value("grid:cols");
    const int effective_layers = grid_cells.size();

    if (layer < 0 || layer >= effective_layers)
        return;

    CBox global_mon_box = {monitor->m_position, monitor->m_transformedSize};
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            const HTGridCell* cell = cell_at(layer, y, x);
            const bool has_ws = cell != nullptr && cell->ws_id != WORKSPACE_INVALID;

            CBox ws_box;
            WORKSPACEID ws_id = WORKSPACE_INVALID;
            if (has_ws) {
                ws_id = cell->ws_id;
                const auto& ws_layout = overview_layout[ws_id];
                ws_box = ws_layout.box;
            } else {
                ws_box = calculate_ws_box(x, y, HT_VIEW_ANIMATING);
            }

            if (ws_box.width < 0.01 || ws_box.height < 0.01)
                continue;

            CBox render_box = {{ws_box.pos() / scale_value}, ws_box.size()};
            if (monitor->m_transform % 2 == 1)
                std::swap(render_box.w, render_box.h);

            if (has_ws && ws_id == start_workspace->m_id && start_workspace != nullptr)
                continue;

            CBox global_box = {ws_box.pos() + monitor->m_position, ws_box.size()};
            if (global_box.expand(border_size).intersection(global_mon_box).empty())
                continue;

            render_border(ws_box, has_ws && start_workspace->m_id == ws_id);

            if (has_ws) {
                const PHLWORKSPACE workspace = get_workspace_from_layout(ws_id);

                if (workspace != nullptr)
                    render_workspace(workspace, render_box, false);
            } else {
                render_workspace(nullptr, render_box, false);
            }
        }
    }

    const auto active_it = overview_layout.find(start_workspace->m_id);
    if (start_workspace != nullptr && active_it != overview_layout.end()) {
        CBox ws_box = active_it->second.box;
        if (ws_box.width > 0.01 && ws_box.height > 0.01) {
            CBox render_box = {{ws_box.pos() / scale_value}, ws_box.size()};
            if (monitor->m_transform % 2 == 1)
                std::swap(render_box.w, render_box.h);

            render_border(ws_box, monitor->m_activeWorkspace->m_id == start_workspace->m_id);
            render_workspace(start_workspace, render_box, true);
        }
    }

    render_dragged_window();
}
