#pragma once

#include <hyprland/src/render/pass/PassElement.hpp>

class CGradientValueData;

class HTDisableSimplification: public IPassElement {
  public:
    HTDisableSimplification();
    virtual ~HTDisableSimplification() = default;

    virtual void draw(const CRegion& damage);
    virtual bool needsLiveBlur();
    virtual bool needsPrecomputeBlur();
    virtual bool disableSimplification();

    virtual const char* passName() {
        return "HTDisableSimplification";
    }
};
