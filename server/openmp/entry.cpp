#include "component.hpp"

COMPONENT_ENTRY_POINT() {
    return new custommodel::server::openmp::CustomModelComponent();
}
