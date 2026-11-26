#include "Skin/Archives.h"

#include "STL.h"

namespace {
    constexpr std::string_view dataFolder{"Data\\"};
    constexpr std::string_view packsPrefix{"textures\\oskin\\"};

    // Skyrim LE archives are version 104, Skyrim SE archives 105. Only the folder records differ
    constexpr std::uint32_t versionLE{104};
    constexpr std::uint32_t versionSE{105};
    constexpr std::size_t folderRecordLE{16};
    constexpr std::size_t folderRecordSE{24};
    constexpr std::size_t folderFileCountOffset{8};
    constexpr std::size_t fileRecordSize{16};

    constexpr std::uint32_t folderNamesFlag{0x1};
    constexpr std::uint32_t fileNamesFlag{0x2};

    struct Header {
        char tag[4];
        std::uint32_t version;
        std::uint32_t folderOffset;
        std::uint32_t flags;
        std::uint32_t folderCount;
        std::uint32_t fileCount;
        std::uint32_t folderNamesLength;
        std::uint32_t fileNamesLength;
        std::uint16_t fileFlags;
        std::uint16_t pad;
    };
    static_assert(sizeof(Header) == 36);

    struct Folder {
        std::string name;
        std::uint32_t files{0};
    };

    std::string Normalize(std::string_view a_path) {
        std::string path;
        path.reserve(a_path.size());

        for (const char c : a_path) {
            if (c == '\0') break;

            const char normalized{c == '/' ? '\\' : stl::to_lower(c)};

            if (normalized == '\\' && (path.empty() || path.back() == '\\')) continue;

            path.push_back(normalized);
        }

        return path;
    }

    std::string_view Trim(std::string_view a_text) {
        constexpr std::string_view spaces{" \t\r\n"};

        const auto first{a_text.find_first_not_of(spaces)};

        if (first == std::string_view::npos) return {};

        return a_text.substr(first, a_text.find_last_not_of(spaces) - first + 1);
    }

    // The archives in the order the game mounts them: the ini lists, then each plugin's own
    std::vector<std::string> MountedArchives() {
        std::vector<std::string> archives;

        const auto add = [&](const std::string_view a_name) {
            const auto name{Trim(a_name)};

            if (name.empty()) return;

            if (std::ranges::any_of(archives, [=](const std::string& a_archive) { return stl::cmp(a_archive, name); })) {
                return;
            }

            archives.emplace_back(name);
        };

        if (auto* const ini{RE::INISettingCollection::GetSingleton()}) {
            for (const auto key : {"sResourceArchiveList:Archive"sv, "sResourceArchiveList2:Archive"sv}) {
                const auto* const setting{ini->GetSetting(key)};

                if (!setting || setting->GetType() != RE::Setting::Type::kString || !setting->GetString()) continue;

                for (const auto name : std::string_view{setting->GetString()} | std::views::split(',')) {
                    add(std::string_view{name.begin(), name.end()});
                }
            }
        }

        if (auto* const data{RE::TESDataHandler::GetSingleton()}) {
            const auto addPlugin = [&](const RE::TESFile* a_file) {
                if (!a_file) return;

                const auto fileName{a_file->GetFilename()};
                const auto stem{fileName.substr(0, fileName.rfind('.'))};

                add(std::format("{}.bsa", stem));
                add(std::format("{} - Textures.bsa", stem));
            };

            for (std::uint8_t i = 0; i < data->GetLoadedModCount(); ++i) addPlugin(data->LookupLoadedModByIndex(i));

            for (std::uint16_t i = 0; i < data->GetLoadedLightModCount(); ++i) {
                addPlugin(data->LookupLoadedLightModByIndex(i));
            }
        }

        return archives;
    }

    template <class T>
    bool Read(std::istream& a_stream, T& a_value) {
        return static_cast<bool>(a_stream.read(reinterpret_cast<char*>(&a_value), sizeof(T)));
    }

    // The pack and the path inside it of a folder under textures\oskin, or no pack for any other folder
    std::pair<std::string_view, std::string_view> SplitPackFolder(const std::string_view a_folder) {
        if (!a_folder.starts_with(packsPrefix)) return {};

        const auto rest{a_folder.substr(packsPrefix.size())};
        const auto slash{rest.find('\\')};

        if (slash == std::string_view::npos) return {rest, {}};

        return {rest.substr(0, slash), rest.substr(slash + 1)};
    }

