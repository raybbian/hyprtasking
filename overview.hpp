#pragma once

#include <utility>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Vector2D.hpp>

struct CHyprtaskingView {
  private:
    MONITORID monitorID;
    bool active;

    // Store the bounding boxes of each workspace as rendered. Modified on
    // render and accessed during mouse button events.
    // TODO: is there a better way to do this?
    std::vector<std::pair<WORKSPACEID, CBox>> workspaceBoxes;

  public:
    CHyprtaskingView(MONITORID inMonitorID);
    ~CHyprtaskingView();

    PHLMONITOR getMonitor();

    void show();
    void hide();
    void render();

    // These two fns are called inside the hook for getMouseCoordsInternal, so
    // we cannot use that function within these methods
    PHLWORKSPACE mouseWorkspace(Vector2D mousePos);
    // pWorkspace by default is the monitor's active workspace
    Vector2D mouseCoordsWorkspaceRelative(Vector2D mousePos,
                                          PHLWORKSPACE pWorkspace = nullptr);

    bool isActive();
};

inline std::vector<std::shared_ptr<CHyprtaskingView>> g_vOverviews;
