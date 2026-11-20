// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/file_manager.hpp"
#include "nexus/port_manager_core.hpp"
#include "nexus/game_cleanup.hpp"
#include "nexus/port_transfer.hpp"
namespace sphaira::nexusports {
struct Port {std::string name,nro;bool usb{};u64 generation{},application{};std::vector<std::string> roots;std::string args;unsigned address{3};};
struct Mod {std::string name,path,root;bool active{},pending{};u64 bytes{};size_t files{};};
struct Removal {Port port;std::vector<nexusfiles::DeletePlan> folders;u64 bytes{};};
Result Scan(ui::ProgressBox*,bool usb,std::vector<Port>&,std::string&);
Result FromGame(ui::ProgressBox*,const nexusapp::Game&,Port&,std::string&);
Result RememberPort(ui::ProgressBox*,const Port&);
Result ReadIcon(const Port&,std::vector<u8>&);
Result PrepareTransfer(ui::ProgressBox*,const Port&,bool move,nexus::port::Plan&,std::string&);
Result Transfer(ui::ProgressBox*,const Port&,const nexus::port::Plan&,nexus::port::Report&,Port& destination);
Result PrepareShortcutRemoval(ui::ProgressBox*,const Port&,nexus::Target,nexusapp::GameRemovalPlan&);
Result LinkFolder(ui::ProgressBox*,Port&,const nexusfiles::Selection&,std::string&);
Result UnlinkFolder(ui::ProgressBox*,Port&,const std::string&,std::string&);
Result PrepareRemoval(ui::ProgressBox*,const Port&,Removal&,std::string&);
Result Remove(ui::ProgressBox*,const Removal&,std::string&);
Result CreateShortcut(ui::ProgressBox*,Port&);
Result ListMods(ui::ProgressBox*,const Port&,std::vector<Mod>&,std::string&);
Result ImportMod(ui::ProgressBox*,const Port&,const std::string& root,const nexusfiles::Selection&,Mod&,std::string&);
Result ReviewEnable(ui::ProgressBox*,const Port&,const Mod&,nexus::ports::ModPlan&,std::string&);
Result Enable(ui::ProgressBox*,const Port&,const nexus::ports::ModPlan&,std::string&);
Result Disable(ui::ProgressBox*,const Port&,const Mod&,std::string&);
}
