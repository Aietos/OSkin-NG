#pragma once

namespace Preview::Physics {
    // An actor moved while time is frozen leaves HDT-SMP's bones behind and stretches the meshes
    // This rebuilds the physics, even while the game is frozen
    void Reset();
}  // namespace Preview::Physics
