#include "config.hpp"
#include "globals.hpp"
#include "layout/dwindle_snapshot.hpp"
#include "manager.hpp"

#include <hyprland/src/desktop/state/GlobalWindowController.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/window/WindowGroupMembership.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/desktop/view/window/WindowSwallowController.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/layout/space/Space.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>

#include <algorithm>
#include <linux/input-event-codes.h>
#include <hyprland/src/managers/SessionLockManager.hpp>

namespace {
struct WindowMove {
    PHLWINDOW window;
    PHLWORKSPACE from, to;
    Fullscreen::SFullscreenMode fullscreen;
    bool layout_fullscreen;
    bool floating;
    CBox floating_box;
};

bool movable(const PHLWINDOW& window) {
    return window->mapped() && !window->isHidden()
        && !(window->m_state & Desktop::View::WINDOW_STATE_PINNED)
        && !window->grouping().group() && !window->swallowing().swallowee()
        && window->layoutTarget() && window->layoutTarget()->space() == window->m_workspace->space();
}

bool supported_contents(const PHLWORKSPACE& a, const PHLWORKSPACE& b) {
    if (!a && !b)
        return true;
    return std::ranges::all_of(Desktop::windowState()->windows(), [&](const auto& window) {
        if (!window->m_workspace || (window->m_workspace != a && window->m_workspace != b))
            return true;
        // Unmapped windows with no layout target are closing and have left the layout.
        if (!window->mapped() && !window->layoutTarget()->space())
            return true;
        return movable(window);
    });
}

void restore_window(const WindowMove& move, const PHLWORKSPACE& workspace) {
    const auto target = move.window->layoutTarget();
    if (target->floating() != move.floating)
        g_layoutManager->changeFloatingMode(target);
    if (move.floating && target->floating()) {
        const auto old_area = move.from->space()->workArea(true);
        const auto new_area = workspace->space()->workArea(true);
        auto box = move.floating_box;
        if (workspace != move.from && move.from->m_monitor != workspace->m_monitor
            && !old_area.empty() && !new_area.empty()) {
            const auto center = (box.middle() - old_area.pos()) / old_area.size();
            const auto size = box.size().clamp(Vector2D{1, 1}, new_area.size());
            const auto position = (new_area.pos() + center * new_area.size() - size / 2)
                .clamp(new_area.pos(), new_area.pos() + new_area.size() - size);
            box = CBox{position, size};
        }
        g_layoutManager->setTargetGeom(box, target);
    }
    auto& presentation = move.window->presentation();
    presentation.alpha(Desktop::View::WINDOW_ALPHA_MOVE_TO_WORKSPACE)->setValueAndWarp(1.f);
    presentation.alpha(Desktop::View::WINDOW_ALPHA_MOVE_FROM_WORKSPACE)->setValueAndWarp(1.f);
}

void draw_rect(Render::CRenderContext& ctx, const CBox& box, const CHyprColor& color) {
    if (box.empty())
        return;
    CRectPassElement::SRectData data;
    data.box = box;
    data.color = color;
    IHyprRenderer::addPassElement(ctx, makeUnique<CRectPassElement>(data));
}
}

CBox HTManager::workspace_handle(const CBox& tile, double scale) const {
    const double size = std::min(
        scale * std::clamp<Config::INTEGER>(HTConfig::value<Config::INTEGER>("workspace_drag:handle_size"), 8, 96),
        std::min(tile.w, tile.h) / 3.0);
    return {tile.x + tile.w - size - scale * 3, tile.y + scale * 3, size, size};
}

bool HTManager::can_swap_tiles(const PHTVIEW& source, WORKSPACEID source_id,
                               const PHTVIEW& destination, WORKSPACEID destination_id) const {
    if (!source || !destination || !source->active || source->closing
        || !destination->active || destination->closing || source_id <= 0 || destination_id <= 0
        || source_id == destination_id || !destination->layout->should_manage_mouse())
        return false;
    const auto source_monitor = source->get_monitor();
    const auto destination_monitor = destination->get_monitor();
    if (!source_monitor || !destination_monitor || source_monitor->m_activeSpecialWorkspace
        || destination_monitor->m_activeSpecialWorkspace)
        return false;

    const auto a = find_workspace(source_id);
    const auto b = find_workspace(destination_id);
    return (!a || a->m_monitor.lock() == source_monitor)
        && (!b || b->m_monitor.lock() == destination_monitor)
        && supported_contents(a, b);
}

