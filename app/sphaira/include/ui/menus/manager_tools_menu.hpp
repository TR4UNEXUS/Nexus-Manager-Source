// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/menus/menu_base.hpp"
#include "nexus/manager_tools.hpp"
#include "nexus/file_manager.hpp"
#include "ui/file_manager_layout.hpp"
#include <optional>
namespace sphaira::ui::menu::managertools {
enum class Mode { Volumes, Files, Saves };
struct Menu final : MenuBase {
    explicit Menu(Mode,const std::string& folder="/");
    const char* GetShortTitle() const override {return "Nexus Manager";}
    void Update(Controller*,TouchInfo*) override;
    void Draw(NVGcontext*,Theme*) override;
private:
    Mode mode;size_t selected{};bool pending{},usb{};u64 generation{};
    std::string path="/";
    std::vector<nexusapp::Item> files;
    std::vector<nexusmanager::Save> saves;
    std::optional<nexusfiles::Selection> clipboard;
    bool cut{},actions_dirty{true};
    std::string focus_path;
    size_t Count() const;
    void Refresh();void Open();void Back();void Inspect(const nexusapp::Item&,bool);
    void SetupActions();void Storage();void FileActions();
    bool FileActionEnabled(fileview::FileAction) const;
    void RunFileAction(fileview::FileAction);
    void SelectClipboard(const nexusapp::Item&,bool move);void ClearClipboard();
    void Paste();void Remove(const nexusapp::Item&);
};
}
