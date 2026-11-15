#include "API/Links.h"
#include "Config/Config.h"
#include "STL.h"
#include "Skin/Diagnostics.h"
#include "Skin/Distribution.h"
#include "Skin/Hooks.h"
#include "Skin/Packs.h"
#include "UI/Translations.h"
#include "UI/UI.h"

namespace {
    void InitializeLogging() {
        // ReSharper disable once CppLocalVariableMayBeConst
        auto path{logger::log_directory()};
        if (!path) {
            SKSE::stl::report_and_fail("Unable to lookup SKSE logs directory.");
        }
        *path /= std::format("{}.log", SKSE::PluginDeclaration::GetSingleton()->GetName());

        auto log{std::make_shared<spdlog::logger>("Global")};
        auto& log_sinks{log->sinks()};

        if (REX::W32::IsDebuggerPresent()) {
            log_sinks.reserve(2);
            const auto msvc_sink{std::make_shared<spdlog::sinks::msvc_sink_mt>()};
            msvc_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] [OSkin.dll,%s:%#] %v");
            log_sinks.emplace_back(msvc_sink);
        }
        const auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
        file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] [%t] [%s:%#] %v");
        log_sinks.emplace_back(file_sink);

        log->set_level(spdlog::level::info);
        log->flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::move(log));
    }

    // ReSharper disable once CppParameterMayBeConstPtrOrRef
    void SKSEMessageHandler(SKSE::MessagingInterface::Message* a_msg) {
        switch (a_msg->type) {
            case SKSE::MessagingInterface::kPostLoad: {
                const REX::W32::HMODULE tweaks{REX::W32::GetModuleHandleA("po3_Tweaks")};
                stl::func = reinterpret_cast<stl::PO3_tweaks_GetFormEditorID>(
                    REX::W32::GetProcAddress(tweaks, "GetFormEditorID"));
                logger::info("Got po3_tweaks api: {}", stl::func != nullptr);

                auto* const serialization{SKSE::GetSerializationInterface()};

                serialization->SetUniqueID(Skin::Distribution::CosaveUID);
                serialization->SetSaveCallback(Skin::Distribution::SaveState);
                serialization->SetLoadCallback(Skin::Distribution::LoadState);
                serialization->SetRevertCallback(Skin::Distribution::RevertState);

                API::OSkin::Register();

                return;
            }

            // Other plugins answer requests for their interfaces from here on
            case SKSE::MessagingInterface::kPostPostLoad: {
                API::OBody::Request();
                API::RaceMenu::Request();

                return;
            }

            case SKSE::MessagingInterface::kPreLoadGame: {
                logger::info("Game beginning to load");

                return;
            }

            case SKSE::MessagingInterface::kPostLoadGame: {
                logger::info("Game finished loading");

                Skin::Distribution::ApplyToLoadedActors("Save loaded");

                return;
            }

            case SKSE::MessagingInterface::kNewGame: {
                logger::info("New game started");

                return;
            }

            case SKSE::MessagingInterface::kDataLoaded: {
                UI::Translations::Init();

                // RaceMenu doesn't always answer at kPostPostLoad
                API::RaceMenu::Request();

                Skin::Packs::Scan();

                if (const auto result{Parser::JSONParser::GetInstance().Load()}; result.error) {
                    SKSE::stl::report_and_fail(std::format("{}. Please check OSkin.log.", *result.error));
                }

                Skin::Hooks::Install();
                Skin::Diagnostics::Register();
                UI::Register();

                return;
            }
            default:
                return;
        }
    }
}  // namespace

SKSEPluginLoad(const SKSE::LoadInterface* a_skse) {
    [[maybe_unused]] stl::timeit const t;
    InitializeLogging();

    const auto* const plugin{SKSE::PluginDeclaration::GetSingleton()};
    logger::info("{} {} is loading...", plugin->GetName(), plugin->GetVersion().string("."));

    SKSE::Init(a_skse, {.log = false, .trampoline = true, .trampolineSize = Skin::Hooks::TrampolineSize});

    stl::mainThreadId = std::this_thread::get_id();

    if (const auto* const message = SKSE::GetMessagingInterface(); !message->RegisterListener(SKSEMessageHandler)) {
        return false;
    }

    logger::info("{} has finished loading.", plugin->GetName());

    return true;
}