bool HTManager::workspace_drag_button(unsigned int button, bool pressed) {
    if (button != BTN_LEFT)
        return false;
    if (workspace_press) {
        if (!pressed) {
            const auto source = *workspace_press;
            workspace_press.reset();
            if (source.dragging && source.workspace != WORKSPACE_INVALID
                && !g_pSessionLockManager->isSessionLocked() && swap_workspace_contents(source)
                && HTConfig::value<Config::INTEGER>("workspace_drag:close_overview_after_swap"))
                hide_all_views();
        }
        return true;
    }
    if (!pressed || !HTConfig::value<Config::INTEGER>("workspace_drag:enabled")
        || g_pSessionLockManager->isSessionLocked() || g_layoutManager->dragController()->target())
        return false;

    const auto view = get_view_from_cursor();
    if (!view || !view->active || view->closing || !view->layout->should_manage_mouse())
        return false;
    const auto monitor = view->get_monitor();
    if (!monitor || monitor->m_activeSpecialWorkspace)
        return false;

    const auto pointer = g_pInputManager->getMouseCoordsInternal();
    const auto id = view->layout->get_ws_id_from_global(pointer);
    const auto tile = view->layout->overview_layout.find(id);
    if (id <= 0 || tile == view->layout->overview_layout.end()
        || !workspace_handle(tile->second.box, monitor->m_scale)
                .containsPoint((pointer - monitor->m_position) * monitor->m_scale))
        return false;
    workspace_press = WorkspacePress{view->monitor_id, id, pointer};
    return true;
}

bool HTManager::workspace_drag_motion() {
    if (!workspace_press)
        return false;
    if (g_pSessionLockManager->isSessionLocked())
        cancel_workspace_drag();
    if (workspace_press->workspace != WORKSPACE_INVALID
        && g_pInputManager->getMouseCoordsInternal().distanceSq(workspace_press->origin) > 36)
        workspace_press->dragging = true;
    return true;
}

bool HTManager::cancel_workspace_drag() {
    if (!workspace_press)
        return false;
    workspace_press->workspace = WORKSPACE_INVALID;
    workspace_press->dragging = false;
    return true;
}

bool HTManager::swap_workspace_contents(const WorkspacePress& press) {
    const auto source = get_view_from_id(press.monitor);
    const auto destination = get_view_from_cursor();
    if (!destination || g_layoutManager->dragController()->target())
        return false;
    const auto id = destination->layout->get_ws_id_from_global(g_pInputManager->getMouseCoordsInternal());
    if (!can_swap_tiles(source, press.workspace, destination, id)) {
        Log::logger->log(LOG, std::format("[Hyprtasking] Rejected workspace drop from {} to {}", press.workspace, id));
        return false;
    }

    const auto source_monitor = source->get_monitor();
    const auto destination_monitor = destination->get_monitor();
    auto a = find_workspace(press.workspace);
    auto b = find_workspace(id);
    if (!a && !b)
        return false;
    if (!a)
        a = create_workspace(press.workspace, source_monitor);
    if (!b)
        b = create_workspace(id, destination_monitor);
    if (!a || !b || a->m_monitor.lock() != source_monitor || b->m_monitor.lock() != destination_monitor)
        return false;

    // Snapshot both sets before moving anything. Keep both workspace objects
    // alive even if native moves momentarily empty either one.
    auto& fullscreen = *Fullscreen::controller();
    std::vector<WindowMove> moves;
    for (const auto& window : Desktop::windowState()->windows()) {
        if (!window->mapped() || (window->m_workspace != a && window->m_workspace != b))
            continue;
        const auto from = window->m_workspace;
        moves.push_back({window, from, from == a ? b : a, fullscreen.getFullscreenModes(window),
                         fullscreen.layoutManagedFS(window), window->isFloating(), {}});
    }
    for (const auto& move : moves) {
        if (move.fullscreen.internal != Fullscreen::FSMODE_NONE)
            fullscreen.setFullscreenMode(move.window, Fullscreen::FSMODE_NONE, Fullscreen::FSMODE_NONE);
    }
    for (auto& move : moves)
        move.floating_box = move.window->layoutTarget()->position();

    const auto a_tree = HTDwindle::capture(a);
    const auto b_tree = HTDwindle::capture(b);
    auto& windows = *Desktop::globalWindowController();
    bool success = true;
    for (const auto& move : moves) {
        windows.moveWindowToWorkspace(move.window, move.to);
        if (move.window->m_workspace != move.to || move.window->layoutTarget()->space() != move.to->space()) {
            success = false;
            break;
        }
    }
    if (!success) {
        for (const auto& move : moves) {
            if (move.window->m_workspace != move.from)
                windows.moveWindowToWorkspace(move.window, move.from);
        }
        Log::logger->log(ERR, "[Hyprtasking] Workspace swap failed; restored original membership");
    }
    for (const auto& move : moves)
        restore_window(move, success ? move.to : move.from);

    const bool restored_a = a_tree && HTDwindle::restore(*a_tree, success ? b : a);
    const bool restored_b = b_tree && HTDwindle::restore(*b_tree, success ? a : b);
    if (!restored_a || !restored_b)
        Log::logger->log(LOG, "[Hyprtasking] A workspace uses native placement for its tiled windows");
    for (const auto& move : moves) {
        if (move.fullscreen.internal != Fullscreen::FSMODE_NONE) {
            fullscreen.setFullscreenMode(move.window, move.fullscreen.internal, move.fullscreen.client,
                                         move.layout_fullscreen);
        }
    }

    a->ready();
    b->ready();
    g_pHyprRenderer->damageMonitor(source_monitor);
    g_pHyprRenderer->damageMonitor(destination_monitor);
    if (const auto monitor = Desktop::focusState()->monitor(); monitor && monitor->m_activeWorkspace) {
        const auto focused = Desktop::focusState()->window();
        if (focused && (!focused->m_workspace || !focused->m_workspace->visible()
            || focused->m_monitor.lock() != monitor))
            Desktop::focusState()->fullWindowFocus(
                monitor->m_activeWorkspace->getFirstWindow(), Desktop::FOCUS_REASON_DESKTOP_STATE_CHANGE);
    }
    if (success)
        Log::logger->log(LOG, std::format("[Hyprtasking] Swapped {} windows between workspaces {} and {}",
                                         moves.size(), press.workspace, id));
    return success;
}

