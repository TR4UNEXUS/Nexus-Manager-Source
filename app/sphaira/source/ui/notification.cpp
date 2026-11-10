#include "ui/notification.hpp"
#include "ui/nvg_util.hpp"
#include "ui/nexus_dialog.hpp"
#include "defines.hpp"
#include "app.hpp"
#include <optional>

namespace sphaira::ui {
namespace {

constexpr u64 MAX_ENTRIES = 9;

} // namespace

NotifEntry::NotifEntry(const std::string& text, Side side)
: m_text{text}
, m_side{side} {
}

auto NotifEntry::Draw(NVGcontext* vg, Theme* theme, float y) -> bool {
    m_pos.y = y;
    Draw(vg, theme);
    m_count--;
    return m_count == 0;
}

auto NotifEntry::Draw(NVGcontext* vg, Theme* theme) -> void {
    float font_size = 18.f;

    if (!m_bounds_measured) {
        m_bounds_measured = true;
        m_pos.w = 320.f;
        m_pos.h = 60.f;

        float bounds[4];
        nvgFontSize(vg, font_size);
        nvgTextBounds(vg, 0, 0, this->m_text.c_str(), nullptr, bounds);
        // m_pos.w = std::max(bounds[2] - bounds[0] + 30.f, m_pos.w);
        m_pos.w = std::clamp(bounds[2]-bounds[0]+48.f,320.f,1000.f);
        nvgTextAlign(vg,NVG_ALIGN_LEFT|NVG_ALIGN_TOP);
        nvgTextLineHeight(vg,1.25f);
        nvgTextBoxBounds(vg,0,0,m_pos.w-48,m_text.c_str(),nullptr,bounds);
        m_pos.h=std::clamp(bounds[3]-bounds[1]+36.f,60.f,180.f);

        switch (m_side) {
            case Side::LEFT:
                m_pos.x = 4.f;
                break;
            case Side::RIGHT:
                m_pos.x = 1280.f - (m_pos.w + 4.f);// + 30.f);
                break;
        }
    }

    nexusdialog::Panel(vg,m_pos);
    nexusdialog::Body(vg,{m_pos.x+24,m_pos.y+18,m_pos.w-48,m_pos.h-32},m_text.c_str(),font_size);
}

auto NotifMananger::Draw(NVGcontext* vg, Theme* theme) -> void {
    mutexLock(&m_mutex);
    ON_SCOPE_EXIT(mutexUnlock(&m_mutex));

    Draw(vg, theme, m_entries_left);
    Draw(vg, theme, m_entries_right);
}

auto NotifMananger::Draw(NVGcontext* vg, Theme* theme, Entries& entries) -> void {
    // auto y = 130.f;
    auto y = 4.f;
    std::optional<Entries::iterator> delete_pos{std::nullopt};

    for (auto itr = entries.begin(); itr != entries.end(); ++itr) {
        itr->Draw(vg, theme, y);
        if (itr->IsDone() && !delete_pos.has_value()) {
            delete_pos = itr;
        }
        y += itr->GetH() + 15.f;
    }

    if (delete_pos.has_value()) {
        entries.erase(*delete_pos, entries.end());
    }
}

auto NotifMananger::Push(const NotifEntry& entry) -> void {
    mutexLock(&m_mutex);
    ON_SCOPE_EXIT(mutexUnlock(&m_mutex));

    auto& entries = GetEntries(entry.GetSide());
    entries.emplace_front(entry);

    // check if we have too many entries, start removing old ones.
    if (entries.size() >= MAX_ENTRIES) {
        entries.erase(entries.begin() + MAX_ENTRIES, entries.end());
    }
}

auto NotifMananger::Pop(NotifEntry::Side side) -> void {
    mutexLock(&m_mutex);
    ON_SCOPE_EXIT(mutexUnlock(&m_mutex));

    GetEntries(side).pop_back();
}

auto NotifMananger::Clear(NotifEntry::Side side) -> void {
    mutexLock(&m_mutex);
    ON_SCOPE_EXIT(mutexUnlock(&m_mutex));

    GetEntries(side).clear();
}

auto NotifMananger::Clear() -> void {
    mutexLock(&m_mutex);
    ON_SCOPE_EXIT(mutexUnlock(&m_mutex));

    m_entries_left.clear();
    m_entries_right.clear();
}

auto NotifMananger::GetEntries(NotifEntry::Side side) -> Entries& {
    if (side == NotifEntry::Side::LEFT) {
        return m_entries_left;
    } else {
        return m_entries_right;
    }
}

} // namespace sphaira::ui
