// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <switch.h>
#include <string>
namespace sphaira::ui {class ProgressBox;}
namespace nexus::diagnostics {
u64 Session();
class Operation {
    u64 previous{};const char* previous_name{};bool previous_failure{};
public:
    explicit Operation(const char* name);
    ~Operation();
    Operation(const Operation&)=delete;Operation& operator=(const Operation&)=delete;
};
Result Export(sphaira::ui::ProgressBox*,std::string& path);
}
