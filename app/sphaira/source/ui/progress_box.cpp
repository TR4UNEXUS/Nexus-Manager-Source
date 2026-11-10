#include "ui/progress_box.hpp"
#include "ui/option_box.hpp"
#include "ui/nvg_util.hpp"
#include "ui/nexus_dialog.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "log.hpp"
#include "threaded_file_transfer.hpp"
#include "i18n.hpp"
#include "nexus/watchdog.h"
#include "nexus/progress_layout.hpp"
#include "nexus/usb_library_startup.hpp"

#include "utils/utils.hpp"
#include "utils/thread.hpp"

#include <cstring>
#include <cmath>

namespace sphaira::ui {
namespace {

void threadFunc(void* arg) {
    auto d = static_cast<ProgressBox::ThreadData*>(arg);
    d->result = d->callback(d->pbox);
    d->pbox->WorkerFinished();
}

using ProgressRect=nexus::progressui::Rect;

void DrawProgressLine(NVGcontext* vg,ScrollingText& scroll,ProgressRect row,
                      const std::string& text,float font,bool centered,const NVGcolor& colour){
    if(row.w<=0||row.h<=0||text.empty())return;
    auto line=text;
    for(auto& c:line)if(c=='\n'||c=='\r'||c=='\t')c=' ';
    nvgSave(vg);nvgIntersectScissor(vg,row.x,row.y,row.w,row.h);
    nvgFontSize(vg,font);nvgTextAlign(vg,NVG_ALIGN_LEFT|NVG_ALIGN_TOP);
    float bounds[4]{};nvgTextBounds(vg,0,0,line.c_str(),nullptr,bounds);
    const auto width=bounds[2]-bounds[0];
    const auto y=row.y+std::max(0.f,(row.h-font*1.2f)/2);
    if(width<=row.w){
        scroll.Reset(line);
        gfx::drawText(vg,nexus::progressui::TextX(row,width,centered)-bounds[0],y,font,colour,
            line.c_str(),NVG_ALIGN_LEFT|NVG_ALIGN_TOP);
    }else{
        // Always scroll from the left edge of this row, never from a centered
        // starting point whose clip could extend beyond the text column.
        scroll.Draw(vg,true,row.x,y,row.w,font,NVG_ALIGN_LEFT|NVG_ALIGN_TOP,colour,line);
    }
    nvgRestore(vg);
}

void DrawProgressBody(NVGcontext* vg,ProgressRect row,const std::string& text,
                      float desired,bool centered,const NVGcolor& colour){
    if(row.w<=0||row.h<=0||text.empty())return;
    nvgSave(vg);nvgIntersectScissor(vg,row.x,row.y,row.w,row.h);
    const auto align=(centered?NVG_ALIGN_CENTER:NVG_ALIGN_LEFT)|NVG_ALIGN_TOP;
    nvgTextAlign(vg,align);nvgTextLineHeight(vg,1.2f);
    auto visible=text;float font=std::max(14.f,desired),bounds[4]{};
    const auto measure=[&]{nvgFontSize(vg,font);nvgTextBoxBounds(vg,0,0,row.w,visible.c_str(),nullptr,bounds);return bounds[3]-bounds[1];};
    while(measure()>row.h&&font>14)--font;
    if(measure()>row.h){
        // Progress notes are short status text, not reports. Preserve complete
        // UTF-8 code points and mark any exceptional overflow visibly.
        NVGtextRow lines[8]{};
        const int count=nvgTextBreakLines(vg,text.c_str(),nullptr,row.w,lines,8);
        const int allowed=std::max(1,std::min(count,int(row.h/(font*1.2f))));
        if(count>0)visible.assign(text.c_str(),lines[allowed-1].end);
        while(!visible.empty()){
            visible+="...";
            if(measure()<=row.h)break;
            visible.resize(visible.size()-3);
            auto end=visible.size()-1;
            while(end>0&&(static_cast<unsigned char>(visible[end])&0xc0)==0x80)--end;
            visible.resize(end);
        }
    }
    const auto height=measure();
    gfx::drawTextBox(vg,row.x,row.y+std::max(0.f,(row.h-height)/2),font,row.w,colour,visible.c_str(),align);
    nvgRestore(vg);
}

} // namespace

ProgressBox::ProgressBox(int image, const std::string& action, const std::string& title, const ProgressBoxCallback& callback, const ProgressBoxDoneCallback& done, ProgressBoxOption option, nexus::safety::Operation operation)
: m_operation{operation}
, m_done{done}
, m_action{action}
, m_title{title}
, m_image{image} {
    // A foreground task owns storage/services. Never let a boot-time library
    // snapshot race an installer, repair, transfer or another foreground scan.
    nexusapp::StopUsbLibraryStartupCheck();
    App::SetAutoSleepDisabled(true);
    if (App::GetApp()->m_progress_boost_mode.Get()) {
        App::SetBoostMode(true);
    }

    SetAction(Button::B, Action{[this](){
        if(ShouldExit())return;
        App::Push<OptionBox>("Stop this task?\n\nThe current storage request must finish first. Keep the SD card and USB drive connected until the result appears.", "Keep going", "Stop task", 0, [this](auto op_index){
            if (op_index && *op_index) {
                RequestExit();
            }
        });
    }});

    SetAction(Button::X, Action{[this](){
        if (!(ShouldExit() || m_safety.Slow(armGetSystemTick(), armNsToTicks(15ULL*1000*1000*1000))) ||
            !m_safety.CanClose(m_operation, nexusWatchdogCanExit())) return;
        App::Push<OptionBox>("This check is taking longer than expected.\n\nIt is not changing files. You can keep waiting or close Nexus Manager. Keep the USB drive connected.", "Keep waiting", "Close app", 0, [this](auto choice){
            if (choice && *choice == 1 && m_safety.CanClose(m_operation, nexusWatchdogCanExit()))
                nexusWatchdogTryExit();
        });
    }});

    if (option == ProgressBoxOption::ScreenToggle && App::AcquireScreenToggle()) {
        m_screen_toggle_enabled = true;
        SetAction(Button::Y, Action{[](){
            App::SetScreenDisabled(true);
        }});
    }

    m_pos.w = 920.f;
    m_pos.h = 465.f;
    m_pos.x = (SCREEN_WIDTH / 2.f) - (m_pos.w / 2.f);
    m_pos.y = (SCREEN_HEIGHT / 2.f) - (m_pos.h / 2.f);

    // create cancel event.
    ueventCreate(&m_uevent, false);

    m_thread_data.pbox = this;
    m_thread_data.callback = callback;
    m_safety.Touch(armGetSystemTick());
    m_protected = m_operation == nexus::safety::Operation::MayWrite;
    if (m_protected) nexusWatchdogProtect();
    m_thread_data.result=utils::CreateThread(&m_thread,threadFunc,&m_thread_data);
    if(R_SUCCEEDED(m_thread_data.result)){
        const auto start_result=threadStart(&m_thread);
        if(R_SUCCEEDED(start_result))m_thread_started=true;
        else {m_thread_data.result=start_result;threadClose(&m_thread);}
    }
    if(!m_thread_started)WorkerFinished();
}

void ProgressBox::WorkerFinished() {
    EndNetworkTransfer();
    if (m_protected) {
        nexusWatchdogUnprotect();
        m_protected = false;
    }
    // Release publication: result and all callback-owned buffers are now safe.
    m_safety.Finish();
}

ProgressBox::~ProgressBox() {
    ueventSignal(GetCancelEvent());
    m_stop_source.request_stop();

    if (m_thread_started && R_FAILED(threadWaitForExit(&m_thread))) {
        log_write("failed to join thread\n");
    }
    if (m_thread_started && R_FAILED(threadClose(&m_thread))) {
        log_write("failed to close thread\n");
    }

    FreeImage();
    if (m_screen_toggle_enabled) {
        App::ReleaseScreenToggle();
    }

    if (m_done && !App::IsExiting()) {
        m_done(m_thread_data.result);
    }

    App::SetBoostMode(false);
    App::SetAutoSleepDisabled(false);
}

auto ProgressBox::Update(Controller* controller, TouchInfo* touch) -> void {
    Widget::Update(controller, touch);

    if (HasFocus() && touch->is_clicked) {
        for (const auto& button : GetFooterButtons()) {
            if (touch->in_range(button.GetPos())) {
                FireAction(button.m_button);
                break;
            }
        }
    }

    if (m_safety.CanDismiss()) {
        SetPop();
    }
}

auto ProgressBox::Draw(NVGcontext* vg, Theme* theme) -> void {
    mutexLock(&m_mutex);
    std::vector<u8> image_data{};
    std::swap(m_image_data, image_data);
    if (m_timestamp.GetSeconds()) {
        m_timestamp.Update();
        m_speed = m_offset - m_last_offset;
        m_last_offset = m_offset;
    }

    const auto action = m_action;
    const auto title = m_title;
    const auto transfer = m_transfer;
    const auto size = m_size;
    const auto offset = m_offset;
    const auto speed = m_speed;
    const auto last_offset = m_last_offset;
    const auto overall = m_overall;
    const auto finishing = m_finishing;
    const auto progress_note = m_progress_note;
    const auto byte_detail = m_byte_detail;
    const auto network = m_network_rate.Sample(armTicksToNs(armGetSystemTick()));
    auto image = m_image;

    if (m_is_image_pending) {
        FreeImage();
        image = m_image = m_image_pending;
        m_image_pending = 0;
        m_is_image_pending = false;
    }
    mutexUnlock(&m_mutex);

    if (!image_data.empty()) {
        FreeImage();
        image = m_image = nvgCreateImageMem(vg, 0, image_data.data(), image_data.size());
        m_own_image = true;
    }

    gfx::dimBackground(vg);
    nexusdialog::Panel(vg,m_pos);
    nexusdialog::Brand(vg,GetX()+32,GetY()+22,"/  TRANSFER");

    const auto layout=nexus::progressui::Make({GetX(),GetY(),GetW(),GetH()},image!=0,network.active);
    const auto colour=theme->GetColour(ThemeEntryID_TEXT);
    const Vec4 prog_bar{layout.bar.x,layout.bar.y,layout.bar.w,layout.bar.h};
    const auto end_y=layout.footer_y;
    const bool has_progress=size>0&&(offset>0||overall);

    nvgSave(vg);
    nvgIntersectScissor(vg,GetX(),GetY(),GetW(),GetH());
    if(image)gfx::drawImage(vg,layout.cover.x,layout.cover.y,layout.cover.w,layout.cover.h,image,12);

    DrawProgressLine(vg,m_scroll_action,layout.action,action,26,layout.centered,colour);
    DrawProgressLine(vg,m_scroll_title,layout.title,title,22,layout.centered,colour);
    DrawProgressLine(vg,m_scroll_transfer,layout.stage,transfer,18,layout.centered,colour);

    const bool stopping=ShouldExit();
    const bool slow=m_safety.Slow(armGetSystemTick(),armNsToTicks(15ULL*1000*1000*1000));
    if(stopping||slow){
        const auto message=stopping
            ? "Stopping safely. Waiting for the current storage request to finish."
            : "The drive is taking longer than usual. Please keep it connected.";
        const auto advice=m_safety.CanClose(m_operation,nexusWatchdogCanExit())
            ? "This is a read-only check. X: Close app, or keep waiting."
            : "Please wait for the result before unplugging or closing the app.";
        DrawProgressBody(vg,layout.status,message,18,layout.centered,colour);
        DrawProgressBody(vg,layout.advice,advice,17,layout.centered,colour);
    }else if(!byte_detail.empty()){
        DrawProgressBody(vg,layout.status,byte_detail,22,layout.centered,colour);
    }else if(overall){
        DrawProgressBody(vg,layout.status,"Copy, check, finish. Keep both storage devices connected.",18,layout.centered,colour);
    }

    if(has_progress){
        gfx::drawRect(vg,prog_bar,theme->GetColour(ThemeEntryID_PROGRESSBAR_BACKGROUND),5);
        const auto fraction=std::clamp(double(offset)/double(size),0.0,1.0);
        const u32 percentage=fraction*100.0;
        gfx::drawRect(vg,prog_bar.x,prog_bar.y,fraction*prog_bar.w,prog_bar.h,nexusdialog::Accent(),5);
        char percent[16]{};std::snprintf(percent,sizeof(percent),"%u%%",percentage);
        DrawProgressBody(vg,layout.percent,percent,18,false,colour);
        gfx::drawSpinner(vg,theme,layout.spinner_x,layout.spinner_y,13,armTicksToNs(armGetSystemTick())/1e+9);
    }

    std::string metric;
    if(network.active){
        char text[160]{};
        if(network.sampled)
            std::snprintf(text,sizeof(text),"NAS download: %.2f MiB/s  |  Received: %.1f MiB",network.bytes_per_second/(1024.0*1024.0),double(network.received)/(1024.0*1024.0));
        else
            std::snprintf(text,sizeof(text),"NAS download: measuring...  |  Received: %.1f MiB",double(network.received)/(1024.0*1024.0));
        DrawProgressBody(vg,layout.metric,text,20,layout.centered,colour);
        DrawProgressBody(vg,layout.note,progress_note,16,layout.centered,colour);
    }else{
        if(!progress_note.empty())metric=progress_note;
        else if(has_progress){
            if(finishing||speed<=0)metric=finishing?"Almost done - keep storage connected":"Calculating time remaining...";
            else{
                const auto left=std::max<s64>(0,size-last_offset);
                const auto left_seconds=left/speed;
                const auto hours=left_seconds/(60*60),minutes=left_seconds%(60*60)/60,seconds=left_seconds%60;
                char time_str[96]{};
                if(hours)std::snprintf(time_str,sizeof(time_str),"%zu hours %zu minutes remaining"_i18n.c_str(),hours,minutes);
                else if(minutes)std::snprintf(time_str,sizeof(time_str),"%zu minutes %zu seconds remaining"_i18n.c_str(),minutes,seconds);
                else std::snprintf(time_str,sizeof(time_str),"%zu seconds remaining"_i18n.c_str(),seconds);
                metric=std::string(time_str)+" ("+utils::formatSizeNetwork(speed)+"/s)";
            }
        }
        DrawProgressBody(vg,layout.metric,metric,18,layout.centered,colour);
    }

    nvgRestore(vg);

    {
        gfx::drawRect(vg, m_pos.x, end_y, m_pos.w, 2.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
        for (auto& button : GetFooterButtons()) {
            button.Draw(vg, theme);
        }
    }
}

auto ProgressBox::GetFooterButtons() const -> Widget::uiButtons {
    Widget::uiButtons buttons;
    const auto y = m_pos.y + m_pos.h - 40.f;

    const auto add_button = [&](Button button, const std::string& hint, float center_x) {
        Widget::uiButtons entry{uiButton{button, hint}};
        Widget::SetupUiButtons(entry, {center_x, y});

        auto& item = entry.front();
        const auto visual_left = item.m_button_pos.x - item.m_button_pos.w;
        const auto visual_right = item.m_hint_pos.x;
        const auto offset = center_x - (visual_left + visual_right) / 2.f;

        item.m_button_pos.x += offset;
        item.m_hint_pos.x += offset;
        item.SetX(item.GetX() + offset);
        buttons.emplace_back(std::move(item));
    };

    if(m_screen_toggle_enabled)add_button(Button::Y, "Screen off", m_pos.x + m_pos.w * 0.25f);
    add_button(Button::B, m_stop_source.stop_requested()?"Stopping...":"Stop task", m_pos.x + m_pos.w * (m_screen_toggle_enabled?0.75f:0.5f));
    return buttons;
}

auto ProgressBox::SetActionName(const std::string& action)  -> ProgressBox& {
    SCOPED_MUTEX(&m_mutex);
    m_action = action;
    m_safety.Touch(armGetSystemTick());
    return *this;
}

auto ProgressBox::SetTitle(const std::string& title)  -> ProgressBox& {
    SCOPED_MUTEX(&m_mutex);
    if(m_detailed)return *this;
    m_title = title;
    m_safety.Touch(armGetSystemTick());
    return *this;
}

auto ProgressBox::NewTransfer(const std::string& transfer)  -> ProgressBox& {
    SCOPED_MUTEX(&m_mutex);
    if(m_detailed)return *this;
    m_transfer = transfer;
    m_safety.Touch(armGetSystemTick());
    m_size = 0;
    m_offset = 0;
    m_last_offset = 0;
    m_timestamp.Update();
    return *this;
}

auto ProgressBox::ResetTranfser() -> ProgressBox& {
    SCOPED_MUTEX(&m_mutex);
    if(m_detailed)return *this;
    m_size = 0;
    m_offset = 0;
    m_last_offset = 0;
    m_timestamp.Update();
    return *this;
}

auto ProgressBox::UpdateTransfer(s64 offset, s64 size)  -> ProgressBox& {
    SCOPED_MUTEX(&m_mutex);
    if(m_detailed)return *this;
    if (m_size != size || m_offset != offset) m_safety.Touch(armGetSystemTick());
    m_size = size;
    m_offset = offset;
    return *this;
}

auto ProgressBox::SetProgressNote(const std::string& note) -> ProgressBox& {
    SCOPED_MUTEX(&m_mutex);
    if(m_detailed)return *this;
    m_progress_note=note;
    return *this;
}

void ProgressBox::BeginNetworkTransfer() {
    SCOPED_MUTEX(&m_mutex);
    m_network_rate.Begin(armTicksToNs(armGetSystemTick()));
}

void ProgressBox::AddNetworkBytes(u64 bytes) {
    if(!bytes)return;
    SCOPED_MUTEX(&m_mutex);
    m_network_rate.Add(bytes);
    m_safety.Touch(armGetSystemTick());
}

void ProgressBox::EndNetworkTransfer() {
    SCOPED_MUTEX(&m_mutex);
    m_network_rate.End();
}

auto ProgressBox::SetDetailedProgress(unsigned percent,const std::string& stage,const std::string& bytes,const std::string& note) -> ProgressBox& {
    SCOPED_MUTEX(&m_mutex);
    if(percent>100 || (m_detailed && percent<m_offset))return *this;
    if(!m_detailed){m_last_offset=0;m_speed=0;m_timestamp.Update();}
    m_detailed=true;m_overall=true;m_size=100;m_offset=percent;
    m_transfer=stage;m_byte_detail=bytes;m_progress_note=note;
    m_safety.Touch(armGetSystemTick());
    return *this;
}

auto ProgressBox::SetOverallTransfer(s64 offset,s64 size,const std::string& stage,bool finishing) -> ProgressBox& {
    SCOPED_MUTEX(&m_mutex);
    if(m_detailed)return *this;
    if(size<=0 || offset<0 || offset>size)return *this;
    if(m_overall && (size!=m_size || offset<m_offset))return *this;
    if(!m_overall){m_last_offset=0;m_speed=0;m_timestamp.Update();}
    // Called by completed transfer chunks or bounded metadata/deletion steps,
    // never an idle timer: repeated finishing steps are real worker activity.
    m_safety.Touch(armGetSystemTick());
    m_overall=true;m_finishing=finishing;m_offset=offset;m_size=size;m_transfer=stage;
    return *this;
}

auto ProgressBox::SetImage(int image) -> ProgressBox& {
    SCOPED_MUTEX(&m_mutex);
    m_image_pending = image;
    m_is_image_pending = true;
    return *this;
}

auto ProgressBox::SetImageData(std::vector<u8>& data) -> ProgressBox& {
    SCOPED_MUTEX(&m_mutex);
    std::swap(m_image_data, data);
    return *this;
}

auto ProgressBox::SetImageDataConst(std::span<const u8> data) -> ProgressBox& {
    SCOPED_MUTEX(&m_mutex);
    m_image_data.resize(data.size());
    std::memcpy(m_image_data.data(), data.data(), m_image_data.size());
    return *this;
}

void ProgressBox::RequestExit() {
    SCOPED_MUTEX(&m_mutex);
    m_network_rate.End();
    m_stop_source.request_stop();
    ueventSignal(GetCancelEvent());

    // cancel any registered events.
    for (auto& e : m_cancel_events) {
        ueventSignal(e);
    }
}

auto ProgressBox::ShouldExit() -> bool {
    return m_stop_source.stop_requested();
}

auto ProgressBox::ShouldExitResult() -> Result {
    if (ShouldExit()) {
        R_THROW(Result_TransferCancelled);
    }
    R_SUCCEED();
}

void ProgressBox::AddCancelEvent(UEvent* event) {
    if (!event) {
        return;
    }

    SCOPED_MUTEX(&m_mutex);
    if (std::ranges::find(m_cancel_events, event) == m_cancel_events.end()) {
        m_cancel_events.emplace_back(event);
    }
}

void ProgressBox::RemoveCancelEvent(const UEvent* event) {
    if (!event) {
        return;
    }

    SCOPED_MUTEX(&m_mutex);
    m_cancel_events.erase(std::remove(m_cancel_events.begin(), m_cancel_events.end(), event), m_cancel_events.end());
}

auto ProgressBox::CopyFile(fs::Fs* fs_src, fs::Fs* fs_dst, const fs::FsPath& src_path, const fs::FsPath& dst_path, bool single_threaded) -> Result {
    const auto is_file_based_emummc = App::IsFileBaseEmummc();
    const auto is_both_native = fs_src->IsNative() && fs_dst->IsNative();

    fs::File src_file;
    R_TRY(fs_src->OpenFile(src_path, FsOpenMode_Read, &src_file));

    s64 src_size;
    R_TRY(src_file.GetSize(&src_size));

    // this can fail if it already exists so we ignore the result.
    // if the file actually failed to be created, the result is implicitly
    // handled when we try and open it for writing.
    fs_dst->CreateFile(dst_path, src_size, 0);

    fs::File dst_file;
    R_TRY(fs_dst->OpenFile(dst_path, FsOpenMode_Write, &dst_file));
    R_TRY(dst_file.SetSize(src_size));

    R_TRY(thread::Transfer(this, src_size,
        [&](void* data, s64 off, s64 size, u64* bytes_read) -> Result {
            const auto rc = src_file.Read(off, data, size, 0, bytes_read);

            if (is_both_native && is_file_based_emummc) {
                svcSleepThread(2e+6); // 2ms
            }

            return rc;
        },
        [&](const void* data, s64 off, s64 size) -> Result {
            const auto rc = dst_file.Write(off, data, size, 0);

            if (is_both_native && is_file_based_emummc) {
                svcSleepThread(2e+6); // 2ms
            }

            return rc;
        }, single_threaded ? thread::Mode::SingleThreaded : thread::Mode::MultiThreaded
    ));

    R_SUCCEED();
}

auto ProgressBox::CopyFile(fs::Fs* fs, const fs::FsPath& src_path, const fs::FsPath& dst_path, bool single_threaded) -> Result {
    return CopyFile(fs, fs, src_path, dst_path, single_threaded);
}

auto ProgressBox::CopyFile(const fs::FsPath& src_path, const fs::FsPath& dst_path, bool single_threaded) -> Result {
    fs::FsNativeSd fs;
    R_TRY(fs.GetFsOpenResult());
    return CopyFile(&fs, src_path, dst_path, single_threaded);
}

void ProgressBox::Yield() {
    svcSleepThread(YieldType_WithoutCoreMigration);
}

void ProgressBox::FreeImage() {
    if (m_image && m_own_image) {
        nvgDeleteImage(App::GetVg(), m_image);
    }

    m_image = 0;
    m_own_image = false;
}

} // namespace sphaira::ui