    // Adds the .dds files under textures\oskin in one archive and returns how many there were
    std::size_t ReadArchive(const fs::path& a_path, const std::string& a_name, Skin::Archives::PackFiles& a_packs) {
        std::ifstream file{a_path, std::ios::binary};

        if (!file) {
            logger::warn("Couldn't open the archive {}", a_name);
            return 0;
        }

        Header header{};

        if (!Read(file, header) || std::string_view{header.tag, 4} != "BSA\0"sv) {
            logger::warn("The archive {} isn't a Skyrim archive", a_name);
            return 0;
        }

        if (header.version != versionLE && header.version != versionSE) {
            logger::warn("The archive {} is version {}, only {} and {} can be read", a_name, header.version,
                         versionLE, versionSE);
            return 0;
        }

        if ((header.flags & folderNamesFlag) == 0 || (header.flags & fileNamesFlag) == 0) {
            logger::warn("The archive {} doesn't keep its file names, so its skin packs can't be found", a_name);
            return 0;
        }

        const std::size_t recordSize{header.version == versionSE ? folderRecordSE : folderRecordLE};
        std::vector<char> records(recordSize * header.folderCount);

        file.seekg(header.folderOffset);

        if (!file.read(records.data(), static_cast<std::streamsize>(records.size()))) {
            logger::warn("The archive {} ends before its folder list", a_name);
            return 0;
        }

        // Each folder's name and file records follow the folder list, in the same order
        std::vector<Folder> folders(header.folderCount);
        bool anyPack{false};

        for (std::size_t i = 0; i < folders.size(); ++i) {
            std::memcpy(&folders[i].files, records.data() + i * recordSize + folderFileCountOffset,
                        sizeof(std::uint32_t));

            std::uint8_t length{0};

            if (!Read(file, length)) {
                logger::warn("The archive {} ends inside its folder names", a_name);
                return 0;
            }

            std::string name(length, '\0');

            if (!file.read(name.data(), length)) {
                logger::warn("The archive {} ends inside its folder names", a_name);
                return 0;
            }

            folders[i].name = Normalize(name);
            anyPack = anyPack || !SplitPackFolder(folders[i].name).first.empty();

            file.seekg(static_cast<std::streamoff>(fileRecordSize) * folders[i].files, std::ios::cur);
        }

        if (!anyPack) return 0;

        std::string names(header.fileNamesLength, '\0');

        if (!file.read(names.data(), static_cast<std::streamsize>(names.size()))) {
            logger::warn("The archive {} ends inside its file names", a_name);
            return 0;
        }

        // The file names are in the same order as the folders' files
        std::size_t position{0};
        std::size_t found{0};

        for (const auto& folder : folders) {
            const auto [pack, inside] = SplitPackFolder(folder.name);

            for (std::uint32_t i = 0; i < folder.files; ++i) {
                if (position >= names.size()) {
                    logger::warn("The archive {} has fewer file names than files", a_name);
                    return found;
                }

                const auto end{names.find('\0', position)};
                const std::string_view fileName{names.data() + position,
                                                (end == std::string::npos ? names.size() : end) - position};

                position = end == std::string::npos ? names.size() : end + 1;

                if (pack.empty()) continue;

                auto relative{Normalize(inside.empty() ? fileName : std::format("{}\\{}", inside, fileName))};

                if (!relative.ends_with(".dds")) continue;

                a_packs[std::string{pack}].push_back(std::move(relative));
                ++found;
            }
        }

        return found;
    }
}  // namespace

Skin::Archives::PackFiles Skin::Archives::ListPackFiles() {
    PackFiles packs;

    for (const auto& name : MountedArchives()) {
        std::error_code error;
        const fs::path path{std::format("{}{}", dataFolder, name)};

        if (!fs::is_regular_file(path, error)) continue;

        if (const auto found{ReadArchive(path, name, packs)}; found > 0) {
            logger::info("The archive {} has {} skin pack textures", name, found);
        }
    }

    return packs;
}
