// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/widget.hpp"
#include "ui/menus/menu_base.hpp"
#include <span>
namespace sphaira::ui::menu::main {
// Retained type for unlinked upstream settings helpers; no extra menus exported.
using MiscMenuFunction=std::function<std::unique_ptr<MenuBase>(u32)>;
enum MiscMenuFlag : u8 { MiscMenuFlag_Shortcut=1, MiscMenuFlag_Install=2 };
struct MiscMenuEntry {
    const char *name,*title;
    MiscMenuFunction func;
    u8 flag;
    const char* info;
    bool IsShortcut() const {return flag & MiscMenuFlag_Shortcut;}
    bool IsInstall() const {return flag & MiscMenuFlag_Install;}
};
auto GetMenuMenuEntries() -> std::span<const MiscMenuEntry>;
struct MainMenu final : Widget {
    MainMenu();
    ~MainMenu();
    void Update(Controller*,TouchInfo*) override;
    void Draw(NVGcontext*,Theme*) override;
    void OnFocusGained() override;
    void OnFocusLost() override;
    bool IsMenu() const override {return true;}
private:
    std::unique_ptr<MenuBase> m_menu;
};
}
