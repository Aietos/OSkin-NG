#include "Skin/Packs.h"

#include "STL.h"

namespace {
    constexpr std::string_view packsFolder{"Data\\Textures\\OSkin"};
    constexpr std::string_view characterPrefix{"actors\\character\\"};
    constexpr std::string_view packsPrefix{"oskin\\"};

    // Human, elf and orc races share the body mesh, beast races don't
    constexpr std::array<std::pair<std::string_view, std::string_view>, 18> sharedRaceFolders{{
        {"bretonfemale", "female"},
        {"darkelffemale", "female"},
        {"highelffemale", "female"},
        {"imperialfemale", "female"},
        {"nordfemale", "female"},
        {"redguardfemale", "female"},
        {"woodelffemale", "female"},
        {"orcfemale", "female"},
        {"femaleorc", "female"},
        {"bretonmale", "male"},
        {"darkelfmale", "male"},
        {"highelfmale", "male"},
        {"imperialmale", "male"},
        {"nordmale", "male"},
        {"redguardmale", "male"},
        {"woodelfmale", "male"},
        {"orcmale", "male"},
        {"maleorc", "male"},
    }};

    std::vector<Skin::Packs::Pack> g_packs;

    std::string Normalize(std::string_view a_path) {
        std::string path;
        path.reserve(a_path.size());

        for (const char c : a_path) {
            const char normalized{c == '/' ? '\\' : stl::to_lower(c)};

            if (normalized == '\\' && (path.empty() || path.back() == '\\')) continue;

            path.push_back(normalized);
        }

        return path;
    }

    void MarkSex(Skin::Packs::Pack& a_pack, const std::string_view a_key) {
        const auto slash{a_key.find('\\')};
        const auto folder{slash == std::string_view::npos ? std::string_view{} : a_key.substr(0, slash)};
        const auto file{slash == std::string_view::npos ? a_key : a_key.substr(a_key.rfind('\\') + 1)};

        // Women's heads use male\blankdetailmap.dds too, so female skins ship it
        if (file == "blankdetailmap.dds") return;

        if (folder.ends_with("female") || folder.starts_with("female")) {
            a_pack.female = true;
        } else if (folder.ends_with("male") || folder.starts_with("male") || folder == "sos") {
            a_pack.male = true;
        } else if (file.contains("female")) {
            a_pack.female = true;
        } else if (file.contains("male")) {
            a_pack.male = true;
        }
    }

    std::string ToUtf8(const fs::path& a_path) {
        const auto text{a_path.u8string()};

        return {reinterpret_cast<const char*>(text.data()), text.size()};
    }

    // A pack can keep a mod's own layout, so everything up to a textures folder and then actors\character is
    // dropped. "00 Core\textures\actors\character\female\x.dds" keys as "female\x.dds"
    std::string_view StripModLayout(std::string_view a_key) {
        constexpr std::string_view texturesFolder{"textures\\"};

        for (std::size_t start = 0; start < a_key.size();) {
            if (a_key.substr(start).starts_with(texturesFolder)) {
                a_key.remove_prefix(start + texturesFolder.size());
                break;
            }

            const auto slash{a_key.find('\\', start)};

            if (slash == std::string_view::npos) break;

            start = slash + 1;
        }

        if (a_key.starts_with(characterPrefix)) {
            a_key.remove_prefix(characterPrefix.size());
        }

        return a_key;
    }
}  // namespace

namespace {
    std::string_view TexturesRelative(const std::string& a_path) {
        std::string_view key{a_path};

        if (key.starts_with('\\')) key.remove_prefix(1);

        if (key.starts_with("data\\")) key.remove_prefix(5);

        if (key.starts_with("textures\\")) key.remove_prefix(9);

        return key;
    }

    // A pack can also be a JSON file in oskin_packs that points at a folder another mod installs, so mod managers
    // and collections can use a skin mod's files where they are. "folder" is under Data\Textures. "keyFolder" is
    // the folder its files count as (female, male, bretonfemale...); without it the layout is read like a pack
    // folder. "keys" gives single files another name, or skips them with an empty name.
    constexpr std::string_view linksFolder{"Data\\SKSE\\Plugins\\OSkin\\oskin_packs"};

    struct Link {
        std::string folder;
        std::string keyFolder;
        std::unordered_map<std::string, std::string> keys;
    };

    // Linked packs' files are outside textures\OSkin, so their paths are kept to know them after a swap
    std::unordered_map<std::string, std::string> g_linkedKeys;

