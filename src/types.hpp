#include <memory>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprutils/math/Box.hpp>

struct CHyprtaskingView;
struct CHyprtaskingManager;

typedef std::shared_ptr<CHyprtaskingView> PHTVIEW;
typedef std::weak_ptr<CHyprtaskingView> PHTVIEWREF;

typedef void (*tRenderWorkspace)(void *thisptr, PHLMONITOR pMonitor,
                                 PHLWORKSPACE pWorkspace, timespec *now,
                                 const CBox &geometry);

typedef bool (*tShouldRenderWindow)(void *thisptr, PHLWINDOW pWindow,
                                    PHLMONITOR pMonitor);

typedef void (*tRenderWindow)(void *thisptr, PHLWINDOW pWindow,
                              PHLMONITOR pMonitor, timespec *time,
                              bool decorate, eRenderPassMode mode,
                              bool ignorePosition, bool ignoreAllGeometry);
