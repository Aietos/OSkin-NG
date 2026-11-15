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

void Skin::Packs::Scan() {
    g_packs.clear();

    std::error_code error;
    const fs::path root{packsFolder};

    if (!fs::is_directory(root, error)) {
        logger::info("No skin packs, {} doesn't exist", packsFolder);

        return;
    }

    for (const auto& folder : fs::directory_iterator(root, error)) {
        if (!folder.is_directory(error)) continue;

        Pack pack;
        pack.name = ToUtf8(folder.path().filename());

        // Sorted, so when two files give the same key the first in alphabetical order wins every time
        std::vector<std::string> files;

        for (const auto& file : fs::recursive_directory_iterator(folder.path(), error)) {
            if (!file.is_regular_file(error)) continue;

            auto relative{Normalize(ToUtf8(file.path().lexically_relative(folder.path())))};

            if (relative.ends_with(".dds")) {
                files.push_back(std::move(relative));
            }
        }

        std::ranges::sort(files);

        constexpr std::size_t duplicatesLogged{5};
        std::size_t duplicates{0};

        for (const auto& relative : files) {
            std::string key{StripModLayout(relative)};

            if (key.empty()) continue;

            if (const auto existing{pack.textures.find(key)}; existing != pack.textures.end()) {
                if (++duplicates <= duplicatesLogged) {
                    logger::warn("The skin pack {} has two files for {}, {} is used and {} isn't", pack.name, key,
                                 existing->second, relative);
                }

                continue;
            }

            MarkSex(pack, key);

            if (key.starts_with("!ube\\")) {
                pack.ube = true;
            }

            pack.textures.emplace(std::move(key), std::format("textures\\OSkin\\{}\\{}", pack.name, relative));
        }

        pack.duplicateFiles = duplicates;

        if (duplicates > duplicatesLogged) {
            logger::warn(
                "The skin pack {} has {} files that another of its files already gives, only the first {} are "
                "listed above",
                pack.name, duplicates, duplicatesLogged);
        }

        if (pack.textures.empty()) {
            logger::warn("The skin pack {} has no .dds files", pack.name);
            continue;
        }

        logger::info("Found the skin pack {} with {} textures ({})", pack.name, pack.textures.size(),
                     pack.female && pack.male ? "female and male"
                     : pack.female            ? "female"
                     : pack.male              ? "male"
                                              : "no female or male files");
        g_packs.push_back(std::move(pack));
    }

    if (error) {
        logger::error("Couldn't read all of {}: {}", packsFolder, error.message());
    }

    std::ranges::sort(g_packs, {}, &Pack::name);
}

const std::vector<Skin::Packs::Pack>& Skin::Packs::GetAll() { return g_packs; }

const Skin::Packs::Pack* Skin::Packs::FindPack(const std::string_view a_name) {
    const auto pack{std::ranges::find_if(g_packs, [=](const Pack& a_pack) { return stl::cmp(a_pack.name, a_name); })};

    return pack == g_packs.end() ? nullptr : &*pack;
}

namespace {
    std::string_view TexturesRelative(const std::string& a_path) {
        std::string_view key{a_path};

        if (key.starts_with('\\')) key.remove_prefix(1);

        if (key.starts_with("data\\")) key.remove_prefix(5);

        if (key.starts_with("textures\\")) key.remove_prefix(9);

        return key;
    }
}  // namespace

bool Skin::Packs::IsPackPath(const std::string_view a_texturePath) {
    const auto path{Normalize(a_texturePath)};

    return TexturesRelative(path).starts_with(packsPrefix);
}

std::string Skin::Packs::ToPackKey(const std::string_view a_texturePath) {
    const auto path{Normalize(a_texturePath)};
    auto key{TexturesRelative(path)};

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
