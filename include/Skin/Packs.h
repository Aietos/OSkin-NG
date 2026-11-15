#pragma once

namespace Skin::Packs {
    struct Pack {
        std::string name;
        // These strings live as long as the game
        std::unordered_map<std::string, std::string> textures;
        bool female{false};
        bool male{false};
        bool ube{false};
        std::size_t duplicateFiles{0};
    };

    void Scan();

    const std::vector<Pack>& GetAll();

    const Pack* FindPack(std::string_view a_name);

    std::string ToPackKey(std::string_view a_texturePath);

    bool IsPackPath(std::string_view a_texturePath);

    bool IsSharedRaceFolder(std::string_view a_folder);

    const std::string* Find(const Pack& a_pack, const std::string& a_key);
}  // namespace Skin::Packs