    std::optional<Link> ReadLink(const fs::path& a_path, const std::string& a_name) {
        stl::FilePtrManager file{a_path.c_str()};

        if (file.error() != 0) {
            logger::warn("Couldn't open the linked skin pack {}", a_name);

            return std::nullopt;
        }

        char buffer[4096];
        rapidjson::FileReadStream stream(file.get(), buffer, std::size(buffer));
        rapidjson::Document document;

        if (document.ParseStream(stream).HasParseError() || !document.IsObject()) {
            logger::warn("The linked skin pack {} isn't a valid JSON object", a_name);

            return std::nullopt;
        }

        const auto folder{document.FindMember("folder")};

        if (folder == document.MemberEnd() || !folder->value.IsString()) {
            logger::warn("The linked skin pack {} has no \"folder\"", a_name);

            return std::nullopt;
        }

        Link link;
        link.folder = std::format("textures\\{}", TexturesRelative(Normalize(folder->value.GetString())));

        while (link.folder.ends_with('\\')) link.folder.pop_back();

        if (const auto keyFolder{document.FindMember("keyFolder")};
            keyFolder != document.MemberEnd() && keyFolder->value.IsString()) {
            link.keyFolder = Normalize(keyFolder->value.GetString());

            while (link.keyFolder.ends_with('\\')) link.keyFolder.pop_back();
        }

        if (const auto keys{document.FindMember("keys")}; keys != document.MemberEnd() && keys->value.IsObject()) {
            for (auto member{keys->value.MemberBegin()}; member != keys->value.MemberEnd(); ++member) {
                if (member->value.IsString()) {
                    link.keys.emplace(Normalize(member->name.GetString()), Normalize(member->value.GetString()));
                }
            }
        }

        return link;
    }

    void AddPack(Skin::Packs::Pack&& a_pack, const fs::path& a_folder, const std::string& a_textureFolder,
                 const Link* a_link) {
        std::error_code error;

        // Sorted, so when two files give the same key the first in alphabetical order wins every time
        std::vector<std::string> files;

        for (const auto& file : fs::recursive_directory_iterator(a_folder, error)) {
            if (!file.is_regular_file(error)) continue;

            auto relative{Normalize(ToUtf8(file.path().lexically_relative(a_folder)))};

            if (relative.ends_with(".dds")) {
                files.push_back(std::move(relative));
            }
        }

        if (error) {
            logger::error("Couldn't read all of {}: {}", ToUtf8(a_folder), error.message());
        }

        std::ranges::sort(files);

        constexpr std::size_t duplicatesLogged{5};
        std::size_t duplicates{0};

        for (const auto& relative : files) {
            std::string key{StripModLayout(relative)};

            if (a_link) {
                if (const auto renamed{a_link->keys.find(relative)}; renamed != a_link->keys.end()) {
                    key = renamed->second;
                } else if (!a_link->keyFolder.empty()) {
                    key = std::format("{}\\{}", a_link->keyFolder, relative);
                }
            }

            if (key.empty()) continue;

            if (const auto existing{a_pack.textures.find(key)}; existing != a_pack.textures.end()) {
                if (++duplicates <= duplicatesLogged) {
                    logger::warn("The skin pack {} has two files for {}, {} is used and {} isn't", a_pack.name, key,
                                 existing->second, relative);
                }

                continue;
            }

            MarkSex(a_pack, key);

            if (key.starts_with("!ube\\")) {
                a_pack.ube = true;
            }

            auto path{std::format("{}\\{}", a_textureFolder, relative)};

            if (a_link) {
                g_linkedKeys.emplace(TexturesRelative(path), key);
            }

            a_pack.textures.emplace(std::move(key), std::move(path));
        }

        a_pack.duplicateFiles = duplicates;

        if (duplicates > duplicatesLogged) {
            logger::warn(
                "The skin pack {} has {} files that another of its files already gives, only the first {} are "
                "listed above",
                a_pack.name, duplicates, duplicatesLogged);
        }

        if (a_pack.textures.empty()) {
            logger::warn("The skin pack {} has no .dds files", a_pack.name);
            return;
        }

        logger::info("Found the skin pack {} with {} textures ({}){}", a_pack.name, a_pack.textures.size(),
                     a_pack.female && a_pack.male ? "female and male"
                     : a_pack.female              ? "female"
                     : a_pack.male                ? "male"
                                                  : "no female or male files",
                     a_link ? std::format(", linked to {}", a_link->folder) : "");
        g_packs.push_back(std::move(a_pack));
    }
}  // namespace

