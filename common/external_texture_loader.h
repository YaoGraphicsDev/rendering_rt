#pragma once

#include "otcv.h"
#include <string>
#include <cassert>

otcv::Image* load_dds_rgba16f(const std::string& filename);

otcv::Image* load_dummy_image();
