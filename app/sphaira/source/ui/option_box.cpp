#include "ui/option_box.hpp"
#include "ui/nvg_util.hpp"
#include "ui/nexus_dialog.hpp"
#include "app.hpp"

namespace sphaira::ui {
namespace {

constexpr float BOX_WIDTH = 1000.f;
constexpr float BOX_HEIGHT = 568.f;
constexpr float BUTTON_HEIGHT = 90.f;
constexpr float CONTENT_HEIGHT = BOX_HEIGHT - BUTTON_HEIGHT;

} // namespace

OptionBoxEntry::OptionBoxEntry(const std::string& text, const Vec4& pos)
: m_text{text} {
    m_pos = pos;
    m_text_pos = Vec2{m_pos.x + (m_pos.w / 2.f), m_pos.y + (m_pos.h / 2.f)};
}

auto OptionBoxEntry::Draw(NVGcontext* vg, Theme* theme) -> void {
    const auto label=m_selected?std::string(gfx::getButton(Button::A))+"  "+m_text:m_text;
    nexusdialog::Button(vg,{m_pos.x+16,m_pos.y+12,m_pos.w-32,m_pos.h-30},label.c_str(),m_selected);
}

auto OptionBoxEntry::Selected(bool enable) -> void {
    m_selected = enable;
}

OptionBox::OptionBox(const std::string& message, const Option& a, const Callback& cb, int image, bool own_image)
: m_message{message}
, m_callback{cb}
, m_image{image}
, m_own_image{own_image} {

    m_pos.w = BOX_WIDTH;
    m_pos.h = BOX_HEIGHT;
    m_pos.x = (SCREEN_WIDTH / 2.f) - (m_pos.w / 2.f);
    m_pos.y = (SCREEN_HEIGHT / 2.f) - (m_pos.h / 2.f);

    auto box = m_pos;
    box.y += CONTENT_HEIGHT;
    box.h = BUTTON_HEIGHT;
    m_entries.emplace_back(a, box);

    Setup(0);
}

OptionBox::OptionBox(const std::string& message, const Option& a, const Option& b, const Callback& cb, int image, bool own_image)
: OptionBox{message, a, b, 0, cb, image, own_image} {

}

OptionBox::OptionBox(const std::string& message, const Option& a, const Option& b, s64 index, const Callback& cb, int image, bool own_image)
: m_message{message}
, m_callback{cb}
, m_image{image}
, m_own_image{own_image} {

    m_pos.w = BOX_WIDTH;
    m_pos.h = BOX_HEIGHT;
    m_pos.x = (SCREEN_WIDTH / 2.f) - (m_pos.w / 2.f);
    m_pos.y = (SCREEN_HEIGHT / 2.f) - (m_pos.h / 2.f);

    auto box = m_pos;
    box.w /= 2.f;
    box.y += CONTENT_HEIGHT;
    box.h = BUTTON_HEIGHT;
    m_entries.emplace_back(a, box);
    box.x += box.w;
    m_entries.emplace_back(b, box);

    Setup(index);
}

OptionBox::~OptionBox() {
    if (m_image && m_own_image) {
        nvgDeleteImage(App::GetVg(), m_image);
    }
}

auto OptionBox::Update(Controller* controller, TouchInfo* touch) -> void {
    Widget::Update(controller, touch);

    if (touch->is_clicked) {
        for (s64 i = 0; i < m_entries.size(); i++) {
            auto& e = m_entries[i];
            if (touch->in_range(e.GetPos())) {
                SetIndex(i);
                FireAction(Button::A);
                break;
            }
        }
    }
}

auto OptionBox::Draw(NVGcontext* vg, Theme* theme) -> void {
    gfx::dimBackground(vg);
    nexusdialog::Panel(vg,m_pos);
    nexusdialog::Brand(vg,m_pos.x+32,m_pos.y+26,"/  CONFIRMATION");

    const Vec4 content{m_pos.x+32,m_pos.y+72,m_pos.w-64,CONTENT_HEIGHT-96};
    if (m_image) {
        const Vec4 image{content.x,content.y,150,150};
        gfx::drawImage(vg,image,m_image,5);
        m_message_scroll.Draw(vg,{content.x+180,content.y,content.w-180,content.h},m_message.c_str());
    } else {
        const auto first_break=m_message.find_first_of("\r\n");
        const auto heading=first_break==std::string::npos?std::string{}:m_message.substr(0,first_break);
        const float heading_height=nexusdialog::BodyHeight(vg,content.w,heading.c_str(),26);
        auto body_start=first_break;
        if(body_start!=std::string::npos)while(body_start<m_message.size()&&(m_message[body_start]=='\r'||m_message[body_start]=='\n'))++body_start;
        // Very long first lines are part of the scrollable report. They must
        // not consume the body viewport or create a negative clipping height.
        if(!heading.empty()&&heading_height<=96&&body_start<m_message.size()){
            nexusdialog::Body(vg,{content.x,content.y,content.w,heading_height+4},heading.c_str(),26);
            const float body_y=content.y+heading_height+18;
            m_message_scroll.Draw(vg,{content.x,body_y,content.w,content.y+content.h-body_y},m_message.c_str()+body_start);
        } else {
            m_message_scroll.Draw(vg,content,m_message.c_str(),first_break==std::string::npos?26:22);
        }
    }

    gfx::drawRect(vg,m_pos.x+30,m_spacer_line.y,m_pos.w-60,1,nvgRGB(72,48,94));

    for (auto&p: m_entries) {
        p.Draw(vg, theme);
    }
}

auto OptionBox::OnFocusGained() noexcept -> void {
    Widget::OnFocusGained();
    SetHidden(false);
}

auto OptionBox::OnFocusLost() noexcept -> void {
    Widget::OnFocusLost();
    SetHidden(true);
}

auto OptionBox::Setup(s64 index) -> void {
    m_index = std::min<s64>(m_entries.size() - 1, index);
    m_entries[m_index].Selected(true);
    m_spacer_line = Vec4{m_pos.x, m_pos.y + CONTENT_HEIGHT - 2.f, m_pos.w, 2.f};

    SetAction(Button::UP,Action{[this]{m_message_scroll.ScrollBy(-66);}});
    SetAction(Button::DOWN,Action{[this]{m_message_scroll.ScrollBy(66);}});
    SetActions(
        std::make_pair(Button::LEFT, Action{[this](){
            if (m_index) {
                SetIndex(m_index - 1);
            }
        }}),
        std::make_pair(Button::RIGHT, Action{[this](){
            if (m_index < (m_entries.size() - 1)) {
                SetIndex(m_index + 1);
            }
        }}),
        std::make_pair(Button::A, Action{[this](){
            m_callback(m_index);
            SetPop();
        }}),
        std::make_pair(Button::B, Action{[this](){
            m_callback({});
            SetPop();
        }})
    );
}

void OptionBox::SetIndex(s64 index) {
    if (m_index != index) {
        m_entries[m_index].Selected(false);
        m_index = index;
        m_entries[m_index].Selected(true);
    }
}

} // namespace sphaira::ui
