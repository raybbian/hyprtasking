#include <algorithm>
#include <any>
#include <cmath>
#include <sstream>
#include <string_view>

#define private public
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/ClearPassElement.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>
#undef private

#include "../config.hpp"
#include "../globals.hpp"
#include "../pass/pass_element.hpp"
#include "../types.hpp"
#include "layout_base.hpp"

HTLayoutBase::HTLayoutBase(VIEWID new_view_id) : view_id(new_view_id) {
    ;
}

void HTLayoutBase::on_move_swipe(Vector2D delta) {
    ;
}

WORKSPACEID HTLayoutBase::on_move_swipe_end() {
    return WORKSPACE_INVALID;
}

WORKSPACEID HTLayoutBase::get_ws_id_in_direction(int x, int y, std::string& direction) {
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
    return get_ws_id_from_xy(x, y);
}

bool HTLayoutBase::on_mouse_axis(double delta) {
    return false;
}

bool HTLayoutBase::should_manage_mouse() {
    return true;
}

bool HTLayoutBase::should_render_window(PHLWINDOW window) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr || window == nullptr)
        return false;

    return ((should_render_window_t)(should_render_window_hook->m_original))(
        g_pHyprRenderer.get(),
        window,
        monitor
    );
}

float HTLayoutBase::drag_window_scale() {
    return 1.f;
}

void HTLayoutBase::init_position() {
    ;
}

void HTLayoutBase::build_overview_layout(HTViewStage stage) {
    ;
}

void HTLayoutBase::render() {
    CClearPassElement::SClearData data;
    data.color = CHyprColor {0};
    g_pHyprRenderer->m_renderPass.add(makeUnique<CClearPassElement>(data));
}

std::vector<WORKSPACEID> HTLayoutBase::jump_targets() const {
    std::vector<std::pair<WORKSPACEID, HTWorkspace>> ordered;
    ordered.reserve(overview_layout.size());
    for (const auto& entry : overview_layout)
        ordered.push_back(entry);

    std::sort(ordered.begin(), ordered.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.second.y != rhs.second.y)
            return lhs.second.y < rhs.second.y;
        if (lhs.second.x != rhs.second.x)
            return lhs.second.x < rhs.second.x;
        return lhs.first < rhs.first;
    });

    std::vector<WORKSPACEID> result;
    result.reserve(ordered.size());
    for (const auto& entry : ordered)
        result.push_back(entry.first);
    return result;
}

std::optional<WORKSPACEID> HTLayoutBase::jump_target(size_t index) const {
    const auto targets = jump_targets();
    if (index >= targets.size())
        return std::nullopt;
    return targets[index];
}

void HTLayoutBase::render_jump_labels() {
    if (!HTConfig::value<Config::INTEGER>("jump:enabled"))
        return;

    const PHTVIEW view = ht_manager->get_view_from_id(view_id);
    const PHLMONITOR monitor = view == nullptr ? nullptr : view->get_monitor();
    if (view == nullptr || monitor == nullptr || !view->active || view->closing)
        return;

    static constexpr std::string_view LABELS = "1234567890abcdefghijklmnopqrstuvwxyz";
    const auto targets = jump_targets();
    const size_t count = std::min(targets.size(), LABELS.size());

    const int font_size = std::max(
        1,
        static_cast<int>(HTConfig::value<Config::INTEGER>("jump:label_size") * monitor->m_scale)
    );
    const float padding = std::max(4.f, font_size * 0.35f);
    const Config::INTEGER label_color_value = HTConfig::value<Config::INTEGER>("jump:label_color");
    const CHyprColor label_color {label_color_value};
    const CHyprColor background_color {HTConfig::value<Config::INTEGER>("jump:label_background")};
    const CBox monitor_box {{0, 0}, monitor->m_transformedSize};

    for (size_t i = 0; i < count; i++) {
        const auto layout_it = overview_layout.find(targets[i]);
        if (layout_it == overview_layout.end())
            continue;
        const CBox& workspace_box = layout_it->second.box;
        if (workspace_box.intersection(monitor_box).empty())
            continue;

        // Text rasterization is relatively expensive and these glyphs are immutable for a
        // given scale/color, so retain one texture per rendered label style.
        static std::unordered_map<std::string, SP<Render::ITexture>> texture_cache;
        const std::string texture_key = std::string(1, LABELS[i]) + ":" + std::to_string(font_size)
            + ":" + std::to_string(label_color_value);
        auto& texture = texture_cache[texture_key];
        if (texture == nullptr) {
            texture = g_pHyprRenderer->renderText(
                std::string(1, LABELS[i]),
                label_color,
                font_size,
                false,
                "",
                0,
                700
            );
        }
        if (texture == nullptr)
            continue;

        const Vector2D badge_size = texture->m_size + Vector2D {padding * 2.f, padding};
        const Vector2D badge_pos = workspace_box.pos() + (workspace_box.size() - badge_size) / 2.f;
        const CBox badge_box {badge_pos, badge_size};

        CRectPassElement::SRectData badge;
        badge.box = badge_box;
        badge.color = background_color;
        badge.round = std::round(std::min(badge_size.x, badge_size.y) * 0.22f);
        g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(badge));

        CTexPassElement::SRenderData text;
        text.tex = texture;
        text.box = CBox {badge_pos + (badge_size - texture->m_size) / 2.f, texture->m_size};
        g_pHyprRenderer->m_renderPass.add(makeUnique<CTexPassElement>(std::move(text)));
    }
}

