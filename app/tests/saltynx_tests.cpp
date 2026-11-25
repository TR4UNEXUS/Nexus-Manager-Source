// SPDX-License-Identifier: GPL-3.0-or-later
#include "nexus/saltynx.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace nexus::saltynx;
namespace fs = std::filesystem;
void Write(const fs::path& file, std::string_view text) {
    std::ofstream(file, std::ios::binary) << text;
}
std::string Read(const fs::path& file) {
    std::ifstream stream(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), {}};
}
int main(int argc, char** argv) {
    assert(argc == 2);
    const fs::path root = argv[1];
    const auto sd = root.string();
    const auto salty = root/"SaltySD";
    const auto config = salty/"exceptions.txt";
    const auto log = salty/"saltynx.log";
    const auto sys = root/"atmosphere/contents/0000000000534C56";
    constexpr TitleId mario = 0x0100EA80032EA000ull, other = 0x010000000000A000ull;
    fs::create_directories(salty);
    Write(log,"old log");
    assert(!Inspect(sd).installed && AddException(mario,Exception::Skip,sd)==Save::Failed);
    fs::create_directories(sys);
    Write(sys/"exefs.nsp","module");
    assert(Inspect(sd).installed && Inspect(sd).settings_readable && !fs::exists(config));
    assert(Inspect(sd).Guidance(mario,"","")==Advice::MarioLaunch);
    assert(Inspect(sd).Guidance(other,"","")==Advice::Unknown);
    assert(Inspect(sd).Guidance(other,"Pikmin 3 Deluxe","")==Advice::PluginsOnly);
    assert(Inspect(sd).Guidance(other,"The Witcher 3: Wild Hunt - Complete Edition","3.2")==Advice::VersionIssue);
    assert(Inspect(sd).Guidance(other,"The Witcher 3: Wild Hunt - Complete Edition","4.0")==Advice::Unknown);
    const std::string data=";X0100ea80032ea000\r\nR0100EA80032EA000\r\n0100EA80032EA000";
    Write(config,data);
    assert(Inspect(sd).Setting(mario)==Exception::PluginsAndRomfs);
    assert(AddException(mario,Exception::Plugins,sd)==Save::AlreadySet);
    assert(AddException(mario,Exception::Skip,sd)==Save::Added);
    assert(Read(config)==data+"\r\nX0100EA80032EA000\r\n");
    assert(AddException(mario,Exception::Skip,sd)==Save::AlreadySet);
    assert(Inspect(sd).Setting(mario)==Exception::Skip);
    Write(config,"x0100ea80032ea000\n");
    assert(AddException(mario,Exception::Skip,sd)==Save::AlreadySet);
    const std::string invalid("hello\0world",11);
    Write(config,invalid);
    assert(!Inspect(sd).settings_readable && AddException(other,Exception::Skip,sd)==Save::Failed);
    assert(Read(config)==invalid);
    fs::remove(config);
    assert(AddException(other,Exception::Plugins,sd)==Save::Added);
    assert(Read(config)=="010000000000A000\n");
    const std::string launch="SaltyNX: found valid CreateProcess event:\n[15] tid 010000000000a000 pid 137\n";
    const std::string load="[serviceLoadELF] loading sdmc:/SaltySD/saltynx_core32.elf, size 0x1b0d8\n";
    const std::string fail="[serviceLoadELF] Load_elf arm32: 1, ret: 0xd401\n";
    Write(log, load+fail+launch+"SaltyNX: PID 137 svcGetDebugevent: end of events...\n"+load+fail);
    assert(Inspect(sd).Guidance(other,"","")==Advice::CoreError);
    Write(log, launch+load+fail+launch);
    assert(Inspect(sd).Guidance(other,"","")==Advice::Unknown);
    Write(log, launch+load+"SaltyNX 2.1.4: got SD card.\n"+fail);
    assert(Inspect(sd).Guidance(other,"","")==Advice::Unknown);
    Write(log,launch+load+"[serviceLoadELF] loading other.elf, size 0x20\n"+fail);
    assert(Inspect(sd).Guidance(other,"","")==Advice::Unknown);
    Write(log,launch+"[serviceLoadELF] loading sdmc:/SaltySD/saltynx_core.elf, size 0x100\n"
        "[serviceLoadELF] Load_elf arm32: 0, ret: 0x123\n");
    assert(Inspect(sd).Guidance(other,"","")==Advice::CoreError);
    std::cout<<"SaltyNX settings, notice scope, log correlation and title-specific exceptions passed\n";
}

