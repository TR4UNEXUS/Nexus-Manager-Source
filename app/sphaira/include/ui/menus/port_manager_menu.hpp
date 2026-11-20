// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/menus/menu_base.hpp"
#include "nexus/port_manager.hpp"
#include <optional>
#include <atomic>
#include <map>
#include <set>
namespace sphaira::ui::menu::portmanager {
struct Menu final:MenuBase {
    explicit Menu(std::optional<nexusports::Port> port={});
    ~Menu() override;
    void OnFocusLost() override;
    const char* GetShortTitle()const override{return "Nexus Manager";}
    void Update(Controller*,TouchInfo*)override;
    void Draw(NVGcontext*,Theme*)override;
private:
    bool usb{},pending{true},mods_page{},title_ready{};unsigned filter{};size_t selected{};
    std::optional<nexusports::Port> port;
    std::vector<nexusports::Port> ports;std::vector<nexusports::Mod> mods;
    std::string status;
    size_t Count()const;void Refresh();void Setup();void Back();void Open();void Storage();
    void Actions();void AddNro();void Folders();void Import();void ModActions();
    void RemoveFiles();void RemoveShortcut();void Shortcut();void DeleteMod();void MoveCopy();
    struct IconJob {nexusports::Port port;std::string key;std::vector<u8> bytes;Result result{};std::atomic_bool done{};};
    std::unique_ptr<IconJob> icon_job;Thread icon_thread{};bool icon_thread_started{};
    struct CachedIcon {int image{};u64 used{};bool title_checked{};};
    // 96 decoded 256x256 RGBA icons: at most 24 MiB plus texture overhead.
    static constexpr size_t IconCacheLimit=96;
    std::map<std::string,CachedIcon> icons;u64 icon_clock{};std::set<u64> title_requested;
    static void ReadIconWorker(void*);void StopIcon(bool keep=true);void PollIcons();void ClearIcons();
    void StoreIcon(const std::string&,int);void DropIcon(const std::string&);int CachedImage(const std::string&);
};
}
