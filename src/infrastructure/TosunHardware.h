#pragma once
#include "BusHardware.h"
#include "TosunRuntime.h"
namespace host {
std::unique_ptr<CanHardware> createTosunCan(std::shared_ptr<tosun::Runtime> runtime);
std::unique_ptr<LinHardware> createTosunLin(std::shared_ptr<tosun::Runtime> runtime);
}
