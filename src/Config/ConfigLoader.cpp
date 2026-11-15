#include "Config/ConfigLoader.h"

#include "STL.h"

namespace {
    constexpr auto SchemaPath{"Data/SKSE/Plugins/OSkin_skinDistributionConfig_schema.json"};
    constexpr auto ConfigPath{"Data/SKSE/Plugins/OSkin_skinDistributionConfig.json"};
    constexpr auto IncludesDir{"Data/SKSE/Plugins/OSkin/oskin_includes"};
    constexpr auto RemoveKey{"remove"};
    constexpr auto RulesKey{"skinRules"};

    using Allocator = rapidjson::Document::AllocatorType;

    enum class KeyShape {
        kList,  // ["value", ...]
        kNamed,
        kFormIDSkins,  // {"plugin": {"FormID": skins}}
        kFormIDList,   // {"plugin": ["FormID", ...]}
        kValue         // a bool, a string or a number, only the master config sets it
    };

    constexpr std::array<std::pair<std::string_view, KeyShape>, 7> KeyShapes{{
        {"skinRules"sv, KeyShape::kNamed},
        {"blacklistedSkinsFromRandomDistribution"sv, KeyShape::kList},
        {"blacklistedSkinsShowInMenu"sv, KeyShape::kValue},
        {"skinMixes"sv, KeyShape::kNamed},
        {"menuHotkey"sv, KeyShape::kValue},
        {"disableSkinDistribution"sv, KeyShape::kValue},
        {"playerFollowsRules"sv, KeyShape::kValue},
    }};

    std::optional<KeyShape> GetKeyShape(const std::string_view a_key) {
        const auto shape{std::ranges::find(KeyShapes, a_key, &std::pair<std::string_view, KeyShape>::first)};

        if (shape == KeyShapes.end()) return std::nullopt;

        return shape->second;
    }

    std::string_view View(const rapidjson::Value& a_value) { return {a_value.GetString(), a_value.GetStringLength()}; }

    std::string_view Text(const rapidjson::Value& a_value) {
        return a_value.IsString() ? View(a_value) : "<not text>"sv;
    }

    rapidjson::Value Copy(const rapidjson::Value& a_value, Allocator& a_allocator) {
        rapidjson::Value copy;
        copy.CopyFrom(a_value, a_allocator);

        return copy;
    }

    std::string FileName(const fs::path& a_path) {
        return SKSE::stl::utf16_to_utf8(a_path.filename().native()).value_or("<file name not readable>");
    }

    std::optional<std::string> ReadJsonFile(const fs::path& a_path, rapidjson::Document& a_document) {
        const auto name{FileName(a_path)};
        stl::FilePtrManager file{a_path.c_str()};

        if (file.error() != 0) {
            return std::format("Couldn't open {}", name);
        }

        char buffer[65535];
        rapidjson::FileReadStream bis(file.get(), buffer, std::size(buffer));
        rapidjson::AutoUTFInputStream<unsigned, rapidjson::FileReadStream> eis(bis);

        if (a_document.ParseStream<0, rapidjson::AutoUTF<unsigned>>(eis).HasParseError()) {
            const auto error{std::format("{} isn't valid JSON (offset {}): {}", name, a_document.GetErrorOffset(),
                                         rapidjson::GetParseError_En(a_document.GetParseError()))};
            logger::error("{}", error);

            return error;
        }

        return std::nullopt;
    }

    std::optional<std::string> Validate(const rapidjson::Document& a_schemaDocument, rapidjson::Document& a_config,
                                        const std::string_view a_file) {
        const rapidjson::SchemaDocument schema(a_schemaDocument);

        if (rapidjson::SchemaValidator validator(schema); !a_config.Accept(validator)) {
            rapidjson::StringBuffer sb;
            const auto invalidSchemaPointer = validator.GetInvalidSchemaPointer();
            invalidSchemaPointer.StringifyUriFragment(sb);
            logger::error("Invalid schema: {}", sb.GetString());
            logger::error("Invalid keyword: {}", validator.GetInvalidSchemaKeyword());
            sb.Clear();
            const auto invalidDocumentPointer = validator.GetInvalidDocumentPointer();
            invalidDocumentPointer.StringifyUriFragment(sb);
            logger::error("Invalid document: {}", sb.GetString());
            sb.Clear();
            if (auto* err_value_ptr = invalidDocumentPointer.Get(a_config)) {
                rapidjson::PrettyWriter writer(sb);
                err_value_ptr->Accept(writer);
                logger::error("Error at: {}", sb.GetString());
                sb.Clear();
            }
            if (auto* err_values_schema_pointer = invalidSchemaPointer.Get(a_schemaDocument)) {
                rapidjson::PrettyWriter writer(sb);
                err_values_schema_pointer->Accept(writer);
                logger::error("Schema Definition of Error: {}", sb.GetString());
            }

            return std::format("{} doesn't match the config's schema", a_file);
        }

        return std::nullopt;
    }

    bool IsLightPlugin(const rapidjson::Value& a_plugin) {
        const auto* const file{RE::TESDataHandler::GetSingleton()->LookupModByName(View(a_plugin))};

        return file && file->IsLight();
    }

    // A FormID can be written with or without its load order digits, so FormIDs match by their ID inside the plugin
    // That's the last 6 digits, or 3 for a light plugin
    std::optional<RE::FormID> ToLocalFormID(const rapidjson::Value& a_formID, const bool a_light) {
        if (!a_formID.IsString()) return std::nullopt;

        const auto text{View(a_formID)};
        const auto* const end{text.data() + text.size()};
        RE::FormID value{};

        if (text.empty() || text.size() > 8) return std::nullopt;

        if (const auto [last, error]{std::from_chars(text.data(), end, value, 16)};
            error != std::errc{} || last != end) {
            return std::nullopt;
        }

        return value & (a_light ? 0xFFFu : 0xFFFFFFu);
    }

    bool IsSameFormID(const rapidjson::Value& a_lhs, const rapidjson::Value& a_rhs, const bool a_light) {
        const auto lhs{ToLocalFormID(a_lhs, a_light)};
        const auto rhs{ToLocalFormID(a_rhs, a_light)};

        if (!lhs || !rhs) return a_lhs == a_rhs;

        return *lhs == *rhs;
    }

    rapidjson::Value::MemberIterator FindFormIDMember(rapidjson::Value& a_object, const rapidjson::Value& a_formID,
                                                      const bool a_light) {
        for (auto member{a_object.MemberBegin()}; member != a_object.MemberEnd(); ++member) {
            if (IsSameFormID(member->name, a_formID, a_light)) return member;
        }

        return a_object.MemberEnd();
    }

