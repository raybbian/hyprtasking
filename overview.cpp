#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/SharedDefs.hpp>

#include "overview.hpp"

CHyprtaskingView::CHyprtaskingView(MONITORID inMonitorID) {
    monitorID = inMonitorID;
}

CHyprtaskingView::~CHyprtaskingView() {}

PHLMONITOR CHyprtaskingView::getMonitor() {
    return g_pCompositor->getMonitorFromID(monitorID);
}
