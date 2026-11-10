// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <switch.h>
namespace sphaira::nexusapp {
struct Game;
// Cosmetic errors are logged, never change install/move success or delete games.
Result UpdateHomeBadge(const Game&);
Result EnableHomeBadges(bool enable);
}
