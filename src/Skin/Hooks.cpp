#include "Skin/Hooks.h"

#include "Skin/Distribution.h"

namespace {
    template <class T>
    struct Load3D {
        static RE::NiAVObject* thunk(T* a_this, const bool a_backgroundLoading) {
            auto* const root{func(a_this, a_backgroundLoading)};

            Skin::Distribution::ApplyForActor(a_this, root, a_backgroundLoading ? "Load3D (background)" : "Load3D");

            return root;
        }

        static inline REL::Relocation<decltype(thunk)> func;
    };

    struct FixSkinInstances {
        static void thunk(RE::BSFaceGenNiNode* a_this, RE::NiNode* a_skeleton, const bool a_arg2) {
            func(a_this, a_skeleton, a_arg2);

            auto* const ref{a_skeleton ? a_this->GetUserData() : nullptr};

            Skin::Distribution::ApplyForActor(ref ? ref->As<RE::Actor>() : nullptr, a_this, "FaceGen");
        }

        static inline REL::Relocation<decltype(thunk)> func;
    };

    struct AttachBipedObject {
        static RE::NiAVObject* thunk(RE::BipedAnim* a_this, RE::NiNode* a_skeleton, RE::BSFadeNode* a_root,
                                     const RE::BIPED_OBJECT a_slot, const std::uint64_t a5, const std::uint64_t a6,
                                     const std::uint64_t a7) {
            auto* const object{func(a_this, a_skeleton, a_root, a_slot, a5, a6, a7)};
            const auto ref{object && a_this ? a_this->actorRef.get() : RE::NiPointer<RE::TESObjectREFR>{}};

            Skin::Distribution::ApplyForActor(ref ? ref->As<RE::Actor>() : nullptr, object,
                                              std::format("Attach slot {}", std::to_underlying(a_slot)));

            return object;
        }

        static inline REL::Relocation<decltype(thunk)> func;
    };

    // Actor::DoReset3D (QueueNiNodeUpdate from scripts) loads the skin's texture set again without attaching the
    // body, and SKSE sends this event right after it on the same thread
    class NiNodeUpdateSink final : public RE::BSTEventSink<SKSE::NiNodeUpdateEvent> {
    public:
        RE::BSEventNotifyControl ProcessEvent(const SKSE::NiNodeUpdateEvent* a_event,
                                              RE::BSTEventSource<SKSE::NiNodeUpdateEvent>*) override {
            auto* const actor{a_event && a_event->reference ? a_event->reference->As<RE::Actor>() : nullptr};

            if (!actor) return RE::BSEventNotifyControl::kContinue;

            Skin::Distribution::ApplyForActor(actor, actor->Get3D(false), "NiNodeUpdate");

            if (actor->IsPlayerRef()) {
                Skin::Distribution::ApplyForActor(actor, actor->Get3D(true), "NiNodeUpdate (first person)");
            }

            return RE::BSEventNotifyControl::kContinue;
        }
    };

    // Only plain prologue instructions, none of them reads relative to the instruction pointer
    std::size_t InstructionLength(const std::uint8_t* a_code) {
        const auto op{a_code[0]};

        if (op >= 0x50 && op <= 0x57) return 1;

        if ((op == 0x40 || op == 0x41) && a_code[1] >= 0x50 && a_code[1] <= 0x57) return 2;

        if (op != 0x48 && op != 0x4C) return 0;

        const auto opcode{a_code[1]};
        const auto modrm{a_code[2]};
        const auto mod{modrm >> 6};
        const auto rm{modrm & 7};

        if ((opcode == 0x8B || opcode == 0x89) && mod == 3) return 3;

        if (opcode == 0x83 && modrm == 0xEC) return 4;

        if (opcode == 0x81 && modrm == 0xEC) return 7;

        if (opcode != 0x89 && opcode != 0x8D) return 0;

        if (mod == 1 && rm != 4) return 4;

        if (rm != 4 || a_code[3] != 0x24) return 0;

        if (mod == 1) return 5;

        if (mod == 2) return 8;

        return 0;
    }