    rapidjson::Value::ValueIterator FindFormIDValue(rapidjson::Value& a_array, const rapidjson::Value& a_formID,
                                                    const bool a_light) {
        return std::find_if(a_array.Begin(), a_array.End(),
                            [&](const rapidjson::Value& a_value) { return IsSameFormID(a_value, a_formID, a_light); });
    }

    // New rules go first instead, so a file loaded later has its rules on top
    void MergeKey(rapidjson::Value& a_target, const rapidjson::Value& a_source, const KeyShape a_shape,
                  Allocator& a_allocator, const std::string_view a_file, const std::string_view a_key) {
        const bool lists{a_shape == KeyShape::kList};

        if (lists ? !a_target.IsArray() || !a_source.IsArray() : !a_target.IsObject() || !a_source.IsObject()) {
            logger::warn("{}: ignoring key '{}', it should be {}", a_file, a_key, lists ? "a list" : "an object");
            return;
        }

        switch (a_shape) {
            case KeyShape::kList: {
                for (const auto& value : a_source.GetArray()) {
                    if (std::find(a_target.Begin(), a_target.End(), value) == a_target.End()) {
                        a_target.PushBack(Copy(value, a_allocator), a_allocator);
                    }
                }
                return;
            }
            case KeyShape::kNamed: {
                const bool first{a_key == RulesKey};
                rapidjson::Value added{rapidjson::kObjectType};

                for (auto entry{a_source.MemberBegin()}; entry != a_source.MemberEnd(); ++entry) {
                    if (const auto existing{a_target.FindMember(entry->name)}; existing != a_target.MemberEnd()) {
                        existing->value.CopyFrom(entry->value, a_allocator);
                    } else if (first && !added.HasMember(entry->name)) {
                        added.AddMember(Copy(entry->name, a_allocator), Copy(entry->value, a_allocator), a_allocator);
                    } else if (!first) {
                        a_target.AddMember(Copy(entry->name, a_allocator), Copy(entry->value, a_allocator),
                                           a_allocator);
                    }
                }

                if (added.ObjectEmpty()) return;

                for (auto entry{a_target.MemberBegin()}; entry != a_target.MemberEnd(); ++entry) {
                    added.AddMember(entry->name, entry->value, a_allocator);
                }

                a_target = std::move(added);
                return;
            }
            case KeyShape::kFormIDSkins:
            case KeyShape::kFormIDList: {
                const bool skins{a_shape == KeyShape::kFormIDSkins};

                for (auto plugin{a_source.MemberBegin()}; plugin != a_source.MemberEnd(); ++plugin) {
                    const auto existing{a_target.FindMember(plugin->name)};

                    if (existing == a_target.MemberEnd()) {
                        a_target.AddMember(Copy(plugin->name, a_allocator), Copy(plugin->value, a_allocator),
                                           a_allocator);
                        continue;
                    }

                    auto& target{existing->value};
                    const auto& source{plugin->value};
                    const bool light{IsLightPlugin(plugin->name)};

                    if (skins ? !target.IsObject() || !source.IsObject() : !target.IsArray() || !source.IsArray()) {
                        logger::warn("{}: ignoring '{}' in key '{}', it should be {}", a_file, View(plugin->name),
                                     a_key, skins ? "an object" : "a list");
                        continue;
                    }

                    if (skins) {
                        for (auto entry{source.MemberBegin()}; entry != source.MemberEnd(); ++entry) {
                            if (const auto same{FindFormIDMember(target, entry->name, light)};
                                same != target.MemberEnd()) {
                                same->value.CopyFrom(entry->value, a_allocator);
                            } else {
                                target.AddMember(Copy(entry->name, a_allocator), Copy(entry->value, a_allocator),
                                                 a_allocator);
                            }
                        }
                    } else {
                        for (const auto& formID : source.GetArray()) {
                            if (FindFormIDValue(target, formID, light) == target.End()) {
                                target.PushBack(Copy(formID, a_allocator), a_allocator);
                            }
                        }
                    }
                }
                return;
            }
            case KeyShape::kValue:
                return;
        }
    }

    void RemoveFromKey(rapidjson::Value& a_target, const rapidjson::Value& a_entries, const KeyShape a_shape,
                       const std::string_view a_file, const std::string_view a_key, const bool a_log) {
        const bool formIDs{a_shape == KeyShape::kFormIDSkins || a_shape == KeyShape::kFormIDList};

        if (formIDs ? !a_entries.IsObject() : !a_entries.IsArray()) {
            logger::warn("{}: ignoring '{}' in \"remove\", it should be {}", a_file, a_key,
                         formIDs ? "an object of plugins" : "a list");
            return;
        }

        const auto removed = [&](const std::string_view a_entry, const bool a_found) {
            if (!a_log) return;

            if (a_found) {
                logger::info("{}: removed '{}' from {}", a_file, a_entry, a_key);
            } else {
                logger::info("{}: nothing to remove for '{}' in {}", a_file, a_entry, a_key);
            }
        };

        switch (a_shape) {
            case KeyShape::kList: {
                for (const auto& entry : a_entries.GetArray()) {
                    const auto found{std::find(a_target.Begin(), a_target.End(), entry)};

                    removed(Text(entry), found != a_target.End());

                    if (found != a_target.End()) {
                        a_target.Erase(found);
                    }
                }
                return;
            }
            case KeyShape::kNamed: {
                for (const auto& entry : a_entries.GetArray()) {
                    const auto found{entry.IsString() ? a_target.FindMember(entry) : a_target.MemberEnd()};

                    removed(Text(entry), found != a_target.MemberEnd());

                    // EraseMember keeps the order of the others
                    if (found != a_target.MemberEnd()) {
                        a_target.EraseMember(found);
                    }
                }
                return;
            }
            case KeyShape::kFormIDSkins:
            case KeyShape::kFormIDList: {
                const bool skins{a_shape == KeyShape::kFormIDSkins};

                for (auto plugin{a_entries.MemberBegin()}; plugin != a_entries.MemberEnd(); ++plugin) {
                    const auto pluginName{View(plugin->name)};

                    if (!plugin->value.IsArray()) {
                        logger::warn("{}: ignoring '{}' of {} in \"remove\", it should be a list of FormIDs", a_file,
                                     pluginName, a_key);
                        continue;
                    }

                    const auto existing{a_target.FindMember(plugin->name)};
                    const bool hasPlugin{existing != a_target.MemberEnd() &&
                                         (skins ? existing->value.IsObject() : existing->value.IsArray())};
                    const bool light{IsLightPlugin(plugin->name)};
                    bool removedAny{false};

                    for (const auto& formID : plugin->value.GetArray()) {
                        const auto entry{std::format("{} {}", pluginName, Text(formID))};

                        if (!hasPlugin) {
                            removed(entry, false);
                        } else if (skins) {
                            const auto found{FindFormIDMember(existing->value, formID, light)};

                            removed(entry, found != existing->value.MemberEnd());

                            if (found != existing->value.MemberEnd()) {
                                existing->value.EraseMember(found);
                                removedAny = true;
                            }
                        } else {
                            const auto found{FindFormIDValue(existing->value, formID, light)};

                            removed(entry, found != existing->value.End());

                            if (found != existing->value.End()) {
                                existing->value.Erase(found);
                                removedAny = true;
                            }
                        }
                    }

                    // A plugin left empty is taken out, so a file that adds it again puts it last
                    if (removedAny && (skins ? existing->value.ObjectEmpty() : existing->value.Empty())) {
                        a_target.EraseMember(existing);
                    }
                }
                return;
            }
            case KeyShape::kValue:
                return;
        }
    }

