#pragma once

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/shared/workspace/WorkspaceRule.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>
#include <hyprland/src/state/workspace/Resolver.hpp>
#include <hyprland/src/workspace/HLWorkspace.hpp>
#include <hyprutils/math/Box.hpp>

using namespace Render;

typedef void (*render_workspace_t)(
    void* thisptr,
    CRenderContext& ctx,
    PHLMONITOR pMonitor,
    PHLWORKSPACE pWorkspace,
    const Time::steady_tp& now,
    const CBox& geometry
);

typedef bool (*should_render_window_t)(void* thisptr, PHLWINDOW pWindow, PHLMONITOR pMonitor);
typedef void (*render_window_t)(
    void* thisptr,
    CRenderContext& ctx,
    PHLWINDOW pWindow,
    PHLMONITOR pMonitor,
    const SWindowRenderPresentation& presentation,
    const Time::steady_tp& time,
    bool decorate,
    eRenderPassMode mode,
    bool ignorePosition,
    bool standalone
);

typedef long VIEWID;

// Hyprland 0.57 replaced integer workspace IDs with a numbered/special variant.
// Hyprtasking only lays out numbered workspaces, so it keeps integer IDs and
// treats named and special workspaces as WORKSPACE_INVALID.
typedef int64_t WORKSPACEID;
inline constexpr WORKSPACEID WORKSPACE_INVALID = -1;

inline WORKSPACEID get_ws_id(const PHLWORKSPACE& workspace) {
    if (workspace == nullptr)
        return WORKSPACE_INVALID;
    const auto id = workspace->numberedID();
    return id.has_value() ? (WORKSPACEID)*id : WORKSPACE_INVALID;
}

inline PHLWORKSPACE find_workspace(WORKSPACEID id) {
    if (id <= 0 || id > UINT32_MAX)
        return nullptr;
    return State::workspaceState()
        ->query()
        .numbered(Workspace::SWorkspaceNumberedID((Workspace::WorkspaceIDContainer)id))
        .run();
}

inline PHLWORKSPACE create_workspace(WORKSPACEID id, PHLMONITOR monitor) {
    if (id <= 0 || id > UINT32_MAX)
        return nullptr;
    return State::workspaceState()->createNumbered(
        Workspace::SWorkspaceNumberedID((Workspace::WorkspaceIDContainer)id),
        monitor
    );
}

inline WORKSPACEID rule_ws_id(const Config::CWorkspaceRule& rule) {
    const auto target =
        State::Workspace::resolver()->getWorkspaceTargetFromString(rule.m_workspaceString);
    if (!target.id.has_value())
        return WORKSPACE_INVALID;
    const auto* numbered = std::get_if<Workspace::SWorkspaceNumberedID>(&*target.id);
    return numbered != nullptr ? (WORKSPACEID)numbered->value : WORKSPACE_INVALID;
}
