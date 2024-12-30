#pragma once

#include <cstdint>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/AnimatedVariable.hpp>
#include <hyprland/src/macros.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Vector2D.hpp>

#include "types.hpp"

struct CHyprtaskingView {
  private:
    MONITORID monitorID;

    // Store the bounding boxes of each workspace as rendered. Modified on
    // render and accessed during mouse button events.
    // TODO: is there a better way to do this?
    // NOTE: workspace boxes do not consider monitor scaling
    std::unordered_map<WORKSPACEID, CBox> workspaceBoxes;

    void generateWorkspaceBoxes(bool useAnimModifs = true);

    CAnimatedVariable<Vector2D> m_vOffset;
    CAnimatedVariable<float> m_fScale;

    // Workspace that the overview was opened from
    PHLWORKSPACEREF m_pOriWorkspace;

    bool trySwitchToHover();
    bool trySwitchToOriginal();

  public:
    bool m_bClosing;
    bool m_bActive;

    CHyprtaskingView(MONITORID inMonitorID);

    PHLMONITOR getMonitor();

    void show();
    void hide();
    void render();

    // Use to switch to the proper workspace depending on behavior before
    // exiting. If overrideHover, we pref hover first over all else
    void doOverviewExitBehavior(bool overrideHover = false);

    // If return value < WORKSPACEID, then there is nothing there
    WORKSPACEID getWorkspaceIDFromGlobal(Vector2D pos);

    CBox getGlobalWorkspaceBoxFromID(WORKSPACEID workspaceID);

    CBox getGlobalWindowBox(PHLWINDOW pWindow);

    Vector2D mapGlobalPositionToWsGlobal(Vector2D pos, WORKSPACEID workspaceID);
    Vector2D mapWsGlobalPositionToGlobal(Vector2D pos, WORKSPACEID workspaceID);
};

struct CHyprtaskingManager {
  public:
    CHyprtaskingManager();

    std::vector<PHTVIEW> m_vViews;

    // So that the window doesn't teleport to the mouse's position
    CAnimatedVariable<Vector2D> dragWindowOffset;

    PHTVIEW getViewFromMonitor(PHLMONITOR pMonitor);
    PHTVIEW getViewFromCursor();

    void reset();

    void showAllViews();
    void hideAllViews();
    void showCursorView();

    void onMouseButton(bool pressed, uint32_t button);
    void onMouseMove();

    bool hasActiveView();
    bool cursorViewActive();

    bool shouldRenderWindow(PHLWINDOW pWindow, PHLMONITOR pMonitor);
};

inline std::unique_ptr<CHyprtaskingManager> g_pHyprtasking;
