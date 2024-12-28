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
    m_bActive = false;
    m_bClosing = false;

    const PHLMONITOR pMonitor = getMonitor();
    if (pMonitor == nullptr)
        return;

    m_vOffset.create(
        {0, 0}, g_pConfigManager->getAnimationPropertyConfig("windowsMove"),
        AVARDAMAGE_NONE);
    m_fScale.create(1.f,
                    g_pConfigManager->getAnimationPropertyConfig("windowsMove"),
                    AVARDAMAGE_NONE);
}

void CHyprtaskingView::show() {
    const PHLMONITOR pMonitor = getMonitor();
    if (pMonitor == nullptr)
        return;

    m_bActive = true;

    // Generate workspace boxes for non-animating configuration
    generateWorkspaceBoxes(false);

    const CBox wsBox = workspaceBoxes[pMonitor->activeWorkspaceID()];

    m_fScale.setValueAndWarp(1.);
    m_vOffset.setValueAndWarp(-wsBox.pos() / wsBox.size() *
                              pMonitor->vecPixelSize);
    m_fScale = 1. / ROWS;
    m_vOffset = {0, 0};

    g_pHyprRenderer->damageMonitor(pMonitor);
    g_pCompositor->scheduleFrameForMonitor(pMonitor);
}

void CHyprtaskingView::hide() {
    if (m_bClosing)
        return;
    const PHLMONITOR pMonitor = getMonitor();
    if (pMonitor == nullptr)
        return;

    m_bClosing = true;

    // Generate workspace boxes for non-animating
    generateWorkspaceBoxes(false);

    const CBox wsBox = workspaceBoxes[pMonitor->activeWorkspaceID()];

    m_fScale = 1.;
    m_vOffset = -wsBox.pos() / wsBox.size() * pMonitor->vecPixelSize;

    m_fScale.setCallbackOnEnd([this](void *) {
        m_bActive = false;
        m_bClosing = false;
    });

    g_pHyprRenderer->damageMonitor(pMonitor);
    g_pCompositor->scheduleFrameForMonitor(pMonitor);
}

Vector2D
CHyprtaskingView::mapGlobalPositionToWsGlobal(Vector2D pos,
                                              WORKSPACEID workspaceID) {
    const PHLMONITOR pMonitor = getMonitor();
    if (pMonitor == nullptr)
        return {};

    CBox workspaceBox = getWorkspaceBoxFromID(workspaceID);
    if (workspaceBox.empty())
        return {};

    pos *= pMonitor->scale;
    pos -= workspaceBox.pos();
    pos *= ROWS;
    pos += pMonitor->vecPosition;
    pos /= pMonitor->scale;
    return pos;
}

Vector2D
CHyprtaskingView::mapWsGlobalPositionToGlobal(Vector2D pos,
                                              WORKSPACEID workspaceID) {
    const PHLMONITOR pMonitor = getMonitor();
    if (pMonitor == nullptr)
        return {};

    CBox workspaceBox = getWorkspaceBoxFromID(workspaceID);
    if (workspaceBox.empty())
        return {};

    pos *= pMonitor->scale;
    pos -= pMonitor->vecPosition;
    pos /= ROWS;
    pos += workspaceBox.pos();
    pos /= pMonitor->scale;
    return pos;
}

WORKSPACEID CHyprtaskingView::getWorkspaceIDFromVector(Vector2D pos) {
    const PHLMONITOR pMonitor = getMonitor();
    if (pMonitor == nullptr)
        return WORKSPACE_INVALID;
    // mousePos is relative to (0, 0), whereas workspaceBoxes are relative
    // to the monitor. Make mousePos relative to the monitor.
    Vector2D relPos = (pos - pMonitor->vecPosition) * pMonitor->scale;
    for (const auto &[id, box] : workspaceBoxes) {
        if (box.containsPoint(relPos)) {
            return id;
        }
    }
    return WORKSPACE_INVALID;
}

CBox CHyprtaskingView::getWorkspaceBoxFromID(WORKSPACEID workspaceID) {
    const PHLMONITOR pMonitor = getMonitor();
    if (pMonitor == nullptr)
        return {};

    if (workspaceBoxes.count(workspaceID)) {
        CBox oBox = workspaceBoxes[workspaceID];
        // NOTE: not scaled by monitor size
        return {pMonitor->vecPosition + oBox.pos(), oBox.size()};
    }

    return {};
}

PHLMONITOR CHyprtaskingView::getMonitor() {
    return g_pCompositor->getMonitorFromID(monitorID);
}

CHyprtaskingManager::CHyprtaskingManager() {
    dragWindowOffset.create(
        {0, 0}, g_pConfigManager->getAnimationPropertyConfig("windowsMove"),
        AVARDAMAGE_NONE);
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

void CHyprtaskingManager::show() {
    for (auto view : m_vViews) {
        view->show();
    }
}

void CHyprtaskingManager::hide() {
    for (const auto &view : m_vViews) {
        view->hide();
    }
}

void CHyprtaskingManager::reset() { m_vViews.clear(); }

bool CHyprtaskingManager::isActive() {
    for (const auto &view : m_vViews) {
        if (view->m_bActive)
            return true;
    }
    return false;
}
