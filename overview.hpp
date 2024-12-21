#pragma once
#include <hyprland/src/Compositor.hpp>

struct CHyprtaskingView {
  private:
    MONITORID monitorID;

  public:
    CHyprtaskingView(int64_t);
    ~CHyprtaskingView();

    PHLMONITOR getMonitor();

    void show();
    void hide();
};

inline std::vector<std::shared_ptr<CHyprtaskingView>> g_overviews;
