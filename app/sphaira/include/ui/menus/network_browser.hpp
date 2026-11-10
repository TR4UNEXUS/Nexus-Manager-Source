// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/menus/menu_base.hpp"
#include "nexus/network_browser_model.hpp"
#include <memory>
namespace sphaira::ui::menu::network {
class Browser final:public MenuBase {
    std::shared_ptr<nexus::network::browser::Model> model;
    ScrollingText title_text,path_text,selected_text,help_text;
    size_t last_selected{},last_revision{};
    void SetupActions();
public:
    explicit Browser(std::shared_ptr<nexus::network::browser::Model> state);
    const char* GetShortTitle()const override{return "Network browser";}
    void Update(Controller*,TouchInfo*) override;
    void Draw(NVGcontext*,Theme*) override;
};
}
