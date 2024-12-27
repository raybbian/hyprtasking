#include <cstdint>
#include <linux/input-event-codes.h>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/macros.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprland/src/managers/LayoutManager.hpp>
#include <hyprland/src/managers/PointerManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>

#include "overview.hpp"

void CHyprtaskingManager::onMouseButton(bool pressed, uint32_t button) {
    const PHLMONITOR pMonitor = g_pCompositor->getMonitorFromCursor();
    const PHTVIEW pView = getViewFromMonitor(pMonitor);
    if (pView == nullptr)
        return;

    const Vector2D mouseCoords = g_pInputManager->getMouseCoordsInternal();
    const WORKSPACEID workspaceID =
        pView->getWorkspaceIDFromVector(mouseCoords);
    PHLWORKSPACE pWorkspace = g_pCompositor->getWorkspaceByID(workspaceID);

    if (button == BTN_LEFT) {
        if (pressed) {
            Debug::log(LOG, "[Hyprtasking] Clicked at ({}, {})", mouseCoords.x,
                       mouseCoords.y);
            // If left click on non-workspace workspace, do nothing
            if (pWorkspace == nullptr)
                return;

            pMonitor->changeWorkspace(pWorkspace, true);
            pWorkspace->startAnim(true, false, true);
            pWorkspace->m_bVisible = true;

            const Vector2D mappedCoords =
                pView->mapGlobalPositionToWsGlobal(mouseCoords, workspaceID);

            g_pPointerManager->warpTo(mappedCoords);
            g_pKeybindManager->changeMouseBindMode(MBIND_MOVE);
            g_pPointerManager->warpTo(mouseCoords);

            // Fix snapping to mouse cursor
            const PHLWINDOW dragWindow =
                g_pInputManager->currentlyDraggedWindow.lock();
            if (dragWindow == nullptr)
                return;

            const Vector2D realCenter = pView->mapWsGlobalPositionToGlobal(
                dragWindow->m_vRealPosition.value() +
                    dragWindow->m_vRealSize.value() / 2.f,
                workspaceID);
            dragWindowOffset.setValueAndWarp(realCenter - mouseCoords);
            dragWindowOffset = {0, 0};

            Debug::log(LOG, "[Hyprtasking] Grabbed window at ({}, {}) on ws {}",
                       mappedCoords.x, mappedCoords.y,
                       pMonitor->activeWorkspace->m_iID);
        } else {
            const PHLWINDOW dragWindow =
                g_pInputManager->currentlyDraggedWindow.lock();
            if (dragWindow == nullptr) {
                g_pKeybindManager->changeMouseBindMode(MBIND_INVALID);
                return;
            }

            // Release on empty dummy workspace, so create and switch to it
            if (pWorkspace == nullptr && workspaceID != WORKSPACE_INVALID) {
                pWorkspace = g_pCompositor->createNewWorkspace(workspaceID,
                                                               pMonitor->ID);
                pMonitor->changeWorkspace(pWorkspace);

                Debug::log(LOG, "[Hyprtasking] Creating new workspace {}",
                           workspaceID);
            } else if (pWorkspace != nullptr) {
                pMonitor->changeWorkspace(pWorkspace, true);
                pWorkspace->startAnim(true, false, true);
                pWorkspace->m_bVisible = true;
            } else {
                // TODO: drop on invalid behavior?
                // FIXME: maybe spawn in middle of dummy workspace (to fix
                // monitor bug)
                pWorkspace = pMonitor->activeWorkspace;
            }

            g_pCompositor->moveWindowToWorkspaceSafe(dragWindow, pWorkspace);

            // Fix releasing animation
            const Vector2D mappedCoords = pView->mapGlobalPositionToWsGlobal(
                mouseCoords, pWorkspace->m_iID);
            dragWindow->m_vRealPosition.setValueAndWarp(
                mappedCoords - dragWindow->m_vRealSize.value() / 2.);

            // TODO: fix behavior that puts window on another monitor if
            // "closer"
            g_pPointerManager->warpTo(mappedCoords);
            g_pKeybindManager->changeMouseBindMode(MBIND_INVALID);
            g_pPointerManager->warpTo(mouseCoords);

            // otherwise the window leaves blur (?) artifacts on all
            // workspaces
            dragWindow->m_fMovingToWorkspaceAlpha.setValueAndWarp(1.0);

            Debug::log(
                LOG,
                "[Hyprtasking] Attempting to drop window at ({}, {}) on ws {}",
                mappedCoords.x, mappedCoords.y, pWorkspace->m_iID);
        }
    } else if (button == BTN_RIGHT) {
        if (pressed) {
            // If right click on dummy workspace, create and go here as well
            if (pWorkspace == nullptr && workspaceID != WORKSPACE_INVALID) {
                pWorkspace = g_pCompositor->createNewWorkspace(workspaceID,
                                                               pMonitor->ID);

                Debug::log(LOG, "[Hyprtasking] Creating new workspace {}",
                           workspaceID);
            }

            if (pWorkspace != nullptr) {
                pMonitor->changeWorkspace(pWorkspace, true);
                pWorkspace->startAnim(true, false, true);
                pWorkspace->m_bVisible = true;
                hide();
            }
        }
    }
}

void CHyprtaskingManager::onMouseMove() { dragWindowOffset.warp(); }