    void ApplyRemovals(rapidjson::Document& a_config, const rapidjson::Value& a_remove, const std::string_view a_file,
                       const bool a_log) {
        if (!a_remove.IsObject()) {
            logger::warn("{}: ignoring \"remove\", it should be an object", a_file);
            return;
        }

        for (auto key{a_remove.MemberBegin()}; key != a_remove.MemberEnd(); ++key) {
            const auto keyName{View(key->name)};
            const auto shape{GetKeyShape(keyName)};

            if (!shape || *shape == KeyShape::kValue) {
                logger::warn("{}: ignoring '{}' in \"remove\", it isn't a key entries can be removed from", a_file,
                             keyName);
                continue;
            }

            const auto target{a_config.FindMember(key->name)};
            const bool lists{*shape == KeyShape::kList};

            if (target == a_config.MemberEnd() || (lists ? !target->value.IsArray() : !target->value.IsObject())) {
                if (a_log) {
                    logger::info("{}: nothing to remove from {}, no file before sets it", a_file, keyName);
                }

                continue;
            }

            RemoveFromKey(target->value, key->value, *shape, a_file, keyName, a_log);
        }
    }

    std::optional<std::string> MergeIncludeDocument(rapidjson::Document& a_config, const rapidjson::Document& a_include,
                                                    const std::string& a_name, const bool a_log) {
        if (!a_include.IsObject()) return std::format("{} is not a JSON object", a_name);

        auto& allocator{a_config.GetAllocator()};

        // The removals come first, so a file can remove an entry and add it again
        if (const auto remove{a_include.FindMember(RemoveKey)}; remove != a_include.MemberEnd()) {
            ApplyRemovals(a_config, remove->value, a_name, a_log);
        }

        for (auto member{a_include.MemberBegin()}; member != a_include.MemberEnd(); ++member) {
            const auto key{View(member->name)};
            const auto shape{GetKeyShape(key)};

            if (key == RemoveKey) continue;

            if (!shape) {
                logger::warn("{}: ignoring unknown key '{}'", a_name, key);
                continue;
            }

            if (*shape == KeyShape::kValue) {
                logger::warn("{}: ignoring key '{}', only the master config can set it", a_name, key);
                continue;
            }

            auto target{a_config.FindMember(member->name)};

            if (target == a_config.MemberEnd()) {
                a_config.AddMember(
                    Copy(member->name, allocator),
                    rapidjson::Value(*shape == KeyShape::kList ? rapidjson::kArrayType : rapidjson::kObjectType),
                    allocator);
                target = a_config.FindMember(member->name);
            }

            MergeKey(target->value, member->value, *shape, allocator, a_name, key);
        }

        return std::nullopt;
    }

    bool IsSameName(const std::wstring_view a_lhs, const std::wstring_view a_rhs) {
        return CompareStringOrdinal(a_lhs.data(), static_cast<int>(a_lhs.size()), a_rhs.data(),
                                    static_cast<int>(a_rhs.size()), TRUE) == CSTR_EQUAL;
    }

    // Where a file or folder really is, MO2 shows the game a virtual folder
    fs::path GetRealPath(const fs::path& a_path, const bool a_folder) {
        const HANDLE handle{CreateFileW(a_path.c_str(), FILE_READ_ATTRIBUTES,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                        a_folder ? FILE_FLAG_BACKUP_SEMANTICS : 0, nullptr)};

        if (handle == INVALID_HANDLE_VALUE) return {};

        wchar_t buffer[2048];
        const auto length{GetFinalPathNameByHandleW(handle, buffer, static_cast<DWORD>(std::size(buffer)),
                                                    FILE_NAME_NORMALIZED | VOLUME_NAME_DOS)};
        CloseHandle(handle);

        if (length == 0 || length >= std::size(buffer)) return {};

        return fs::path{std::wstring_view{buffer, length}};
    }

    std::string DisplayPath(const fs::path& a_path) {
        std::wstring_view text{a_path.native()};

        if (text.starts_with(LR"(\\?\)")) {
            text.remove_prefix(4);
        }

        return SKSE::stl::utf16_to_utf8(text).value_or("<path not readable>");
    }

    struct IncludeFile {
        std::wstring name;
        fs::path path;
        fs::path realPath;
    };

    bool IsInGameConfigFile(const std::wstring_view a_name) {
        return IsSameName(a_name, fs::path{Parser::InGameConfigFileName}.native());
    }

    // Alphabetical, ignoring case like Windows. The in-game file always goes last so its changes win
    void SortIncludeFiles(std::vector<IncludeFile>& a_files) {
        std::ranges::sort(a_files, [](const IncludeFile& a, const IncludeFile& b) {
            const bool aInGame{IsInGameConfigFile(a.name)};
            const bool bInGame{IsInGameConfigFile(b.name)};

            if (aInGame != bInGame) return bInGame;

            return CompareStringOrdinal(a.name.c_str(), static_cast<int>(a.name.size()), b.name.c_str(),
                                        static_cast<int>(b.name.size()), TRUE) == CSTR_LESS_THAN;
        });
    }

