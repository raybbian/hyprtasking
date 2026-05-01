#pragma once

#include <hyprland/src/desktop/DesktopTypes.hpp>

PHLWORKSPACE create_workspace_for_monitor(PHLMONITOR monitor);
bool can_reuse_empty_workspace(PHLWORKSPACE workspace, PHLMONITOR monitor);
