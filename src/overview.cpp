#include <ctime>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/macros.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/math/Box.hpp>

#include "globals.hpp"
#include "overview.hpp"

CHyprtaskingView::CHyprtaskingView(MONITORID inMonitorID) {
    monitorID = inMonitorID;
    m_bActive = false;
    m_bClosing = false;

    m_vOffset.create(
        {0, 0}, g_pConfigManager->getAnimationPropertyConfig("windowsMove"),
        AVARDAMAGE_NONE);
    m_fScale.create(1.f,
                    g_pConfigManager->getAnimationPropertyConfig("windowsMove"),
                    AVARDAMAGE_NONE);

    // Adjust offset to match workspace
    const PHLMONITOR pMonitor = getMonitor();
    if (pMonitor == nullptr)
        return;
    generateWorkspaceBoxes(false);
    const CBox wsBox = workspaceBoxes[getMonitor()->activeWorkspaceID()];
    m_vOffset.setValueAndWarp(-wsBox.pos() / wsBox.size() *
                              pMonitor->vecPixelSize);
}

bool CHyprtaskingView::trySwitchToHover() {
    const PHLMONITOR pMonitor = g_pCompositor->getMonitorFromCursor();
    // If the cursor monitor is not the view monitor, then we cannot
    // exit to hover
    if (pMonitor != getMonitor())
        return false;

    const Vector2D mouseCoords = g_pInputManager->getMouseCoordsInternal();
    const WORKSPACEID workspaceID = getWorkspaceIDFromGlobal(mouseCoords);
    PHLWORKSPACE pWorkspace = g_pCompositor->getWorkspaceByID(workspaceID);

    // If hovering on new, then create new
    if (pWorkspace == nullptr && workspaceID != WORKSPACE_INVALID) {
        pWorkspace =
            g_pCompositor->createNewWorkspace(workspaceID, pMonitor->ID);

        Debug::log(LOG, "[Hyprtasking] Creating new workspace {}", workspaceID);
    }

    // Failed to create new or hovering over dead space
    if (pWorkspace == nullptr)
        return false;

    pMonitor->changeWorkspace(pWorkspace, true);
    pWorkspace->startAnim(true, false, true);
    pWorkspace->m_bVisible = true;
    return true;
}

bool CHyprtaskingView::trySwitchToOriginal() {
    const PHLMONITOR pMonitor = getMonitor();
    if (pMonitor == nullptr) //???
        return false;

    PHLWORKSPACE oWorkspace = m_pOriWorkspace.lock();

    // If original workspace died (rip), then we go next
    if (oWorkspace == nullptr)
        return false;

    pMonitor->changeWorkspace(oWorkspace, true);
    oWorkspace->startAnim(true, false, true);
    oWorkspace->m_bVisible = true;
    return true;
}

void CHyprtaskingView::doOverviewExitBehavior(bool overrideHover) {
    static auto const *PEXIT_BEHAVIOR =
        (Hyprlang::STRING const *)HyprlandAPI::getConfigValue(
            PHANDLE, "plugin:hyprtasking:exit_behavior")
            ->getDataStaticPtr();

    if (overrideHover && trySwitchToHover())
        return;

    CVarList exitBehavior{*PEXIT_BEHAVIOR, 0, 's', true};

    for (const auto &behavior : exitBehavior) {
        if (behavior == "hovered") {
            if (trySwitchToHover())
                return;
        } else if (behavior == "original") {
            if (trySwitchToOriginal())
                return;
        } else {
            // if inavlid string or 'interacted', then we default to last
            // interacted this requires us to do nothing, as we will zoom into
            // the last focused ws on the monitor
            break;
        }
    }
}

void CHyprtaskingView::show() {
    if (m_bActive)
        return;
    const PHLMONITOR pMonitor = getMonitor();
    if (pMonitor == nullptr)
        return;

    m_bActive = true;
    m_pOriWorkspace = pMonitor->activeWorkspace;

    generateWorkspaceBoxes(false);
    const CBox wsBox = workspaceBoxes[pMonitor->activeWorkspaceID()];

    m_fScale = wsBox.w / pMonitor->vecPixelSize.x; // 1 / ROWS
    m_vOffset = {0, 0};

    g_pHyprRenderer->damageMonitor(pMonitor);
    g_pCompositor->scheduleFrameForMonitor(pMonitor);
}

