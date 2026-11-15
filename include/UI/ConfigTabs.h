#pragma once

namespace UI::ConfigTabs {
    void RenderSettings();

    void RenderIncludeFiles();

    void RenderViewer();

    void Reset();

    struct ReloadResult {
        std::string message;
        bool failed{false};
        bool filesSkipped{false};
    };

    ReloadResult Reload();
}  // namespace UI::ConfigTabs