void HTManager::render_workspace_handles(Render::CRenderContext& ctx, const PHTVIEW& view) {
    if (!view->active || view->closing || !HTConfig::value<Config::INTEGER>("workspace_drag:enabled"))
        return;
    const auto monitor = view->get_monitor();
    if (!monitor)
        return;
    const auto cursor = get_view_from_cursor();
    const auto pointer = g_pInputManager->getMouseCoordsInternal();
    const bool dragging = workspace_press && workspace_press->dragging;
    const auto hovered_id = cursor == view ? view->layout->get_ws_id_from_global(pointer) : WORKSPACE_INVALID;
    const bool valid = dragging && can_swap_tiles(get_view_from_id(workspace_press->monitor), workspace_press->workspace, view, hovered_id);
    const CBox output{{0, 0}, monitor->m_transformedSize};

    for (const auto& [id, layout] : view->layout->overview_layout) {
        if (id <= 0 || layout.box.intersection(output).empty())
            continue;
        const bool source = dragging && view->monitor_id == workspace_press->monitor && id == workspace_press->workspace;
        const bool hovered = dragging && id == hovered_id;
        const CHyprColor accent = source ? CHyprColor{0xfff3be6f} : hovered
            ? (valid ? CHyprColor{0xff79e6ad} : CHyprColor{0xfff47777}) : CHyprColor{0xc0ffffff};

        if (hovered || source)
            draw_rect(ctx, CBox{layout.box.x, layout.box.y, layout.box.w, 3 * monitor->m_scale}, accent);

        const auto handle = workspace_handle(layout.box, monitor->m_scale);
        draw_rect(ctx, handle, CHyprColor{0xdd20242c});
        for (int line = 1; line <= 3; ++line)
            draw_rect(ctx, CBox{handle.x + handle.w * .22, handle.y + handle.h * line / 4,
                                handle.w * .56, std::max(1.0, handle.h * .08)}, accent);
    }

    if (dragging && cursor == view && hovered_id == WORKSPACE_INVALID) {
        const auto pos = (pointer - monitor->m_position) * monitor->m_scale + Vector2D{12, 12};
        draw_rect(ctx, CBox{pos, Vector2D{8, 8}}, CHyprColor{0xfff47777});
    }
}
