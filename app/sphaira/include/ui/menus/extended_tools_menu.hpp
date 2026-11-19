// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/menus/menu_base.hpp"
#include "nexus/transfer_activity.hpp"
namespace sphaira::ui::menu::extended {
enum class Mode {Cartridge,Transfer};
struct Menu final:MenuBase {
    explicit Menu(Mode);
    ~Menu();
    const char* GetShortTitle()const override{return "Nexus Manager";}
    void Update(Controller*,TouchInfo*)override;
    void Draw(NVGcontext*,Theme*)override;
private:
    Mode mode;size_t selected{};
    nexus::transfer::activity::Cursor activity_cursor;
    bool activity_visible{},activity_connected{};
    size_t Count()const;void Open();void Back();void Card(bool,bool);
};
}
