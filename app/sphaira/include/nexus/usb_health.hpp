// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/usb_health_core.hpp"
#include "ui/progress_box.hpp"
namespace sphaira::nexusapp {
Result CheckUsbHealth(ui::ProgressBox* progress,nexus::health::Report& report);
}
