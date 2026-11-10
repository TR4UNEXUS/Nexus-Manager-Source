// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <switch.h>
#include <string>

namespace nexus::updates {
// Main thread only, after installation and publication have been verified.
// Requests the edition's restart before exiting. Failure leaves the UI open.
Result RestartUpdatedManager(std::string& detail);
}