void Skin::Packs::Scan() {
    g_packs.clear();
    g_linkedKeys.clear();

    std::error_code error;

    if (const fs::path root{packsFolder}; fs::is_directory(root, error)) {
        for (const auto& folder : fs::directory_iterator(root, error)) {
            if (!folder.is_directory(error)) continue;

            Pack pack;
            pack.name = ToUtf8(folder.path().filename());
            const auto textureFolder{std::format("textures\\OSkin\\{}", pack.name)};

            AddPack(std::move(pack), folder.path(), textureFolder, nullptr);
        }

        if (error) {
            logger::error("Couldn't read all of {}: {}", packsFolder, error.message());
        }
    } else {
        logger::info("No skin pack folders, {} doesn't exist", packsFolder);
    }

    error.clear();

    if (const fs::path links{linksFolder}; fs::is_directory(links, error)) {
        for (const auto& entry : fs::directory_iterator(links, error)) {
            if (!entry.is_regular_file(error) || entry.path().extension() != ".json") continue;

            const auto name{ToUtf8(entry.path().stem())};

            if (FindPack(name)) {
                logger::warn("The linked skin pack {} has the name of a pack folder, it's skipped", name);
                continue;
            }

            const auto link{ReadLink(entry.path(), name)};

            if (!link) continue;

            const fs::path folder{fs::path{"Data"} / fs::path{link->folder}};

            if (!fs::is_directory(folder, error)) {
                logger::info("The linked skin pack {} points to {}, which isn't installed", name, link->folder);
                error.clear();
                continue;
            }

            Pack pack;
            pack.name = name;

            AddPack(std::move(pack), folder, link->folder, &*link);
        }
    }

    std::ranges::sort(g_packs, {}, &Pack::name);
}

const std::vector<Skin::Packs::Pack>& Skin::Packs::GetAll() { return g_packs; }

const Skin::Packs::Pack* Skin::Packs::FindPack(const std::string_view a_name) {
    const auto pack{std::ranges::find_if(g_packs, [=](const Pack& a_pack) { return stl::cmp(a_pack.name, a_name); })};

    return pack == g_packs.end() ? nullptr : &*pack;
}

bool Skin::Packs::IsPackPath(const std::string_view a_texturePath) {
    const auto path{Normalize(a_texturePath)};
    const auto relative{TexturesRelative(path)};

    return relative.starts_with(packsPrefix) || g_linkedKeys.contains(std::string{relative});
}

std::string Skin::Packs::ToPackKey(const std::string_view a_texturePath) {
    const auto path{Normalize(a_texturePath)};
    auto key{TexturesRelative(path)};

    if (const auto linked{g_linkedKeys.find(std::string{key})}; linked != g_linkedKeys.end()) {
        return linked->second;
    }

    if (key.starts_with(characterPrefix)) {
        key.remove_prefix(characterPrefix.size());
    } else if (key.starts_with(packsPrefix)) {
        // A texture that was already swapped, so it keys the same as the original
        key.remove_prefix(packsPrefix.size());
        const auto packEnd{key.find('\\')};

        key = packEnd == std::string_view::npos ? std::string_view{} : StripModLayout(key.substr(packEnd + 1));
    }

    return std::string{key};
}

bool Skin::Packs::IsSharedRaceFolder(const std::string_view a_folder) {
    return std::ranges::any_of(sharedRaceFolders, [=](const auto& a_race) { return a_race.first == a_folder; });
}

const std::string* Skin::Packs::Find(const Pack& a_pack, const std::string& a_key) {
    if (const auto texture{a_pack.textures.find(a_key)}; texture != a_pack.textures.end()) return &texture->second;

    const auto slash{a_key.find('\\')};

    if (slash == std::string::npos) return nullptr;

    const std::string_view folder{a_key.data(), slash};
    const std::string_view file{std::string_view{a_key}.substr(slash + 1)};

    // Each race's head is its own mesh, so a head normal map only fits that race
    if (file.contains("head") && file.ends_with("_msn.dds")) return nullptr;

    for (const auto& [race, shared] : sharedRaceFolders) {
        if (folder != race) continue;

        const auto texture{a_pack.textures.find(std::format("{}\\{}", shared, file))};

        return texture == a_pack.textures.end() ? nullptr : &texture->second;
    }

    return nullptr;
}
