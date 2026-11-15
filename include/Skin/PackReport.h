#pragma once

#include "Skin/Packs.h"

namespace Skin::PackReport {
    enum Part : std::size_t { kBody, kHands, kHead, kVampireHead, kGenitals, kTeeth, kPartTotal };
    enum Channel : std::size_t { kDiffuse, kNormal, kSubsurface, kSpecular, kChannelTotal };

    struct Cell {
        bool allRaces{false};
        std::vector<std::string> races;

        [[nodiscard]] bool Has() const { return allRaces || !races.empty(); }
    };

    struct Side {
        std::array<std::array<Cell, kChannelTotal>, kPartTotal> cells{};
        std::vector<std::string> beastRaces;
        std::map<std::string, std::array<bool, kPartTotal>> specials;
        std::vector<std::string> faceDetails;
        bool standard{false};
        bool any{false};
    };

    struct Report {
        Side female;
        Side male;
        std::size_t otherFiles{0};
    };

    const Report& Get(const Packs::Pack& a_pack);

    std::string RaceName(std::string_view a_folder);
}  // namespace Skin::PackReport
