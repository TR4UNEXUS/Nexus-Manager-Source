// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/menus/menu_base.hpp"
#include "ui/scrolling_text.hpp"
#include "nexus/manager.hpp"
#include "nexus/game_cleanup.hpp"
#include "nexus/mods.hpp"
#include "nexus/release.hpp"
#include <map>
#include <set>
#include <atomic>
namespace sphaira::ui::menu::nexusmenu {
struct Menu final : MenuBase {
    explicit Menu(u32 flags=MenuFlag_None);
    ~Menu();
    const char* GetShortTitle() const override {return "Nexus Manager";}
    void Update(Controller*,TouchInfo*) override;
    void Draw(NVGcontext*,Theme*) override;
private:
    enum class Page {Home,More,Installed,Files,Homebrew,Cleanup,Queue,Results,Mods,ModFolders};
    Page page=Page::Home;
    size_t selected{};
    bool usb_source{};
    u64 generation{};
    std::string path="/";
    std::vector<nexusapp::Item> files;
    std::vector<nexusapp::Game> games;
    std::set<u64> selected_games;
    void ToggleGame();
    void DeleteSelectedGames();
    void ChooseInstallMode(nexus::Target,const std::vector<nexus::batch::Entry>&);
    std::vector<nexusapp::CleanupItem> cleanup_items;
    std::string cleanup_detail;
    nexus::Target cleanup_target{nexus::Target::Sd};
    void ReviewRemoval(const nexusapp::CleanupItem&);
    void DeleteGameOptions(const nexusapp::Game&);
    void ScanCleanupTarget(nexus::Target);
    void ScanTickets();
    void DeleteAllCleanup();
    bool cleanup_tickets{};
    std::vector<nexus::mods::Candidate> mods;
    std::string mod_folder="/atmosphere";
    void ModActions();
    void ReviewMod(const nexus::mods::Candidate&,bool);
    void BrowseMods();
    void RecoverMods();
    nexus::batch::Selection packages;
    std::shared_ptr<nexus::batch::Report> batch_result;
    std::string batch_source;
    std::map<std::string,int> icons;
    std::set<std::string> icon_checked;
    std::set<u64> title_requested;
    bool refresh_pending{};
    bool actions_dirty{true};
    bool startup_update_started{},startup_update_handled{};
    ScrollingText row_text_scroll{},path_text_scroll{};
    bool title_service_ready{};
    struct IconJob {
        nexusapp::Game game;
        std::vector<u8> bytes;
        Result result{};
        std::atomic_bool done{};
        u64 started{};
        bool warned{};
    };
    std::unique_ptr<IconJob> icon_job;
    Thread icon_thread{};
    bool icon_thread_started{};
    static void ReadIconWorker(void*);
    void PollIcon();
    void QueueIcon(const nexusapp::Game&);
    void Activate();
    void Back();
    void Refresh();
    void ChooseSource(bool usb);
    void ChooseInstall(const nexusapp::Item&);
    void LaunchHomebrew(const nexusapp::Item&,u64 gen);
    void HomebrewActions(const nexusapp::Item&,u64 gen,bool usb=true);
    void CreateForwarder(const nexusapp::Item&,u64 gen);
    void RunProbe();
    void ChooseBatchDestination();
    void StartBatch(nexus::Target target,const std::vector<nexus::batch::Entry>& entries,bool reuse_existing_usb_only=false);
    void TogglePackage();
    void SelectAllPackages();
    void ReviewBatch();
    void SetupActions();
    void QueueDetails();
    void BatchResultDetails();
    void GameActions(bool primary_move=false);
    void ShowGameActions(const nexusapp::Game&);
    void MoveGameOptions(const nexusapp::Game&,bool move,bool primary=false);
    void BeginTransfer(const nexusapp::Game&,nexus::Target source,nexus::Target target,bool move);
    void MoreOptions();
    void ActivateMore();
    void PortTransferOptions(const nexusapp::Game&,bool move);
    void RefreshBadges();
    void CleanupTargets();
    void Updates();
    void ShowLanguagePicker();
    void CheckForUpdates();
    bool PollStartupUpdate();
    void OpenUsbRepair();
    void ShowUpdatePrompt(const nexus::updates::Release&);
    void RecoverUsbIndexes(u64 only_application=0);
    void ResumeInstalls();
    void ResumeMoves();
    void Report(Result,const std::string&);
    size_t Count() const;
};
}
