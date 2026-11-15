#include "API/Links.h"

#include "Config/Config.h"
#include "Preview/Preview.h"
#include "SKEE.h"
#include "STL.h"
#include "Skin/Distribution.h"
#include "UI/UI.h"

using Actor = RE::Actor;
using TESForm = RE::TESForm;
#include "API/OBodyAPI.h"

#include "API/OSkinAPI.h"

namespace {
    // OBody's interface, and whether it's safe to use now. OBody tells when that changes, like while saving
    ::OBody::API::IPluginInterface* g_obody{nullptr};
    std::atomic_bool g_obodyReady{false};

    bool RulesUsePresets() { return Parser::JSONParser::GetInstance().GetConfig()->UsesPresets(); }

    // Applied again in the task phase, since the preset may change what the rules give
    void ApplyAgain(RE::Actor* a_actor) {
        if (!a_actor || !RulesUsePresets()) return;

        SKSE::GetTaskInterface()->AddTask([handle{a_actor->GetHandle()}] {
            if (const auto actor{handle.get()}; actor && actor->Is3DLoaded()) {
                Skin::Distribution::ApplyCurrent(actor.get());
            }
        });
    }

    class ReadinessListener final : public ::OBody::API::IOBodyReadinessEventListener {
    public:
        void OBodyIsReady() override {
            g_obodyReady = true;

            // The skins applied while OBody wasn't ready had no presets to check
            if (RulesUsePresets()) {
                SKSE::GetTaskInterface()->AddTask([] { Skin::Distribution::ApplyToLoadedActors("OBody is ready"); });
            }
        }

        void OBodyIsNoLongerReady() override { g_obodyReady = false; }
    };

    ReadinessListener g_readinessListener;

    class PresetListener final : public ::OBody::API::IActorChangeEventListener {
    public:
        OnActorGenerated::Response OnActorGenerated(RE::Actor* a_actor, OnActorGenerated::Flags,
                                                    OnActorGenerated::Payload&) override {
            ApplyAgain(a_actor);

            return OnActorGenerated::Response::None;
        }

        OnActorPresetChangedWithoutGeneration::Response OnActorPresetChangedWithoutGeneration(
            RE::Actor* a_actor, OnActorPresetChangedWithoutGeneration::Flags,
            OnActorPresetChangedWithoutGeneration::Payload&) override {
            ApplyAgain(a_actor);

            return OnActorPresetChangedWithoutGeneration::Response::None;
        }
    };

    PresetListener g_presetListener;

    SKEE::IBodyMorphInterface* g_morphs{nullptr};

    class Interface final : public ::OSkin::API::IInterface {
    public:
        ::OSkin::API::InterfaceVersion GetVersion() override { return ::OSkin::API::InterfaceVersion::v1; }

        bool OpenMenu(RE::Actor* a_actor, const ::OSkin::API::MenuState* a_state) override {
            // A state from an older copy of the header has fewer fields, it's left out
            const bool complete{a_state && a_state->size >= sizeof(::OSkin::API::MenuState)};

            return UI::Menu::OpenFor(a_actor, complete ? a_state : nullptr);
        }

        RE::Actor* GetPreviewCopyTarget(RE::Actor* a_actor) override {
            return Preview::Session::GetInstance().GetCopyTarget(a_actor);
        }
    };

    Interface g_interface;

    // ReSharper disable once CppParameterMayBeConstPtrOrRef
    void OnMessage(SKSE::MessagingInterface::Message* a_message) {
        using Request = ::OSkin::API::RequestInterface;
        using Version = ::OSkin::API::InterfaceVersion;

        if (!a_message || a_message->type != Request::type) return;

        if (a_message->dataLen < sizeof(Request) || !a_message->data) {
            logger::error("{} asked for OSkin's interface with a message too short", a_message->sender);
            return;
        }

        const auto* const request{static_cast<Request*>(a_message->data)};

        if (request->version == Version::Invalid || request->version > Version::Latest || !request->oskinInterface) {
            logger::error("{} asked for OSkin's interface version {}, which OSkin doesn't have", a_message->sender,
                          std::to_underlying(request->version));
            return;
        }

        *request->oskinInterface = &g_interface;
        logger::info("Gave OSkin's interface version {} to {}", std::to_underlying(request->version),
                     a_message->sender);
    }
}  // namespace

void API::OBody::Request() {
    ::OBody::API::SKSEMessages::RequestPluginInterface request{};
    request.version = ::OBody::API::PluginAPIVersion::v2;
    request.pluginInterface = &g_obody;
    request.readinessEventListener = &g_readinessListener;

    SKSE::GetMessagingInterface()->Dispatch(::OBody::API::SKSEMessages::RequestPluginInterface::type, &request,
                                            sizeof(request), "OBody");

    // An OBody without version 2 doesn't answer, then OSkin works on its own
    logger::info("OBody's plugin interface version 2 {}", g_obody ? "was given" : "isn't there");

    // A preset given or changed may change what the rules give the actor
    if (g_obody && !g_obody->RegisterEventListener(g_presetListener)) {
        logger::warn("OBody didn't take OSkin's listener, a preset change doesn't change the skin until it loads");
    }
}

