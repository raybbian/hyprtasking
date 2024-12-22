#pragma once

#include <hyprland/src/Compositor.hpp>

struct CHyprtaskingView {
  private:
    MONITORID monitorID;
    bool active;

  public:
    CHyprtaskingView(int64_t);
    ~CHyprtaskingView();

    PHLMONITOR getMonitor();

    void show();
    void hide();
    void render();

    bool isActive();
};

inline std::vector<std::shared_ptr<CHyprtaskingView>> g_overviews;
