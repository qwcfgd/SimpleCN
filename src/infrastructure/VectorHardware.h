#pragma once
#include "BusHardware.h"
#include "VectorRuntime.h"
namespace host {
std::unique_ptr<CanHardware> createVectorCan(std::shared_ptr<vectorxl::Runtime>);
std::unique_ptr<LinHardware> createVectorLin(std::shared_ptr<vectorxl::Runtime>);
}
