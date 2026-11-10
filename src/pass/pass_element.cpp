#include "pass_element.hpp"

HTPassElement::HTPassElement() {
    ;
}

std::vector<UP<IPassElement>> HTPassElement::draw(Render::CRenderContext& ctx) {
    return {};
}

bool HTPassElement::needsLiveBlur(Render::CRenderContext& ctx) {
    return false;
}

bool HTPassElement::needsPrecomputeBlur(Render::CRenderContext& ctx) {
    // hyprexpo uses false
    return true;
}

bool HTPassElement::disableSimplification(Render::CRenderContext& ctx) {
    return true;
}
