#pragma once

#include "ui/widget.hpp"
#include "ui/nexus_dialog.hpp"
#include <optional>

namespace sphaira::ui {

class ErrorBox final : public Widget {
public:
    ErrorBox(Result code, const std::string& message);
    ErrorBox(const std::string& message);

    auto Update(Controller* controller, TouchInfo* touch) -> void override;
    auto Draw(NVGcontext* vg, Theme* theme) -> void override;

private:
    std::string m_message{};
    nexusdialog::ScrollableBody m_message_scroll;
};

} // namespace sphaira::ui