std::string API::OBody::GetPreset(RE::Actor* a_actor) {
    if (!a_actor || !IsAvailable()) return {};

    ::OBody::API::PresetAssignmentInformation information;
    information.flags = ::OBody::API::PresetAssignmentInformation::Flags::None;

    g_obody->GetPresetAssignedToActor(a_actor, information);

    return std::string{information.presetName};
}

std::vector<std::string> API::OBody::GetPresetNames(const bool a_female) {
    if (!IsAvailable()) return {};

    std::vector<std::string_view> buffer(g_obody->GetAllPresetNames(a_female, nullptr, 0));
    const auto found{g_obody->GetAllPresetNames(a_female, buffer.data(), buffer.size())};
    std::vector<std::string> names;

    for (std::size_t i = 0; i < found; ++i) {
        names.emplace_back(buffer[i]);
    }

    return names;
}

bool API::OBody::IsAvailable() { return g_obody && g_obodyReady.load(); }

bool API::OBody::OpenPresetMenu(RE::Actor* a_actor, const ::OSkin::API::MenuState& a_state) {
    if (!IsAvailable()) return false;

    ::OBody::API::MenuState state;
    state.broughtToPlayer = a_state.broughtToPlayer;
    state.clothed = a_state.clothed;
    state.lit = a_state.lit;
    state.cursorX = a_state.cursorX;
    state.cursorY = a_state.cursorY;

    return g_obody->OpenPresetMenu(a_actor, &state);
}

RE::Actor* API::OBody::GetPreviewCopyTarget(RE::Actor* a_actor) {
    return a_actor && IsAvailable() ? g_obody->GetPreviewCopyTarget(a_actor) : nullptr;
}

void API::RaceMenu::Request() {
    if (g_morphs) return;

    SKEE::InterfaceExchangeMessage message;
    SKSE::GetMessagingInterface()->Dispatch(SKEE::InterfaceExchangeMessage::kExchangeInterface, &message,
                                            sizeof(SKEE::InterfaceExchangeMessage*), "skee");

    if (!message.interfaceMap) {
        logger::info("RaceMenu didn't answer, the preview copy doesn't get the body morphs yet");
        return;
    }

    auto* const morphs{static_cast<SKEE::IBodyMorphInterface*>(message.interfaceMap->QueryInterface("BodyMorph"))};

    if (!morphs || morphs->GetVersion() == 0) {
        logger::warn("RaceMenu has no body morph interface, the preview copy gets no body morphs");
        return;
    }

    g_morphs = morphs;
    logger::info("Got RaceMenu's body morph interface version {}", morphs->GetVersion());
}

void API::RaceMenu::CopyBody(RE::Actor* a_from, RE::Actor* a_to) {
    if (!g_morphs || !a_from || !a_to) return;

    struct Morph {
        std::string name;
        std::string key;
        float value;
    };

    struct NameVisitor final : SKEE::IBodyMorphInterface::MorphVisitor {
        void Visit(RE::TESObjectREFR*, const char* a_name) override {
            if (a_name) names.emplace_back(a_name);
        }

        std::vector<std::string> names;
    } names;

    struct KeyVisitor final : SKEE::IBodyMorphInterface::MorphKeyVisitor {
        void Visit(const char* a_key, const float a_value) override {
            // ORefit's are for the clothes worn, OBody sets them again as the copy is dressed
            if (a_key && !stl::cmp(a_key, "OClothe")) morphs.push_back({*name, a_key, a_value});
        }

        std::vector<Morph> morphs;
        const std::string* name{nullptr};
    } keys;

    g_morphs->VisitMorphs(a_from, names);

    for (const auto& name : names.names) {
        keys.name = &name;
        g_morphs->VisitKeys(a_from, name.c_str(), keys);
    }

    // The game can build the copy's 3D as it's placed, so something may have morphed it already
    g_morphs->ClearMorphs(a_to);

    for (const auto& morph : keys.morphs) {
        g_morphs->SetMorph(a_to, morph.name.c_str(), morph.key.c_str(), morph.value);
    }

    if (a_to->Is3DLoaded()) {
        g_morphs->ApplyBodyMorphs(a_to, true);
    }

    logger::info("Preview: the copy got {} body morphs of {}", keys.morphs.size(), a_from->GetDisplayFullName());
}

void API::OSkin::Register() { SKSE::GetMessagingInterface()->RegisterListener(nullptr, OnMessage); }
