#pragma once

namespace Preview {
    struct ThirdPersonBackup {
        RE::CameraState state{};
        RE::NiPoint2 freeRotation;
        bool freeRotationEnabled{};
    };

    class Session {
    public:
        static Session& GetInstance();

        Session(Session&&) = delete;
        Session(const Session&) = delete;

        Session& operator=(Session&&) = delete;
        Session& operator=(const Session&) = delete;

        bool Begin(RE::Actor* a_actor);

        void End(bool a_keepSkin = false);

        void Apply(const std::function<void()>& a_apply);

        void Abandon();

        void RunWhenMenuCloses(RE::FormID a_key, std::function<void()> a_func);
        void OnMenuClosed();

        void OnSave();

        [[nodiscard]] bool IsActive() const;

        [[nodiscard]] bool IsPreviewing(const RE::Actor* a_actor) const;

        void RequestSkin(const std::string& a_name);

        [[nodiscard]] bool GetSkinShownOn(const RE::Actor* a_actor, std::optional<std::string>& a_skin) const;

        [[nodiscard]] std::string GetShownSkin() const;

        [[nodiscard]] RE::FormID GetShownActorID() const;

        [[nodiscard]] RE::NiPointer<RE::Actor> GetRuleSubject(const RE::Actor* a_actor) const;

        [[nodiscard]] bool IsClothed() const;
        void SetClothed(bool a_clothed);

        [[nodiscard]] bool IsLit() const;
        void SetLit(bool a_lit);

        void Orbit(float a_across, float a_down);
        void Pan(float a_across, float a_down);
        void Zoom(float a_steps);

        void Recenter();

        void ResetView();

        void SetScreenPosition(float a_across);

        void FaceCamera();

        [[nodiscard]] bool CanBringToPlayer() const;
        [[nodiscard]] bool IsBroughtToPlayer() const;
        void SetBroughtToPlayer(bool a_brought);

        static void SetAlwaysCopy(bool a_always);
        [[nodiscard]] static bool IsAlwaysCopy();

        [[nodiscard]] bool IsCopy(const RE::TESForm* a_form) const;
        [[nodiscard]] bool IsCopy(RE::FormID a_formID) const;
        // No lock, the 3D hooks call it
        [[nodiscard]] RE::Actor* GetCopyTarget(const RE::Actor* a_actor) const;

        // Takes lock_, unlike IsCopy which the event handlers call
        [[nodiscard]] bool IsAnyCopy(const RE::Actor* a_actor) const;

        void Update();

    private:
        struct UnequippedItem {
            RE::TESBoundObject* object;
            const RE::ExtraDataList* extraList;
            RE::BGSEquipSlot* slot{nullptr};
            std::int32_t count{1};
        };

        struct CameraBackup {
            bool switched{};
            RE::NiPoint3 translation;
            RE::BSTPoint2<float> rotation;
            std::optional<ThirdPersonBackup> thirdPerson;
        };

        struct View {
            float orbit{std::numbers::pi_v<float>};

            float pitch{0.3F};

            float zoom{1.0F};

            float panRight{0.0F};
            float panUp{0.0F};
        };

        struct Framing {
            float distance;
            RE::NiPoint3 offset;
        };

        Session() = default;
        ~Session() = default;

        static Session instance_;

        static bool SettlePose(RE::Actor* a_actor, RE::NiAVObject* a_root);

        void BeginSettling();
        void UpdateSettling();

        void RestoreSkin();
        void Show(const std::string& a_name);

        void Unequip();
        void EquipBack();

        void QueueWornUpdate();

        void QueueLightUpdate();
        void UpdateLight();

        void RemoveLight();

        void EndActor(bool a_keepSkin);

        void EnterScene();
        void LeaveScene();
        void RestoreBlur();
        void RestoreHUD();

        void Present(bool a_keepView = false);

        void ChooseView();

        [[nodiscard]] Framing ComputeFraming(const View& a_view) const;

        [[nodiscard]] RE::NiPoint3 CameraPosition(const View& a_view) const;
        [[nodiscard]] float CameraYaw(const View& a_view) const;

        bool EaseView();
        void ApplyFraming();
        void RestoreCamera();

        void QueueCameraUpdate();
        void UpdateCamera();

        void HideOtherActors();

        void UpdateOccluders(const RE::NiPoint3& a_cameraPosition);

        static void ShowAll(std::vector<RE::NiPointer<RE::NiAVObject>>& a_hidden);

        [[nodiscard]] bool WantsCopy() const;
        void UpdateCopy();
        void SpawnCopy();
        void QueueDressCopy();
        [[nodiscard]] bool IsCopyReady();
        void ShowCopy();
        void ShowTarget();
        void PlaceCopy(bool a_atPlayer);
        void DeleteCopy();
        void DeleteDisabledCopies(bool a_all);
        void ResetCopy();

        void ResetActor();
        void ResetScene();

        mutable std::recursive_mutex lock_;

        std::atomic<RE::FormID> formID_{0};

        RE::NiPointer<RE::Actor> target_;
        RE::NiPointer<RE::Actor> actor_;
        RE::NiPointer<RE::NiAVObject> root_;

        static inline std::atomic_bool alwaysCopy_{false};

        std::vector<std::pair<RE::FormID, std::function<void()>>> onMenuClosed_;

        bool bring_{false};

        RE::NiPointer<RE::Actor> copy_;
        std::uint32_t copyRequest_{0};
        bool copySpawnQueued_{false};
        bool copyDressQueued_{false};
        bool copyDressed_{false};
        bool copyShown_{false};
        bool copyAtPlayer_{false};
        std::chrono::steady_clock::time_point copyRequestedAt_;
        std::chrono::steady_clock::time_point copyDressedAt_;

        std::optional<std::chrono::steady_clock::time_point> settlingSince_;

        RE::FormID copyFailedFor_{0};

        std::atomic<RE::FormID> copyID_{0};
        std::atomic<RE::FormID> copyOf_{0};
        std::atomic<RE::FormID> deletedCopyID_{0};
        std::atomic<RE::FormID> deletedCopyOf_{0};
        std::atomic<std::int64_t> deletedCopyAt_{0};

        std::vector<std::pair<RE::NiPointer<RE::Actor>, std::int64_t>> disabledCopies_;
        std::atomic<const RE::TESNPC*> spawningBase_{nullptr};
        std::atomic<RE::FormID> spawningFrom_{0};

        std::string requestedSkin_;
        std::string shownSkin_;
        std::chrono::steady_clock::time_point requestTime_;

        bool clothed_{true};
        std::vector<UnequippedItem> unequipped_;

        std::optional<float> originalHeading_;

        bool lit_{false};

        RE::NiPointer<RE::NiPointLight> light_;
        RE::NiPointer<RE::BSLight> bsLight_;
        std::atomic_bool lightUpdateQueued_{false};

        std::vector<RE::NiPointer<RE::NiAVObject>> hiddenActors_;
        std::vector<RE::NiPointer<RE::NiAVObject>> hiddenOccluders_;

        bool occludersDirty_{false};

        bool sceneActive_{false};
        bool frozeTime_{false};

        bool removedBlur_{false};

        RE::GPtr<RE::IMenu> hiddenHUD_;

        bool framingAvailable_{false};
        bool presented_{false};
        std::optional<CameraBackup> cameraBackup_;
        std::atomic_bool cameraUpdateQueued_{false};

        View view_;
        View targetView_;
        View defaultView_;
        float screenPosition_{0.3F};
        std::chrono::steady_clock::time_point lastEase_;
    };
}  // namespace Preview
