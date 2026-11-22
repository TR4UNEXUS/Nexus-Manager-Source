// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <switch.h>
#include <string>
namespace nexus::reports {
bool Configured();
int Preference(); // -1 undecided, 0 disabled, 1 automatic reports enabled
Result SetPreference(bool);
void QueueFailure(const char* kind,Result);
Result QueueManual(const std::string& description,std::string& preview);
Result ClearQueued();
std::string Status();
void Poll(bool idle);
void Stop();
void DisplayReportMenu();
bool OfferChoice();
}