    std::vector<IncludeFile> FindIncludeFiles(const bool a_log) {
        std::vector<IncludeFile> files;
        std::vector<fs::path> realFolders;

        const auto addRealFolder = [&](const fs::path& a_folder) {
            const bool known{std::ranges::any_of(
                realFolders, [&](const fs::path& a_known) { return IsSameName(a_known.native(), a_folder.native()); })};

            if (!a_folder.empty() && !known) {
                realFolders.push_back(a_folder);
            }
        };

        addRealFolder(GetRealPath(IncludesDir, true));

        for (const auto& entry : fs::directory_iterator(IncludesDir)) {
            if (!entry.is_regular_file()) continue;

            const auto realPath{GetRealPath(entry.path(), false)};
            addRealFolder(realPath.parent_path());

            if (entry.path().extension() == ".json") {
                files.push_back({entry.path().filename().native(), entry.path(), realPath});
            }
        }

        // MO2 hides files added while the game runs. In Overwrite they're listed but can't be opened, in a mod
        // folder they aren't listed at all. So the real folders are read too
        for (const auto& folder : realFolders) {
            std::error_code error;

            for (const auto& entry : fs::directory_iterator(folder, error)) {
                if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;

                const auto name{entry.path().filename().native()};
                const auto listed{std::ranges::find_if(
                    files, [&](const IncludeFile& a_file) { return IsSameName(a_file.name, name); })};

                if (listed == files.end()) {
                    if (a_log) {
                        logger::info("Found {} in {}, it was added while the game runs", FileName(entry.path()),
                                     DisplayPath(folder));
                    }

                    files.push_back({name, entry.path(), entry.path()});
                } else if (listed->realPath.empty()) {
                    if (a_log) {
                        logger::info(
                            "{} can't be opened in the game's Data folder, it was added while the game runs. It's "
                            "read from {}",
                            FileName(entry.path()), DisplayPath(folder));
                    }

                    listed->path = entry.path();
                    listed->realPath = entry.path();
                }
            }

            if (error) {
                logger::warn("Couldn't read the folder {}: {}", DisplayPath(folder), error.message());
            }
        }

        SortIncludeFiles(files);

        return files;
    }

    bool UsesWindowsLineEnds(const fs::path& a_path) {
        std::ifstream file{a_path, std::ios::binary};
        const std::string text{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};

        return text.find('\n') == std::string::npos || text.find("\r\n") != std::string::npos;
    }

    // Written in place, not through a temporary file, so MO2 writes to the copy the game uses
    // and Vortex keeps its hard link
    std::optional<std::string> WriteJsonFile(const fs::path& a_path, const rapidjson::Document& a_document) {
        const auto name{FileName(a_path)};
        rapidjson::StringBuffer buffer;
        rapidjson::PrettyWriter writer(buffer);
        writer.SetIndent(' ', 2);
        a_document.Accept(writer);
        buffer.Put('\n');

        // JSON text can't have a line end inside a string, they're all between values
        std::string text{buffer.GetString(), buffer.GetSize()};

        if (UsesWindowsLineEnds(a_path)) {
            std::string withWindowsLineEnds;
            withWindowsLineEnds.reserve(text.size() + text.size() / 16);

            for (const char c : text) {
                if (c == '\n') withWindowsLineEnds.push_back('\r');

                withWindowsLineEnds.push_back(c);
            }

            text = std::move(withWindowsLineEnds);
        }

        stl::FilePtrManager file{a_path.c_str(), L"wb"};

        if (file.error() != 0) return std::format("Couldn't write {}", name);

        const auto written{std::fwrite(text.data(), 1, text.size(), file.get())};

        if (written != text.size() || std::fflush(file.get()) != 0) {
            return std::format("Couldn't write all of {}", name);
        }

        logger::info("Wrote {}", name);

        return std::nullopt;
    }

    std::optional<std::string> WriteMasterValue(const char* a_key,
                                                const std::function<rapidjson::Value(Allocator&)>& a_value) {
        rapidjson::Document schemaDocument;
        rapidjson::Document master;

        if (auto error{ReadJsonFile(SchemaPath, schemaDocument)}) return error;

        if (auto error{ReadJsonFile(ConfigPath, master)}) return error;

        if (!master.IsObject()) return "OSkin_skinDistributionConfig.json is not a JSON object";

        auto& allocator{master.GetAllocator()};

        if (const auto member{master.FindMember(a_key)}; member != master.MemberEnd()) {
            member->value = a_value(allocator);
        } else {
            master.AddMember(rapidjson::Value{a_key, allocator}, a_value(allocator), allocator);
        }

        if (auto error{Validate(schemaDocument, master, FileName(ConfigPath))}) return error;

        return WriteJsonFile(ConfigPath, master);
    }

    rapidjson::Value StringValue(const std::string_view a_text) {
        return rapidjson::Value{rapidjson::StringRef(a_text.data(), a_text.size())};
    }

    // FormIDs by their value inside the plugin, like the merge compares them, other entries by their text
    std::string SourceId(const std::string_view a_key, const std::string_view a_plugin,
                         const std::string_view a_entry) {
        const auto shape{GetKeyShape(a_key)};

        if (shape != KeyShape::kFormIDSkins && shape != KeyShape::kFormIDList) {
            return std::format("{}|{}", a_key, a_entry);
        }

        if (const auto formID{ToLocalFormID(StringValue(a_entry), IsLightPlugin(StringValue(a_plugin)))}) {
            return std::format("{}|{}|{:X}", a_key, a_plugin, *formID);
        }

        return std::format("{}|{}|{}", a_key, a_plugin, a_entry);
    }

    void RecordSources(const rapidjson::Value& a_document, const std::string& a_file, Parser::EntrySources& a_sources) {
        if (!a_document.IsObject()) return;

        for (auto member{a_document.MemberBegin()}; member != a_document.MemberEnd(); ++member) {
            const auto key{View(member->name)};
            const auto shape{GetKeyShape(key)};
            const auto& value{member->value};

            if (!shape) continue;

            switch (*shape) {
                case KeyShape::kList:
                    if (!value.IsArray()) break;

                    for (const auto& entry : value.GetArray()) {
                        if (entry.IsString()) a_sources[SourceId(key, {}, View(entry))] = a_file;
                    }
                    break;
                case KeyShape::kNamed:
                    if (!value.IsObject()) break;

                    for (auto entry{value.MemberBegin()}; entry != value.MemberEnd(); ++entry) {
                        a_sources[SourceId(key, {}, View(entry->name))] = a_file;
                    }
                    break;
                case KeyShape::kFormIDSkins:
                case KeyShape::kFormIDList:
                    if (!value.IsObject()) break;

                    for (auto plugin{value.MemberBegin()}; plugin != value.MemberEnd(); ++plugin) {
                        const auto pluginName{View(plugin->name)};

                        if (plugin->value.IsObject()) {
                            for (auto entry{plugin->value.MemberBegin()}; entry != plugin->value.MemberEnd(); ++entry) {
                                a_sources[SourceId(key, pluginName, View(entry->name))] = a_file;
                            }
                        } else if (plugin->value.IsArray()) {
                            for (const auto& entry : plugin->value.GetArray()) {
                                if (entry.IsString()) a_sources[SourceId(key, pluginName, View(entry))] = a_file;
                            }
                        }
                    }
                    break;
                case KeyShape::kValue:
                    break;
            }
        }
    }

