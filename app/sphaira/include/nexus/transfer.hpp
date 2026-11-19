// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <switch.h>
#include <string>
#include <memory>
#include "nexus/ftp_stream.hpp"
#include "nexus/transfer_policy.hpp"
namespace sphaira::ui {struct ProgressBox;}
namespace nexus::transfer {
enum class Mode {Off,Cable,Ftp};
Result PrepareSession(sphaira::ui::ProgressBox*);
Result Start(Mode);
std::string StartupMessage();
struct InstallJob {bool usb{};u64 generation{},size{};std::string path;std::shared_ptr<FtpStream> stream;u64 upload_id{};};
Result RemoveInstalledUpload(const InstallJob&);
bool TakeInstall(InstallJob&);
void FinishInstall(Result);
void PollInstall();
void ShowNextInstall();
std::string SharedFolder();
void Stop();
void Poll();
Mode Active();
std::string Status();
}
