#pragma once

namespace Skin::Archives {
    // Pack folder name -> its .dds files, relative to the pack folder. Everything is lowercase with backslashes, the
    // way archives store names
    using PackFiles = std::map<std::string, std::vector<std::string>>;

    // The files under textures\OSkin in the archives the game mounts, from the ini archive lists and each loaded
    // plugin's .bsa and " - Textures.bsa". Only the names are read, so compressed and uncompressed archives are the same
    PackFiles ListPackFiles();
}  // namespace Skin::Archives