    enum class InGameFile {
        kFromDisk,  // read like the other files
        kLeftOut,   // to find what the other files set
        kGiven      // a document that is being edited takes its place
    };

    std::optional<std::string> MergeConfig(rapidjson::Document& a_config,
                                           std::vector<Parser::IncludeFileInfo>& a_includeFiles,
                                           const InGameFile a_inGame, const bool a_log,
                                           const rapidjson::Document* a_inGameDocument = nullptr,
                                           Parser::EntrySources* a_sources = nullptr) {
        std::optional<stl::timeit> timer;

        if (a_log) {
            timer.emplace(std::source_location::current());
        }

        rapidjson::Document schemaDocument;

        if (auto error{ReadJsonFile(SchemaPath, schemaDocument)}) return error;

        if (auto error{ReadJsonFile(ConfigPath, a_config)}) return error;

        if (auto error{Validate(schemaDocument, a_config, FileName(ConfigPath))}) return error;

        if (a_log) {
            logger::info("Validated {} successfully", ConfigPath);
        }

        if (a_sources) {
            RecordSources(a_config, FileName(ConfigPath), *a_sources);
        }

        std::vector<IncludeFile> includeFiles;

        if (fs::is_directory(IncludesDir)) {
            includeFiles = FindIncludeFiles(a_log);
        }

        const bool given{a_inGame == InGameFile::kGiven && a_inGameDocument};

        // The in-game file being edited may not exist yet
        if (given && std::ranges::none_of(includeFiles,
                                          [](const IncludeFile& a_file) { return IsInGameConfigFile(a_file.name); })) {
            includeFiles.push_back({fs::path{Parser::InGameConfigFileName}.native(), {}, {}});
            SortIncludeFiles(includeFiles);
        }

        for (std::size_t i{0}; i < includeFiles.size(); ++i) {
            const auto& file{includeFiles[i]};
            const bool inGame{IsInGameConfigFile(file.name)};
            const auto name{SKSE::stl::utf16_to_utf8(file.name).value_or("<file name not readable>")};
            const auto from{file.realPath.empty() ? std::string{} : DisplayPath(file.realPath)};

            if (inGame && a_inGame == InGameFile::kLeftOut) continue;

            auto& info{a_includeFiles.emplace_back(name, from)};
            rapidjson::Document read;
            const rapidjson::Document* include{&read};

            if (inGame && given) {
                include = a_inGameDocument;
            } else {
                if (a_log) {
                    logger::info("Loading include file [{}/{}]: {}, from {}", i + 1, includeFiles.size(), name,
                                 from.empty() ? "unknown" : from);
                }

                if (const stl::FilePtrManager opened{file.path.c_str()}; opened.error() != 0) {
                    logger::warn("Skipped {}, it couldn't be opened", name);
                    info.skipped = true;
                    continue;
                }

                if (auto error{ReadJsonFile(file.path, read)}) return error;
            }

            if (auto error{MergeIncludeDocument(a_config, *include, name, a_log)}) return error;

            if (a_sources) {
                RecordSources(*include, name, *a_sources);
            }
        }

        if (a_log) {
            logger::info("Loaded {} include file(s) from {}",
                         std::ranges::count(a_includeFiles, false, &Parser::IncludeFileInfo::skipped), IncludesDir);
        }

        return std::nullopt;
    }

