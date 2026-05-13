#include "pass_element.hpp"

HTPassElement::HTPassElement() {
    ;
}

std::vector<UP<IPassElement>> HTPassElement::draw(const CRegion& damage) {
    return {};
}

bool HTPassElement::needsLiveBlur() {
    return false;
}

bool HTPassElement::needsPrecomputeBlur() {
    // hyprexpo uses false
    return true;
}

bool HTPassElement::disableSimplification() {
    return true;
}