const std::string CLEAR_PASS_ELEMENT_NAME = "CClearPassElement";

void HTLayoutBase::post_render() {
    bool first = true;
    std::erase_if(g_pHyprRenderer->m_renderPass.m_passElements, [&first](const auto& e) {
        bool res = e.element->passName() == CLEAR_PASS_ELEMENT_NAME && !first;
        first = false;
        return res;
    });
    render_jump_labels();
    g_pHyprRenderer->m_renderPass.add(makeUnique<HTPassElement>());
    // g_pHyprOpenGL->setDamage(CRegion {CBox {0, 0, INT32_MAX, INT32_MAX}});
}

PHLMONITOR HTLayoutBase::get_monitor() {
    const auto par_view = ht_manager->get_view_from_id(view_id);
    if (par_view == nullptr)
        return nullptr;
    return par_view->get_monitor();
}

WORKSPACEID HTLayoutBase::get_ws_id_from_global(Vector2D pos) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return WORKSPACE_INVALID;

    if (!monitor->logicalBox().containsPoint(pos))
        return WORKSPACE_INVALID;

    Vector2D relative_pos = (pos - monitor->m_position) * monitor->m_scale;
    for (const auto& [id, layout] : overview_layout)
        if (layout.box.containsPoint(relative_pos))
            return id;

    return WORKSPACE_INVALID;
}

WORKSPACEID HTLayoutBase::get_ws_id_from_xy(int x, int y) {
    for (const auto& [id, layout] : overview_layout)
        if (layout.x == x && layout.y == y)
            return id;

    return WORKSPACE_INVALID;
}

CBox HTLayoutBase::get_global_window_box(PHLWINDOW window, WORKSPACEID workspace_id) {
    if (window == nullptr)
        return {};

    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {};

    const PHLWORKSPACE workspace = g_pCompositor->getWorkspaceByID(workspace_id);
    if (workspace == nullptr || workspace->m_monitor != monitor)
        return {};

    const CBox ws_window_box = window->getWindowMainSurfaceBox();

    const Vector2D top_left =
        local_ws_unscaled_to_global(ws_window_box.pos() - monitor->m_position, workspace->m_id);
    const Vector2D bottom_right = local_ws_unscaled_to_global(
        ws_window_box.pos() + ws_window_box.size() - monitor->m_position,
        workspace->m_id
    );

    return {top_left, bottom_right - top_left};
}

CBox HTLayoutBase::get_global_ws_box(WORKSPACEID workspace_id) {
    const CBox scaled_ws_box = overview_layout[workspace_id].box;
    const Vector2D top_left = local_ws_scaled_to_global(scaled_ws_box.pos(), workspace_id);
    const Vector2D bottom_right =
        local_ws_scaled_to_global(scaled_ws_box.pos() + scaled_ws_box.size(), workspace_id);
    return {top_left, bottom_right - top_left};
}

Vector2D HTLayoutBase::global_to_local_ws_unscaled(Vector2D pos, WORKSPACEID workspace_id) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {};

    CBox workspace_box = overview_layout[workspace_id].box;
    if (workspace_box.empty())
        return {};
    pos -= monitor->m_position;
    pos *= monitor->m_scale;
    pos -= workspace_box.pos();
    pos /= monitor->m_scale;
    pos /= workspace_box.w / monitor->m_transformedSize.x;
    return pos;
}

Vector2D HTLayoutBase::global_to_local_ws_scaled(Vector2D pos, WORKSPACEID workspace_id) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {};

    pos = global_to_local_ws_unscaled(pos, workspace_id);
    pos *= monitor->m_scale;
    return pos;
}

Vector2D HTLayoutBase::local_ws_unscaled_to_global(Vector2D pos, WORKSPACEID workspace_id) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {};

    CBox workspace_box = overview_layout[workspace_id].box;
    if (workspace_box.empty())
        return {};
    pos *= workspace_box.w / monitor->m_transformedSize.x;
    pos *= monitor->m_scale;
    pos += workspace_box.pos();
    pos /= monitor->m_scale;
    pos += monitor->m_position;
    return pos;
}

Vector2D HTLayoutBase::local_ws_scaled_to_global(Vector2D pos, WORKSPACEID workspace_id) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {};

    pos /= monitor->m_scale;
    return local_ws_unscaled_to_global(pos, workspace_id);
}