    constexpr auto RemoveSchema{R"({
        "type": "object",
        "additionalProperties": {
            "anyOf": [
                {"type": "array", "items": {"type": "string"}},
                {"type": "object", "additionalProperties": {"type": "array", "items": {"type": "string"}}}
            ]
        }
    })"};

    std::optional<std::string> ReadIncludeFileSchema(rapidjson::Document& a_schemaDocument) {
        if (auto error{ReadJsonFile(SchemaPath, a_schemaDocument)}) return error;

        const auto properties{a_schemaDocument.FindMember("properties")};

        if (!a_schemaDocument.IsObject() || properties == a_schemaDocument.MemberEnd() ||
            !properties->value.IsObject()) {
            return "The config's schema has no properties";
        }

        rapidjson::Document remove;
        remove.Parse(RemoveSchema);
        properties->value.AddMember(rapidjson::StringRef(RemoveKey), Copy(remove, a_schemaDocument.GetAllocator()),
                                    a_schemaDocument.GetAllocator());

        return std::nullopt;
    }

    // Under MO2, a file added while the game runs may not open in Data, then its real path is used
    fs::path FindInGameConfigFile() {
        const auto path{fs::path{IncludesDir} / Parser::InGameConfigFileName};

        if (!fs::is_directory(IncludesDir)) return path;

        // Looked for at each read and write, without logging again
        for (const auto& file : FindIncludeFiles(false)) {
            if (IsInGameConfigFile(file.name)) return file.path;
        }

        return path;
    }

    // Empty when it doesn't exist. Compared to tell whether the file changed since it was read
    std::string ReadFileText(const fs::path& a_path) {
        std::ifstream file{a_path, std::ios::binary};

        return {std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    }

    rapidjson::Value* FindMemberValue(rapidjson::Value& a_object, const std::string_view a_key) {
        if (!a_object.IsObject()) return nullptr;

        const auto member{a_object.FindMember(StringValue(a_key))};

        return member != a_object.MemberEnd() ? &member->value : nullptr;
    }

    // Added empty when missing. The file was checked, so a_key holds the right type if it's there
    rapidjson::Value& GetOrAdd(rapidjson::Value& a_object, const std::string_view a_key, const rapidjson::Type a_type,
                               Allocator& a_allocator) {
        if (auto* const value{FindMemberValue(a_object, a_key)}) return *value;

        a_object.AddMember(rapidjson::Value{a_key.data(), static_cast<rapidjson::SizeType>(a_key.size()), a_allocator},
                           rapidjson::Value{a_type}, a_allocator);

        return (a_object.MemberEnd() - 1)->value;
    }

    rapidjson::Value& GetOrAddObject(rapidjson::Value& a_object, const std::string_view a_key, Allocator& a_allocator) {
        return GetOrAdd(a_object, a_key, rapidjson::kObjectType, a_allocator);
    }

    rapidjson::Value& GetOrAddArray(rapidjson::Value& a_object, const std::string_view a_key, Allocator& a_allocator) {
        return GetOrAdd(a_object, a_key, rapidjson::kArrayType, a_allocator);
    }

    // Erases a_key from a_object once it holds nothing anymore, so the file doesn't keep empty sections
    void EraseIfEmpty(rapidjson::Value& a_object, const std::string_view a_key) {
        if (!a_object.IsObject()) return;

        const auto member{a_object.FindMember(StringValue(a_key))};

        if (member == a_object.MemberEnd()) return;

        if ((member->value.IsObject() && member->value.ObjectEmpty()) ||
            (member->value.IsArray() && member->value.Empty())) {
            a_object.EraseMember(member);
        }
    }

    bool EraseValues(rapidjson::Value& a_list, const std::function<bool(const rapidjson::Value&)>& a_matches) {
        bool erased{false};

        if (!a_list.IsArray()) return false;

        for (auto value{a_list.Begin()}; value != a_list.End();) {
            if (a_matches(*value)) {
                value = a_list.Erase(value);
                erased = true;
            } else {
                ++value;
            }
        }

        return erased;
    }

    bool IsFormIDShape(const std::optional<KeyShape> a_shape) {
        return a_shape == KeyShape::kFormIDSkins || a_shape == KeyShape::kFormIDList;
    }

    // The schema lets each key of "remove" be a list or an object, the editing needs the one the key takes
    std::optional<std::string> CheckRemovals(const rapidjson::Document& a_config) {
        const auto remove{a_config.FindMember(RemoveKey)};

        if (remove == a_config.MemberEnd() || !remove->value.IsObject()) return std::nullopt;

        for (auto key{remove->value.MemberBegin()}; key != remove->value.MemberEnd(); ++key) {
            const auto shape{GetKeyShape(View(key->name))};

            if (!shape || *shape == KeyShape::kValue) continue;

            if (const bool formIDs{IsFormIDShape(shape)}; formIDs ? !key->value.IsObject() : !key->value.IsArray()) {
                return std::format("{}: \"remove\" has {} as {}, it should be {}", Parser::InGameConfigFileName,
                                   View(key->name), formIDs ? "a list" : "an object",
                                   formIDs ? "an object of plugins" : "a list");
            }
        }

        return std::nullopt;
    }

    // Reads the in-game file from the disk, so what was written to it by hand stays
    std::optional<std::string> EditInGameConfig(
        const std::function<std::optional<std::string>(rapidjson::Document&)>& a_edit) {
        rapidjson::Document config;
        std::string text;

        if (auto error{Parser::ReadInGameConfig(config, text)}) return error;

        if (auto error{a_edit(config)}) return error;

        return Parser::WriteInGameConfig(config, text);
    }

    std::optional<std::string> IsRuleSetByOtherFiles(const std::string_view a_name, bool& a_set) {
        rapidjson::Document others;
        std::vector<Parser::IncludeFileInfo> files;

        if (auto error{MergeConfig(others, files, InGameFile::kLeftOut, false)}) return error;

        auto* const rules{FindMemberValue(others, RulesKey)};
        a_set = rules && rules->IsObject() && rules->HasMember(StringValue(a_name));

        logger::info("The rule {} {} by a file other than {}", a_name, a_set ? "is set" : "isn't set",
                     Parser::InGameConfigFileName);

        return std::nullopt;
    }
}  // namespace

std::optional<std::string> Parser::LoadConfig(rapidjson::Document& a_config,
                                              std::vector<IncludeFileInfo>& a_includeFiles) {
    return MergeConfig(a_config, a_includeFiles, InGameFile::kFromDisk, true);
}

std::optional<std::string> Parser::MergeConfigFiles(rapidjson::Document& a_config, EntrySources& a_sources,
                                                    const rapidjson::Document* a_inGame) {
    std::vector<IncludeFileInfo> files;

    return MergeConfig(a_config, files, a_inGame ? InGameFile::kGiven : InGameFile::kLeftOut, false, a_inGame,
                       &a_sources);
}

std::string Parser::EntrySourceId(const EntryRef& a_ref) { return SourceId(a_ref.key, a_ref.plugin, a_ref.entry); }

std::optional<std::string> Parser::ReadInGameConfig(rapidjson::Document& a_config, std::string& a_text) {
    const auto path{FindInGameConfigFile()};

    a_text = ReadFileText(path);

    if (!fs::exists(path)) {
        a_config.SetObject();
        return std::nullopt;
    }

    if (auto error{ReadJsonFile(path, a_config)}) return error;

    return ValidateInGameConfig(a_config);
}

std::optional<std::string> Parser::ValidateInGameConfig(rapidjson::Document& a_config) {
    rapidjson::Document schemaDocument;

    if (!a_config.IsObject()) return std::format("{} is not a JSON object", InGameConfigFileName);

    if (auto error{ReadIncludeFileSchema(schemaDocument)}) return error;

    if (auto error{Validate(schemaDocument, a_config, InGameConfigFileName)}) return error;

    return CheckRemovals(a_config);
}

std::optional<std::string> Parser::WriteInGameConfig(const rapidjson::Document& a_config,
                                                     const std::string_view a_readText) {
    const auto path{FindInGameConfigFile()};
    const auto name{FileName(path)};
    rapidjson::Document checked;

    if (ReadFileText(path) != a_readText) {
        logger::warn("{} changed since it was read, it's not written", name);
        return std::format(
            "{} changed since it was read, for example through Save skin to config. It's read again now, "
            "please make your change once more",
            name);
    }

    checked.CopyFrom(a_config, checked.GetAllocator());

    if (auto error{ValidateInGameConfig(checked)}) return error;

    if (std::error_code error; !fs::is_directory(IncludesDir) && !fs::create_directories(IncludesDir, error)) {
        return std::format("Couldn't create the folder {}: {}", IncludesDir, error.message());
    }

    return WriteJsonFile(path, a_config);
}

