#include "Skin/Distribution.h"
#include "Preview/Physics.h"
#include "Preview/Preview.h"
#include "API/Links.h"
#include "STL.h"

namespace Preview {
    namespace {
        constexpr auto kCopyTimeout{std::chrono::seconds{5}};

        constexpr auto kWornTimeout{std::chrono::milliseconds{500}};

        constexpr std::int64_t kDeletedCopyMs{5000};

        // How long after it was disabled a copy can be deleted, the game's tasks for it have run by then
        constexpr std::int64_t kDisabledCopyDeleteMs{1000};

        std::int64_t NowMs() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                .count();
        }

        RE::ExtraDataList* WornList(const RE::InventoryEntryData* a_entry) {
            if (!a_entry->extraLists) return nullptr;
            for (auto* const extraList : *a_entry->extraLists) {
                if (extraList && (extraList->HasType(RE::ExtraDataType::kWorn) ||
                                  extraList->HasType(RE::ExtraDataType::kWornLeft))) {
                    return extraList;
                }
            }
            return nullptr;
        }

        std::pair<int, int> AttachedArmors(RE::Actor* a_actor) {
            const auto& biped{a_actor->GetBiped(false)};
            if (!biped) return {0, 0};

            int worn{0}, attached{0};
            for (const auto* const entry : stl::GetWornEntries(a_actor)) {
                if (!entry->object->As<RE::TESObjectARMO>()) continue;
                ++worn;
                if (std::ranges::any_of(biped->objects, [&](const auto& a_object) {
                        return a_object.item == entry->object && a_object.partClone;
                    })) {
                    ++attached;
                }
            }
            return {attached, worn};
        }
    }  // namespace

    void Session::SetAlwaysCopy(const bool a_always) {
        alwaysCopy_ = a_always;
        logger::info("Preview: {} a copy of the actor", a_always ? "always previewing" : "no longer always previewing");
    }

    bool Session::IsAlwaysCopy() { return alwaysCopy_; }

    bool Session::IsCopy(const RE::TESForm* a_form) const {
        if (!a_form) return false;

        const auto formID{a_form->GetFormID()};
        if (IsCopy(formID)) return true;

        // The game can start building it before it's known which actor it is
        if (const auto* const base{spawningBase_.load()}; base && formID != spawningFrom_ && a_form->IsDynamicForm()) {
            const auto* const actor{a_form->As<RE::Actor>()};
            return actor && actor->GetActorBase() == base;
        }
        return false;
    }

    bool Session::IsCopy(const RE::FormID a_formID) const {
        if (a_formID == 0) return false;
        if (a_formID == copyID_) return true;
        return a_formID == deletedCopyID_ && NowMs() - deletedCopyAt_ < kDeletedCopyMs;
    }

    RE::Actor* Session::GetCopyTarget(const RE::Actor* a_actor) const {
        if (!IsCopy(a_actor)) return nullptr;

        // The copy shown, the one deleted a moment ago, or the one being placed. No lock, the 3D hooks ask
        const auto formID{a_actor->GetFormID()};
        RE::FormID target{0};

        if (formID == copyID_) {
            target = copyOf_;
        } else if (IsCopy(formID)) {
            target = deletedCopyOf_;
        } else {
            target = spawningFrom_;
        }

        return target != 0 ? RE::TESForm::LookupByID<RE::Actor>(target) : nullptr;
    }

    bool Session::IsAnyCopy(const RE::Actor* a_actor) const {
        if (IsCopy(a_actor)) return true;

        std::lock_guard lock{lock_};

        return std::ranges::any_of(disabledCopies_,
                                   [a_actor](const auto& a_disabled) { return a_disabled.first.get() == a_actor; });
    }

    bool Session::WantsCopy() const {
        // The player's copy wouldn't have their face, and isn't needed to bring them to themselves
        return target_ && !target_->IsPlayerRef() && copyFailedFor_ != target_->GetFormID() && (bring_ || alwaysCopy_);
    }

    void Session::UpdateCopy() {
        if (!WantsCopy()) {
            if (copyShown_) {
                ShowTarget();
            }
            if (copy_ || copySpawnQueued_) {
                DeleteCopy();
            }
            return;
        }

        if (!copy_) {
            if (!copySpawnQueued_) {
                SpawnCopy();
            }
            return;
        }

        if (!copyShown_) {
            if (std::chrono::steady_clock::now() - copyRequestedAt_ > kCopyTimeout) {
                const auto [attached, worn]{AttachedArmors(copy_.get())};
                logger::warn(
                    "Preview: the copy of {} wasn't ready after {} s ({}, {}dressed, {} of {} worn armors have "
                    "their 3D), previewing them instead",
                    target_->GetDisplayFullName(), kCopyTimeout.count(), copy_->Is3DLoaded() ? "3D loaded" : "no 3D",
                    copyDressed_ ? "" : "not ", attached, worn);
                copyFailedFor_ = target_->GetFormID();
                bring_ = false;
                DeleteCopy();
                return;
            }

            if (IsCopyReady()) {
                ShowCopy();
            }
            return;
        }

        if (copyAtPlayer_ != bring_) {
            PlaceCopy(bring_);
            Physics::Reset();
            Present();
        }
    }

    void Session::SpawnCopy() {
        copySpawnQueued_ = true;
        copyRequestedAt_ = std::chrono::steady_clock::now();
        const auto request{++copyRequest_};

        SKSE::GetTaskInterface()->AddTask([this, request, handle{target_->GetHandle()}] {
            std::lock_guard lock{lock_};
            if (request != copyRequest_ || !copySpawnQueued_) return;
            copySpawnQueued_ = false;

            // Time is frozen while previewing, so nothing is queued for the disabled copies
            DeleteDisabledCopies(false);

            const auto target{handle.get()};
            auto* const base{target ? target->GetActorBase() : nullptr};
            if (!target || !base || !target->Is3DLoaded()) return;

            const auto start{std::chrono::steady_clock::now()};
            spawningFrom_ = target->GetFormID();
            spawningBase_ = base;
            const auto placed{target->PlaceObjectAtMe(base, false)};
            spawningBase_ = nullptr;

            auto* const copy{placed ? placed->As<RE::Actor>() : nullptr};
            if (!copy) {
                logger::warn("Preview: couldn't spawn a copy of {}, previewing them instead",
                             target->GetDisplayFullName());
                spawningFrom_ = 0;
                copyFailedFor_ = target->GetFormID();
                bring_ = false;
                return;
            }
            copyOf_ = target->GetFormID();
            copyID_ = copy->GetFormID();
            spawningFrom_ = 0;
            copy_ = RE::NiPointer<RE::Actor>{copy};

            // A new actor fades in, but only while time runs, so with time frozen it would stay invisible
            if (const auto* const process{copy->GetActorRuntimeData().currentProcess}; process && process->high) {
                process->high->fadeState = RE::HighProcessData::FADE_STATE::kNormal;
                process->high->fadeAlpha = 1.0F;
            } else {
                logger::warn("Preview: the copy of {} has no high process, it may stay invisible",
                             target->GetDisplayFullName());
            }

            copy->EnableAI(false);
            copy->SetCollision(false);
            copy->SetScale(static_cast<float>(target->GetReferenceRuntimeData().refScale) / 100.0F);
            PlaceCopy(bring_);
            API::RaceMenu::CopyBody(target.get(), copy);

            // The copy's skin hooks get the target's skin from GetSkinShownOn as its 3D is built
            logger::info("Preview: spawned a copy of {} ({:08X}) {} in {} us", target->GetDisplayFullName(),
                         copy->GetFormID(), copyAtPlayer_ ? "where the player stands" : "in their place",
                         std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start)
                             .count());
        });
    }

    void Session::QueueDressCopy() {
        copyDressQueued_ = true;

        SKSE::GetTaskInterface()->AddTask([this, request{copyRequest_}] {
            std::lock_guard lock{lock_};
            if (request != copyRequest_ || !copy_ || !target_) return;

            auto* const copy{copy_.get()};
            auto* const equipManager{RE::ActorEquipManager::GetSingleton()};

            std::vector<std::pair<RE::TESBoundObject*, RE::ExtraDataList*>> off;
            for (const auto* const entry : stl::GetWornEntries(copy)) {
                off.emplace_back(entry->object, WornList(entry));
            }
            for (const auto& [object, extraList] : off) {
                equipManager->UnequipObject(copy, object, extraList, 1, nullptr, false, true, false, true);
            }

            std::vector<RE::TESObjectARMO*> on;
            for (const auto* const entry : stl::GetWornEntries(target_.get())) {
                if (auto* const armor{entry->object->As<RE::TESObjectARMO>()}) on.push_back(armor);
            }
            if (actor_ == target_) {
                for (const auto& item : unequipped_) {
                    if (auto* const armor{item.object->As<RE::TESObjectARMO>()};
                        armor && !std::ranges::contains(on, armor)) {
                        on.push_back(armor);
                    }
                }
            }
            for (auto* const armor : on) {
                copy->AddObjectToContainer(armor, nullptr, 1, nullptr);
                equipManager->EquipObject(copy, armor, nullptr, 1, nullptr, false, true, false, true);
            }

            if (copy->Is3DLoaded()) {
                copy->Update3DModel();
                if (auto* const root{copy->Get3D(false)}) {
                    RE::NiUpdateData updateData;
                    root->Update(updateData);
                }
            }

            copyDressed_ = true;
            copyDressedAt_ = std::chrono::steady_clock::now();
            logger::info("Preview: dressed the copy of {}: took off {} items, put on {} armors",
                         target_->GetDisplayFullName(), off.size(), on.size());
        });
    }

    bool Session::IsCopyReady() {
        auto* const copy{copy_.get()};
        if (!copy->Is3DLoaded() || !copy->Get3D(false)) return false;

        if (!copyDressQueued_) {
            QueueDressCopy();
            return false;
        }
        if (!copyDressed_) return false;

        const auto [attached, worn]{AttachedArmors(copy)};
        if (attached < worn) {
            if (std::chrono::steady_clock::now() - copyDressedAt_ < kWornTimeout) return false;
            logger::info("Preview: {} of the {} armors the copy of {} wears have no 3D, showing it anyway",
                         worn - attached, worn, target_->GetDisplayFullName());
        }
        return true;
    }

    void Session::ShowCopy() {
        const bool wasClothed{clothed_};
        const auto skin{requestedSkin_};

        RemoveLight();
        EquipBack();

        if (!shownSkin_.empty()) {
            shownSkin_.clear();
            RestoreSkin();
        }

        auto* const copy{copy_.get()};
        auto* const root{copy->Get3D(false)};

        root->SetAppCulled(false);

        actor_ = copy_;
        root_ = RE::NiPointer<RE::NiAVObject>{root};
        copyShown_ = true;
        BeginSettling();
        originalHeading_.reset();

        shownSkin_.clear();
        if (!wasClothed) {
            Unequip();
            Show(skin);
        } else if (!skin.empty()) {
            Show(skin);
        }

        Physics::Reset();

        Present(!copyAtPlayer_);

        if (lit_) {
            QueueLightUpdate();
        }

        logger::info(
            "Preview: showing the copy of {} {}, {} ms after it was asked for", target_->GetDisplayFullName(),
            copyAtPlayer_ ? "where the player stands" : "in their place",
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - copyRequestedAt_)
                .count());
    }

    void Session::ShowTarget() {
        const bool wasClothed{clothed_};
        const auto skin{requestedSkin_};

        RemoveLight();
        copyShown_ = false;
        settlingSince_.reset();

        auto* const root{target_->Get3D(false)};
        actor_ = target_;
        root_ = RE::NiPointer<RE::NiAVObject>{root};
        clothed_ = true;
        unequipped_.clear();
        originalHeading_.reset();

        if (!root) return;

        shownSkin_.clear();

        if (!wasClothed) {
            Unequip();
            Show(skin);
        } else if (!skin.empty()) {
            Show(skin);
        }

        Physics::Reset();

        Present(!copyAtPlayer_);

        if (lit_) {
            QueueLightUpdate();
        }

        logger::info("Preview: showing {} again", target_->GetDisplayFullName());
    }

    void Session::PlaceCopy(const bool a_atPlayer) {
        auto* const copy{copy_.get()};
        copyAtPlayer_ = a_atPlayer;

        if (a_atPlayer) {
            const auto* const player{RE::PlayerCharacter::GetSingleton()};
            copy->SetPosition(player->GetPosition(), true);
            copy->SetHeading(
                std::remainder(player->GetAngleZ() + std::numbers::pi_v<float>, 2.0F * std::numbers::pi_v<float>));
        } else {
            copy->SetPosition(target_->GetPosition(), true);
            copy->SetHeading(target_->GetAngleZ());
        }

        if (copy->Is3DLoaded()) {
            copy->Update3DPosition(true);
        }
    }

    void Session::DeleteCopy() {
        // A queued spawn or dressing does nothing now
        ++copyRequest_;
        auto copy{std::move(copy_)};
        const auto copyOf{copyOf_.load()};
        ResetCopy();
        if (!copy) return;

        // Still the copy for the event handlers, from now until a while after it's gone
        deletedCopyOf_ = copyOf;
        deletedCopyID_ = copy->GetFormID();
        deletedCopyAt_ = NowMs();

        // Deleted in the game's task phase after disabling, deleting right away can crash the game
        SKSE::GetTaskInterface()->AddTask([this, copy{std::move(copy)}] {
            std::lock_guard lock{lock_};
            copy->Disable();
            deletedCopyAt_ = NowMs();
            logger::info("Preview: disabled the copy {:08X}", copy->GetFormID());
            disabledCopies_.emplace_back(copy, NowMs());
        });
    }

    void Session::DeleteDisabledCopies(const bool a_all) {
        const auto now{NowMs()};
        const auto deleted{std::erase_if(disabledCopies_, [&](const auto& a_disabled) {
            const auto& [copy, disabledAt]{a_disabled};
            if (!a_all && now - disabledAt < kDisabledCopyDeleteMs) return false;
            copy->SetDelete(true);
            return true;
        })};
        if (deleted > 0) {
            logger::info("Preview: deleted {} disabled {}", deleted, deleted == 1 ? "copy" : "copies");
        }
    }

    void Session::OnSave() {
        std::lock_guard lock{lock_};
        End();
        DeleteDisabledCopies(true);
    }

    void Session::ResetCopy() {
        copyID_ = 0;
        copyOf_ = 0;
        copy_.reset();
        copySpawnQueued_ = false;
        copyDressQueued_ = false;
        copyDressed_ = false;
        copyShown_ = false;
        copyAtPlayer_ = false;
    }
}  // namespace Preview
