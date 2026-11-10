// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace nexus::network::browser {
inline constexpr size_t PageSize=7;
inline constexpr size_t MaxSelection=64;
enum class Kind {Folder,File,Action};
struct SelectedFile {std::string path;std::uint64_t size{};};
struct Row {std::string name,detail;Kind kind{Kind::File};std::function<void()> activate;std::string selection_path{};std::uint64_t selection_size{};};
// UI-thread state only. Folder readers prepare rows in a worker and publish
// them in ProgressBox's completion callback, never from that worker.
struct Model {
    std::string title,location,help,empty;
    std::vector<Row> rows;
    std::function<void()> back,refresh;
    std::function<void(std::vector<SelectedFile>)> install_selected;
    std::vector<SelectedFile> marked;
    size_t selected{},revision{};
    std::map<std::string,size_t> history;
    void Replace(std::string next_title,std::string next_location,std::vector<Row> next_rows,
            std::string next_help,std::string next_empty,std::function<void()> next_back={},std::function<void()> next_refresh={}){
        if(!location.empty()){if(history.size()>=64&&!history.contains(location))history.erase(history.begin());history[location]=selected;}
        title=std::move(next_title);location=std::move(next_location);rows=std::move(next_rows);
        help=std::move(next_help);empty=std::move(next_empty);back=std::move(next_back);refresh=std::move(next_refresh);
        const auto previous=history.find(location);selected=rows.empty()?0:std::min(previous==history.end()?size_t{}:previous->second,rows.size()-1);++revision;
    }
    size_t PageStart()const{return selected/PageSize*PageSize;}
    void Move(bool down,size_t step=1){if(rows.empty()){selected=0;return;}selected=down?selected+std::min(step,rows.size()-1-selected):(selected>step?selected-step:0);}
    void Activate(){if(selected<rows.size()){auto callback=rows[selected].activate;if(callback)callback();}}
    bool IsMarked(const Row& row)const{return !row.selection_path.empty()&&std::any_of(marked.begin(),marked.end(),[&](const auto& file){return file.path==row.selection_path;});}
    void SetSelectionAction(std::function<void(std::vector<SelectedFile>)> action){install_selected=std::move(action);if(!install_selected)marked.clear();++revision;}
    bool Toggle(){
        if(!install_selected||selected>=rows.size()||rows[selected].selection_path.empty())return false;
        const auto& row=rows[selected];const auto found=std::find_if(marked.begin(),marked.end(),[&](const auto& file){return file.path==row.selection_path;});
        if(found!=marked.end())marked.erase(found);
        else {if(marked.size()>=MaxSelection)return false;marked.push_back({row.selection_path,row.selection_size});}
        ++revision;return true;
    }
    void InstallSelected(){auto callback=install_selected;auto files=marked;if(callback&&!files.empty())callback(std::move(files));}
    void RemoveSelected(const std::vector<std::string>& completed){
        std::erase_if(marked,[&](const auto& item){return std::find(completed.begin(),completed.end(),item.path)!=completed.end();});++revision;
    }
};
}
