// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/menus/main_menu.hpp"
#include "ui/menus/nexus_menu.hpp"
#include "app.hpp"

namespace sphaira::ui::menu::main {
auto GetMenuMenuEntries() -> std::span<const MiscMenuEntry> { return {}; }
MainMenu::MainMenu() {
    // One fixed workspace. Old 0.1 config entries cannot reopen Sphaira tools.
    m_menu=std::make_unique<nexusmenu::Menu>(MenuFlag_Tab);
    m_menu->SetAction(Button::START,Action{"Exit",App::Exit});
}
MainMenu::~MainMenu() = default;
void MainMenu::Update(Controller* c,TouchInfo* t){m_menu->Update(c,t);}
void MainMenu::Draw(NVGcontext* vg,Theme* theme){m_menu->Draw(vg,theme);}
void MainMenu::OnFocusGained(){Widget::OnFocusGained();m_menu->OnFocusGained();}
void MainMenu::OnFocusLost(){Widget::OnFocusLost();m_menu->OnFocusLost();}
}
