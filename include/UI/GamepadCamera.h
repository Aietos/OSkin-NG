#pragma once
#include "UI/Widgets.h"

namespace UI::GamepadCamera {
    void OnMenuOpened();

    void Control();

    bool IsInUse();

    std::vector<Widgets::ControlHint> Hints();
}  // namespace UI::GamepadCamera