void Parser::InGameEdit::SetEntry(rapidjson::Document& a_config, const EntryRef& a_ref,
                                  const std::vector<std::string>* a_skins) {
    const auto shape{GetKeyShape(a_ref.key)};
    auto& allocator{a_config.GetAllocator()};
    const auto entry{StringValue(a_ref.entry)};
    const auto text = [&](const std::string_view a_text) {
        return rapidjson::Value{a_text.data(), static_cast<rapidjson::SizeType>(a_text.size()), allocator};
    };
    const auto skins = [&] {
        rapidjson::Value list{rapidjson::kArrayType};

        if (a_skins) {
            for (const auto& skin : *a_skins) {
                list.PushBack(text(skin), allocator);
            }
        }

        return list;
    };

    logger::info("{}: setting {} {} {}", InGameConfigFileName, a_ref.key, a_ref.plugin, a_ref.entry);

    if (!shape) return;

    switch (*shape) {
        case KeyShape::kList: {
            auto& list{GetOrAddArray(a_config, a_ref.key, allocator)};

            if (std::find(list.Begin(), list.End(), entry) == list.End()) {
                list.PushBack(text(a_ref.entry), allocator);
            }
            return;
        }
        case KeyShape::kNamed: {
            auto& named{GetOrAddObject(a_config, a_ref.key, allocator)};

            if (const auto found{named.FindMember(entry)}; found != named.MemberEnd()) {
                found->value = skins();
            } else {
                named.AddMember(text(a_ref.entry), skins(), allocator);
            }
            return;
        }
        case KeyShape::kFormIDSkins: {
            auto& formIDs{GetOrAddObject(GetOrAddObject(a_config, a_ref.key, allocator), a_ref.plugin, allocator)};

            if (const auto same{FindFormIDMember(formIDs, entry, IsLightPlugin(StringValue(a_ref.plugin)))};
                same != formIDs.MemberEnd()) {
                same->value = skins();
            } else {
                formIDs.AddMember(text(a_ref.entry), skins(), allocator);
            }
            return;
        }
        case KeyShape::kFormIDList: {
            auto& formIDs{GetOrAddArray(GetOrAddObject(a_config, a_ref.key, allocator), a_ref.plugin, allocator)};

            if (FindFormIDValue(formIDs, entry, IsLightPlugin(StringValue(a_ref.plugin))) == formIDs.End()) {
                formIDs.PushBack(text(a_ref.entry), allocator);
            }
            return;
        }
        case KeyShape::kValue:
            return;
    }
}

void Parser::InGameEdit::SetEntryValue(rapidjson::Document& a_config, const EntryRef& a_ref,
                                       const rapidjson::Value& a_value, const bool a_first) {
    if (GetKeyShape(a_ref.key) != KeyShape::kNamed) return;

    auto& allocator{a_config.GetAllocator()};
    auto& named{GetOrAddObject(a_config, a_ref.key, allocator)};
    rapidjson::Value name{a_ref.entry.data(), static_cast<rapidjson::SizeType>(a_ref.entry.size()), allocator};

    logger::info("{}: setting {} {}", InGameConfigFileName, a_ref.key, a_ref.entry);

    if (const auto found{named.FindMember(StringValue(a_ref.entry))}; found != named.MemberEnd()) {
        found->value.CopyFrom(a_value, allocator);
    } else if (a_first) {
        rapidjson::Value entries{rapidjson::kObjectType};
        entries.AddMember(name, Copy(a_value, allocator), allocator);

        for (auto entry{named.MemberBegin()}; entry != named.MemberEnd(); ++entry) {
            entries.AddMember(entry->name, entry->value, allocator);
        }

        named = std::move(entries);
    } else {
        named.AddMember(name, Copy(a_value, allocator), allocator);
    }
}

bool Parser::InGameEdit::DeleteEntry(rapidjson::Document& a_config, const EntryRef& a_ref) {
    const auto shape{GetKeyShape(a_ref.key)};
    const auto entry{StringValue(a_ref.entry)};
    const bool light{IsLightPlugin(StringValue(a_ref.plugin))};
    auto* const key{FindMemberValue(a_config, a_ref.key)};
    bool deleted{false};

    if (!shape || !key) return false;

    switch (*shape) {
        case KeyShape::kList:
            deleted = EraseValues(*key, [&](const rapidjson::Value& a_value) { return a_value == entry; });
            break;
        case KeyShape::kNamed:
            if (const auto found{key->FindMember(entry)}; found != key->MemberEnd()) {
                key->EraseMember(found);
                deleted = true;
            }
            break;
        case KeyShape::kFormIDSkins:
            if (auto* const formIDs{FindMemberValue(*key, a_ref.plugin)}; formIDs && formIDs->IsObject()) {
                for (auto found{FindFormIDMember(*formIDs, entry, light)}; found != formIDs->MemberEnd();
                     found = FindFormIDMember(*formIDs, entry, light)) {
                    formIDs->EraseMember(found);
                    deleted = true;
                }
            }
            EraseIfEmpty(*key, a_ref.plugin);
            break;
        case KeyShape::kFormIDList:
            if (auto* const formIDs{FindMemberValue(*key, a_ref.plugin)}) {
                deleted = EraseValues(
                    *formIDs, [&](const rapidjson::Value& a_value) { return IsSameFormID(a_value, entry, light); });
            }
            EraseIfEmpty(*key, a_ref.plugin);
            break;
        case KeyShape::kValue:
            return false;
    }

    EraseIfEmpty(a_config, a_ref.key);
    logger::info("{}: {} {} {} {}", InGameConfigFileName, deleted ? "deleted" : "had no", a_ref.key, a_ref.plugin,
                 a_ref.entry);

    return deleted;
}

void Parser::InGameEdit::AddRemoval(rapidjson::Document& a_config, const EntryRef& a_ref) {
    const auto shape{GetKeyShape(a_ref.key)};
    auto& allocator{a_config.GetAllocator()};
    const auto entry{StringValue(a_ref.entry)};
    const bool light{IsLightPlugin(StringValue(a_ref.plugin))};

    if (!shape || *shape == KeyShape::kValue) return;

    logger::info("{}: removing {} {} {} set by other files", InGameConfigFileName, a_ref.key, a_ref.plugin,
                 a_ref.entry);

    auto& remove{GetOrAddObject(a_config, RemoveKey, allocator)};
    auto& list{IsFormIDShape(shape)
                   ? GetOrAddArray(GetOrAddObject(remove, a_ref.key, allocator), a_ref.plugin, allocator)
                   : GetOrAddArray(remove, a_ref.key, allocator)};
    const bool listed{IsFormIDShape(shape) ? FindFormIDValue(list, entry, light) != list.End()
                                           : std::find(list.Begin(), list.End(), entry) != list.End()};

    if (!listed) {
        list.PushBack(
            rapidjson::Value{a_ref.entry.data(), static_cast<rapidjson::SizeType>(a_ref.entry.size()), allocator},
            allocator);
    }
}

