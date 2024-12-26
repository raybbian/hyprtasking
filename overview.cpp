#include <ctime>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/macros.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/math/Box.hpp>

#include "globals.hpp"
#include "overview.hpp"

CHyprtaskingView::CHyprtaskingView(MONITORID inMonitorID) {
    monitorID = inMonitorID;
}

CHyprtaskingView::~CHyprtaskingView() {}

Vector2D
CHyprtaskingView::mouseCoordsWorkspaceRelative(Vector2D mousePos,
                                               PHLWORKSPACE pWorkspace) {
    const PHLMONITOR pMonitor = getMonitor();
    if (pMonitor == nullptr)
        return mousePos;

    if (pWorkspace == nullptr)
        pWorkspace = pMonitor->activeWorkspace;
    if (pWorkspace == nullptr)
        return mousePos;

    // mousePos is relative to (0, 0), whereas workspaceBoxes are relative
    // to the monitor. Make mousePos relative to the monitor.
    Vector2D relMousePos = mousePos - pMonitor->vecPosition;

    CBox workspaceBox{};
    for (const auto &[id, box] : workspaceBoxes) {
        if (pWorkspace->m_iID == id)
            workspaceBox = box;
    }

    if (workspaceBox.w == 0)
        return mousePos;

    Vector2D offset = relMousePos - workspaceBox.pos();
    // The offset between mouse position and workspace box position must be
    // scaled up to the actual size of the workspace
    offset *= ROWS;
    // Make offset relative to (0, 0) again
    offset += pMonitor->vecPosition;

    return offset;
}

WORKSPACEID CHyprtaskingView::getWorkspaceIDFromVector(Vector2D pos) {
    const PHLMONITOR pMonitor = getMonitor();
    if (pMonitor == nullptr)
        return SPECIAL_WORKSPACE_START - 1;
    // mousePos is relative to (0, 0), whereas workspaceBoxes are relative
    // to the monitor. Make mousePos relative to the monitor.
    Vector2D relPos = pos - pMonitor->vecPosition;
    for (const auto &[id, box] : workspaceBoxes) {
        if (box.containsPoint(relPos)) {
            return id;
        }
    }
    return SPECIAL_WORKSPACE_START - 1;
}

CBox CHyprtaskingView::getWorkspaceBoxFromID(WORKSPACEID workspaceID) {
    const PHLMONITOR pMonitor = getMonitor();
    if (pMonitor == nullptr)
        return {};
    for (const auto &[id, box] : workspaceBoxes) {
        if (id == workspaceID) {
            CBox newBox = {pMonitor->vecPosition + box.pos(), box.size()};
            return newBox;
        }
    }
    return {};
}

PHLMONITOR CHyprtaskingView::getMonitor() {
    return g_pCompositor->getMonitorFromID(monitorID);
}

PHTVIEW CHyprtaskingManager::getViewFromMonitor(PHLMONITOR pMonitor) {
    if (pMonitor == nullptr)
        return nullptr;
    for (PHTVIEW pView : m_vViews) {
        if (pView == nullptr)
            continue;
        if (pView->getMonitor() != pMonitor)
            continue;
        return pView;
    }
    return nullptr;
}

PHTVIEW CHyprtaskingManager::getViewFromCursor() {
    const PHLMONITOR pMonitor = g_pCompositor->getMonitorFromCursor();
    return getViewFromMonitor(pMonitor);
}

void CHyprtaskingManager::show() {
    m_bActive = true;
    for (auto view : m_vViews) {
        PHLMONITOR pMonitor = view->getMonitor();
        if (pMonitor == nullptr)
            continue;
        g_pHyprRenderer->damageMonitor(pMonitor);
        g_pCompositor->scheduleFrameForMonitor(pMonitor);
    }
}

void CHyprtaskingManager::hide() {
    m_bActive = false;
    for (auto view : m_vViews) {
        PHLMONITOR pMonitor = view->getMonitor();
        if (pMonitor == nullptr)
            continue;
        g_pHyprRenderer->damageMonitor(pMonitor);
        g_pCompositor->scheduleFrameForMonitor(pMonitor);
    }
}

void CHyprtaskingManager::reset() {
    m_bActive = false;
    m_vViews.clear();
}

bool CHyprtaskingManager::isActive() { return m_bActive; }
