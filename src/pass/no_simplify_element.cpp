#include "no_simplify_element.hpp"

HTDisableSimplification::HTDisableSimplification() {
    ;
}

void HTDisableSimplification::draw(const CRegion& damage) {
    ;
}

bool HTDisableSimplification::needsLiveBlur() {
    return false;
}

bool HTDisableSimplification::needsPrecomputeBlur() {
    return false;
}

bool HTDisableSimplification::disableSimplification() {
    return true;
}