bool Parser::InGameEdit::DeleteRemoval(rapidjson::Document& a_config, const EntryRef& a_ref) {
    const auto shape{GetKeyShape(a_ref.key)};
    const auto entry{StringValue(a_ref.entry)};
    const bool light{IsLightPlugin(StringValue(a_ref.plugin))};
    auto* const remove{FindMemberValue(a_config, RemoveKey)};
    auto* const key{remove ? FindMemberValue(*remove, a_ref.key) : nullptr};
    bool deleted{false};

    if (!shape || !key) return false;

    if (IsFormIDShape(shape)) {
        if (auto* const formIDs{FindMemberValue(*key, a_ref.plugin)}) {
            deleted = EraseValues(*formIDs,
                                  [&](const rapidjson::Value& a_value) { return IsSameFormID(a_value, entry, light); });
        }
        EraseIfEmpty(*key, a_ref.plugin);
    } else {
        deleted = EraseValues(*key, [&](const rapidjson::Value& a_value) { return a_value == entry; });
    }

    EraseIfEmpty(*remove, a_ref.key);
    EraseIfEmpty(a_config, RemoveKey);

    if (deleted) {
        logger::info("{}: no longer removing {} {} {}", InGameConfigFileName, a_ref.key, a_ref.plugin, a_ref.entry);
    }

    return deleted;
}

std::optional<std::string> Parser::SaveNpcRule(const std::string_view a_name, const std::string_view a_plugin,
                                               const std::string_view a_formID, const std::string_view a_skin) {
    return EditInGameConfig([&](rapidjson::Document& a_config) -> std::optional<std::string> {
        const EntryRef ref{RulesKey, {}, std::string{a_name}};
        rapidjson::Document rule;
        auto& allocator{rule.GetAllocator()};

        const auto text = [&](const std::string_view a_text) {
            return rapidjson::Value{a_text.data(), static_cast<rapidjson::SizeType>(a_text.size()), allocator};
        };

        rapidjson::Value formIDs{rapidjson::kArrayType};
        formIDs.PushBack(text(a_formID), allocator);

        rapidjson::Value npcs{rapidjson::kObjectType};
        npcs.AddMember(text(a_plugin), formIDs, allocator);

        rapidjson::Value skins{rapidjson::kArrayType};
        skins.PushBack(text(a_skin), allocator);

        rule.SetObject();
        rule.AddMember("npcFormID", npcs, allocator);
        rule.AddMember("skins", skins, allocator);

        InGameEdit::SetEntryValue(a_config, ref, rule, true);

        InGameEdit::DeleteRemoval(a_config, ref);

        return std::nullopt;
    });
}

std::optional<std::string> Parser::RemoveRule(const std::string_view a_name) {
    bool setByOthers{false};

    if (auto error{IsRuleSetByOtherFiles(a_name, setByOthers)}) return error;

    return EditInGameConfig([&](rapidjson::Document& a_config) -> std::optional<std::string> {
        const EntryRef ref{RulesKey, {}, std::string{a_name}};

        const bool hadEntry{InGameEdit::DeleteEntry(a_config, ref)};

        // The other files' rule, the in-game file removes it as it's loaded
        if (setByOthers) {
            InGameEdit::AddRemoval(a_config, ref);
        }

        if (!hadEntry && !setByOthers) return "No config file has this rule";

        return std::nullopt;
    });
}

std::optional<std::string> Parser::SetMasterConfigValue(const char* a_key, const bool a_value) {
    logger::info("Setting {} to {} in {}", a_key, a_value, ConfigPath);

    return WriteMasterValue(a_key, [=](Allocator&) { return rapidjson::Value{a_value}; });
}

std::optional<std::string> Parser::SetMasterConfigValue(const char* a_key, const std::string_view a_value) {
    logger::info("Setting {} to \"{}\" in {}", a_key, a_value, ConfigPath);

    return WriteMasterValue(a_key, [=](Allocator& a_allocator) {
        return rapidjson::Value{a_value.data(), static_cast<rapidjson::SizeType>(a_value.size()), a_allocator};
    });
}

std::optional<std::string> Parser::SetMasterConfigNumber(const char* a_key, const std::int64_t a_value) {
    logger::info("Setting {} to {} in {}", a_key, a_value, ConfigPath);

    return WriteMasterValue(a_key, [=](Allocator&) { return rapidjson::Value{a_value}; });
}

std::optional<std::string> Parser::SaveMix(const std::string_view a_name, const MixParts& a_parts) {
    return EditInGameConfig([&](rapidjson::Document& a_config) -> std::optional<std::string> {
        auto& allocator{a_config.GetAllocator()};
        auto& mixes{GetOrAddObject(a_config, "skinMixes", allocator)};
        rapidjson::Value mix{rapidjson::kObjectType};

        const auto text = [&](const std::string_view a_text) {
            return rapidjson::Value{a_text.data(), static_cast<rapidjson::SizeType>(a_text.size()), allocator};
        };

        for (const auto& [field, pack] : {std::pair{"base", &a_parts.base},
                                          {"diffuse", &a_parts.diffuse},
                                          {"normal", &a_parts.normal},
                                          {"subsurface", &a_parts.subsurface},
                                          {"specular", &a_parts.specular},
                                          {"head", &a_parts.head},
                                          {"headNormal", &a_parts.headNormal},
                                          {"teeth", &a_parts.teeth}}) {
            if (!pack->empty()) {
                mix.AddMember(rapidjson::StringRef(field), text(*pack), allocator);
            }
        }

        logger::info("{}: setting skinMixes {}", InGameConfigFileName, a_name);

        if (const auto found{mixes.FindMember(StringValue(a_name))}; found != mixes.MemberEnd()) {
            found->value = mix;
        } else {
            mixes.AddMember(text(a_name), mix, allocator);
        }

        InGameEdit::DeleteRemoval(a_config, EntryRef{"skinMixes", {}, std::string{a_name}});

        return std::nullopt;
    });
}
