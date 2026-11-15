#pragma once

namespace UI::Util {
    std::string_view TrimWhitespace(std::string_view text);

    std::wstring Utf8ToWide(std::string_view text);

    std::wstring ToLowerWide(const std::wstring& text);

    int CompareLinguistic(const std::wstring& a, const std::wstring& b);

    bool IsSkinTarget(RE::Actor* a_actor);

    bool IsFemale(RE::Actor* a_actor);

    struct NameOrder {
        std::string name;
        std::string displayName;
        std::wstring searchText;
        std::wstring sortKey;
        bool hasAlnum{false};
    };

    NameOrder MakeNameOrder(const std::string& a_name);

    bool IsBeforeInNameOrder(const NameOrder& a, const NameOrder& b);
}  // namespace UI::Util
