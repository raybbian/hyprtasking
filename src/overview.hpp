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

    bool m_bClosing;

  public:
    bool m_bActive;

    // Set this to hide at non-active workspace
    PHLWORKSPACEREF hideAt;

    CHyprtaskingView(MONITORID inMonitorID);

    PHLMONITOR getMonitor();

    void show();
    void hide();
    void render();

    // If return value < WORKSPACEID, then there is nothing there
    WORKSPACEID getWorkspaceIDFromVector(Vector2D pos);

    // Returns the CBox relative to (0, 0), not the monitor
    // If CBox == {0, 0, 0, 0}, then there was no ws with that ID
    CBox getWorkspaceBoxFromID(WORKSPACEID workspaceID);

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

    void show();
    void hide();
    void reset();

    void onMouseButton(bool pressed, uint32_t button);
    void onMouseMove();

    bool isActive();
};

inline std::unique_ptr<CHyprtaskingManager> g_pHyprtasking;