void CHyprtaskingView::hide() {
    if (m_bClosing || !m_bActive)
        return;
    const PHLMONITOR pMonitor = getMonitor();
    if (pMonitor == nullptr)
        return;

    m_bClosing = true;
    m_pOriWorkspace.reset();

    generateWorkspaceBoxes(false);
    const CBox wsBox = workspaceBoxes[pMonitor->activeWorkspaceID()];

    m_fScale = 1.;
    m_vOffset = -wsBox.pos() / wsBox.size() * pMonitor->vecPixelSize;

    m_fScale.setCallbackOnEnd([this](void *) {
        m_bActive = false;
        m_bClosing = false;
        const PHLWORKSPACE activeWs = getMonitor()->activeWorkspace;
        if (activeWs == nullptr)
            return;
        const PHLWINDOW activeWindow = activeWs->getLastFocusedWindow();
        if (activeWindow)
            g_pCompositor->focusWindow(activeWindow);
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

    CBox workspaceBox = getGlobalWorkspaceBoxFromID(workspaceID);
    if (workspaceBox.empty())
        return {};

    pos *= pMonitor->scale;
    pos -= workspaceBox.pos();
    pos /= m_fScale.value(); // multiply by ROWS
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

    CBox workspaceBox = getGlobalWorkspaceBoxFromID(workspaceID);
    if (workspaceBox.empty())
        return {};

    pos *= pMonitor->scale;
    pos -= pMonitor->vecPosition;
    pos *= m_fScale.value(); // divide by ROWS
    pos += workspaceBox.pos();
    pos /= pMonitor->scale;
    return pos;
}

WORKSPACEID CHyprtaskingView::getWorkspaceIDFromGlobal(Vector2D pos) {
    const PHLMONITOR pMonitor = getMonitor();
    if (pMonitor == nullptr)
        return WORKSPACE_INVALID;
    Vector2D relPos = (pos - pMonitor->vecPosition) * pMonitor->scale;
    for (const auto &[id, box] : workspaceBoxes)
        if (box.containsPoint(relPos))
            return id;
    return WORKSPACE_INVALID;
}

CBox CHyprtaskingView::getGlobalWorkspaceBoxFromID(WORKSPACEID workspaceID) {
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

CBox CHyprtaskingView::getGlobalWindowBox(PHLWINDOW pWindow) {
    const PHLMONITOR pMonitor = getMonitor();
    if (pMonitor == nullptr || pWindow == nullptr)
        return {};
    PHLWORKSPACE pWorkspace = pWindow->m_pWorkspace;
    if (pWorkspace == nullptr || pWorkspace->m_pMonitor != pMonitor)
        return {};

    CBox wsWindowBox = {pWindow->m_vRealPosition.value(),
                        pWindow->m_vRealSize.value()};

    Vector2D topLeftNoScale =
        mapWsGlobalPositionToGlobal(wsWindowBox.pos(), pWorkspace->m_iID);
    Vector2D bottomRightNoScale = mapWsGlobalPositionToGlobal(
        wsWindowBox.pos() + wsWindowBox.size(), pWorkspace->m_iID);

    return {topLeftNoScale, bottomRightNoScale - topLeftNoScale};
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
PHTVIEW CHyprtaskingManager::getViewFromCursor() {
    return getViewFromMonitor(g_pCompositor->getMonitorFromCursor());
}

void CHyprtaskingManager::showAllViews() {
    for (PHTVIEW pView : m_vViews) {
        if (pView == nullptr)
            continue;
        pView->show();
    }
}

void CHyprtaskingManager::hideAllViews() {
    for (PHTVIEW pView : m_vViews) {
        if (pView == nullptr)
            continue;
        pView->doOverviewExitBehavior();
        pView->hide();
    }
}

void CHyprtaskingManager::showCursorView() {
    const PHTVIEW pView = getViewFromCursor();
    if (pView != nullptr)
        pView->show();
}

void CHyprtaskingManager::reset() { m_vViews.clear(); }

bool CHyprtaskingManager::hasActiveView() {
    for (const auto &view : m_vViews) {
        if (view == nullptr)
            continue;
        if (view->m_bActive)
            return true;
    }
    return false;
}

bool CHyprtaskingManager::cursorViewActive() {
    const PHTVIEW pView = getViewFromCursor();
    if (pView == nullptr)
        return false;
    return pView->m_bActive;
}

bool CHyprtaskingManager::shouldRenderWindow(PHLWINDOW pWindow,
                                             PHLMONITOR pMonitor) {
    if (pWindow == nullptr || pMonitor == nullptr)
        return false;

    if (pWindow == g_pInputManager->currentlyDraggedWindow.lock())
        return false;

    PHLWORKSPACE pWorkspace = pWindow->m_pWorkspace;
    PHTVIEW pView = getViewFromMonitor(pMonitor);
    if (pWorkspace == nullptr || pView == nullptr)
        return false;

    CBox winBox = pView->getGlobalWindowBox(pWindow);
    if (winBox.empty())
        return false;

    return !winBox.intersection(pMonitor->logicalBox()).empty();
}
