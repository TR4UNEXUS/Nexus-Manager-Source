// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nexus/port_transfer_core.hpp"
namespace nexus::port {
// Physical selections do not need a HOME title. Keep isolated USB SD views
// mapped back to their logical SD paths, including separately linked data.
inline bool FilesPlan(bool usb,std::string_view nro,const std::vector<std::string>& roots,
                      std::string destination_root,Plan& plan){
    plan.source_usb=usb;plan.application=0;plan.create_launcher=false;
    plan.nro=std::string(nro);plan.roots.clear();plan.source_root.clear();plan.destination_root.clear();
    if(!Safe(nro)||!nexus_vsd::Nro(nro)||roots.empty()||roots.size()>16)return false;
    if(usb){nexus_vsd::Profile profile{};
        if(nexus_vsd::Parse(nro,profile)&&!nexus_vsd::UsbRoot(profile)){
            plan.source_root=profile.root;plan.nro=profile.logical;
        }
    }else plan.destination_root=std::move(destination_root);
    for(auto root:roots){
        if(!Safe(root))return false;
        if(!plan.source_root.empty()){
            if(!Within(root,plan.source_root)||root==plan.source_root)return false;
            root.erase(0,plan.source_root.size());
        }
        if(!Safe(root)||!Safe(plan.Destination(root)))return false;
        if(std::none_of(plan.roots.begin(),plan.roots.end(),[&](const auto& old){return Within(Fold(root),Fold(old));})){
            std::erase_if(plan.roots,[&](const auto& old){return Within(Fold(old),Fold(root));});plan.roots.push_back(root);
        }
    }
    return std::any_of(plan.roots.begin(),plan.roots.end(),[&](const auto& root){return Within(Fold(plan.nro),Fold(root));});
}
}
