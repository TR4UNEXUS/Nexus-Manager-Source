// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace nexus::power {
// Foreground ownership outlives individual tasks. A completed transfer must
// not re-enable the idle timer while the user is still browsing Manager.
class AwakePolicy {
    bool foreground{}, stopped{};
public:
    void Focus(bool value) { foreground=value; }
    void Stop() { stopped=true; }
    bool Disabled() const { return foreground && !stopped; }
};
}
