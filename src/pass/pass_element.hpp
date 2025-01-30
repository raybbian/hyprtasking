#pragma once

#include <hyprland/src/render/pass/PassElement.hpp>

class HTPassElement: public IPassElement {
  public:
    HTPassElement();
    virtual ~HTPassElement() = default;

    virtual void draw(const CRegion& damage);
    virtual bool needsLiveBlur();
    virtual bool needsPrecomputeBlur();
    virtual bool disableSimplification();

    virtual const char* passName() {
        return "HTDisableSimplification";
    }
};
