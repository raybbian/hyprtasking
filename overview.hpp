#pragma once

#include <cstdint>
#include <utility>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Vector2D.hpp>

#include "types.hpp"

struct CHyprtaskingView {
  private:
    MONITORID monitorID;

    // Store the bounding boxes of each workspace as rendered. Modified on
    // render and accessed during mouse button events.
    // TODO: is there a better way to do this?
    std::vector<std::pair<WORKSPACEID, CBox>> workspaceBoxes;

  public:
    CHyprtaskingView(MONITORID inMonitorID);
    ~CHyprtaskingView();

    PHLMONITOR getMonitor();

    void render();

    // If return value < WORKSPACEID, then there is nothing there
    WORKSPACEID getWorkspaceIDFromVector(Vector2D pos);
    // If CBox == {0, 0, 0, 0}, then there was no ws with that ID
    // Returns the CBox relative to (0, 0), not the monitor
    CBox getWorkspaceBoxFromID(WORKSPACEID workspaceID);
    // pWorkspace by default is the monitor's active workspace
    Vector2D mouseCoordsWorkspaceRelative(Vector2D mousePos,
                                          PHLWORKSPACE pWorkspace = nullptr);
};

struct CHyprtaskingManager {
  private:
    bool m_bActive;

  public:
    std::vector<PHTVIEW> m_vViews;

    PHTVIEW getViewFromMonitor(PHLMONITOR pMonitor);
    PHTVIEW getViewFromCursor();

    void show();
    void hide();
    void reset();

    void onMouseButton(bool pressed, uint32_t button);
    void onMouseMove();

    bool isActive();
};

inline std::unique_ptr<CHyprtaskingManager> g_pHyprtasking;