    std::size_t PrologueLength(const std::uint8_t* a_code) {
        std::size_t length{0};

        while (length < 5) {
            const auto instruction{InstructionLength(a_code + length)};

            if (instruction == 0) return 0;

            length += instruction;
        }

        return length;
    }

    // jmp qword ptr [rip], then the address, 14 bytes
    void WriteAbsoluteJump(std::uint8_t* a_at, const std::uintptr_t a_to) {
        a_at[0] = 0xFF;
        a_at[1] = 0x25;
        std::memset(a_at + 2, 0, 4);
        std::memcpy(a_at + 6, &a_to, sizeof(a_to));
    }

    // Adapted from powerof3's Papyrus Extender (hook_function_prologue), written without Xbyak
    template <class T>
    bool HookPrologue(const std::uintptr_t a_target, const std::string_view a_name) {
        const auto* const code{reinterpret_cast<const std::uint8_t*>(a_target)};
        std::string bytes;

        for (std::size_t i = 0; i < 16; ++i) {
            bytes += std::format("{:02X} ", code[i]);
        }

        auto& trampoline{SKSE::GetTrampoline()};

        // Another mod's hook already starts with a jump, ours goes in front of it
        if (code[0] == 0xE9) {
            std::int32_t displacement;
            std::memcpy(&displacement, code + 1, sizeof(displacement));
            const std::uintptr_t next{a_target + 5 + static_cast<std::intptr_t>(displacement)};
            auto* const original{static_cast<std::uint8_t*>(trampoline.allocate(14))};

            WriteAbsoluteJump(original, next);
            trampoline.write_branch<5>(a_target, T::thunk, true);
            T::func = reinterpret_cast<std::uintptr_t>(original);

            logger::info("Hooked {} in front of another mod's hook ({})", a_name, bytes);

            return true;
        }

        const auto length{PrologueLength(code)};

        if (length == 0) {
            logger::error("Didn't hook {}, its first bytes aren't known as safe to move: {}", a_name, bytes);

            return false;
        }

        auto* const original{static_cast<std::uint8_t*>(trampoline.allocate(length + 14))};

        std::memcpy(original, code, length);
        WriteAbsoluteJump(original + length, a_target + length);
        trampoline.write_branch<5>(a_target, T::thunk, true);
        T::func = reinterpret_cast<std::uintptr_t>(original);

        logger::info("Hooked {} ({} bytes moved: {})", a_name, length, bytes);

        return true;
    }
}  // namespace

void Skin::Hooks::Install() {
    REL::Relocation<std::uintptr_t> characterVtable{RE::VTABLE_Character[0]};

    Load3D<RE::Character>::func = characterVtable.write_vfunc(0x6A, Load3D<RE::Character>::thunk);
    logger::info("Hooked Character::Load3D");

    REL::Relocation<std::uintptr_t> playerVtable{RE::VTABLE_PlayerCharacter[0]};

    Load3D<RE::PlayerCharacter>::func = playerVtable.write_vfunc(0x6A, Load3D<RE::PlayerCharacter>::thunk);
    logger::info("Hooked PlayerCharacter::Load3D");

    REL::Relocation<std::uintptr_t> faceGenVtable{RE::VTABLE_BSFaceGenNiNode[0]};

    FixSkinInstances::func = faceGenVtable.write_vfunc(REL::Relocate(0x3E, 0x3E, 0x3F), FixSkinInstances::thunk);
    logger::info("Hooked BSFaceGenNiNode::FixSkinInstances");

    const REL::Relocation<std::uintptr_t> attach{REL::VariantID(15535, 15712, 0x1DB9E0)};

    HookPrologue<AttachBipedObject>(attach.address(), "the biped object attach");

    static NiNodeUpdateSink niNodeUpdateSink;

    if (auto* const source{SKSE::GetNiNodeUpdateEventSource()}) {
        source->AddEventSink(&niNodeUpdateSink);
        logger::info("Listening to SKSE NiNodeUpdate events");
    }
}
