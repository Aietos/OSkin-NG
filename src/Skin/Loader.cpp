#include "Skin/Loader.h"

#include "STL.h"
#include "Skin/Distribution.h"

namespace {
    using Clock = std::chrono::steady_clock;
    using Textures = std::vector<RE::NiPointer<RE::NiTexture>>;

    // A load that never ends doesn't keep the skin waiting
    constexpr auto kMaxWait{std::chrono::seconds{30}};

    struct Waiting {
        RE::ActorHandle actor;
        RE::FormID formID{0};
        bool firstPerson{false};
        std::string source;
        std::string skin;
        Textures textures;
        std::uint64_t job{0};
        Clock::time_point start;
    };

    // Only the main thread uses these
    std::vector<Waiting> g_waiting;
    bool g_pumping{false};
    // A swap run again after its load doesn't wait again
    bool g_finishing{false};
    std::uint64_t g_lastJob{0};
    std::optional<RE::NiTexture*> g_standIn;

    // The game only gives textures it has in memory and a stand in for the rest, so a thread of ours loads them
    // These are shared with that thread
    struct Job {
        std::uint64_t id{0};
        std::vector<std::string> paths;
    };

    std::mutex g_lock;
    std::condition_variable g_wake;
    std::deque<Job> g_jobs;
    std::unordered_set<std::uint64_t> g_wanted;
    std::vector<std::pair<std::uint64_t, Textures>> g_done;
    bool g_started{false};

    bool IsWanted(const std::uint64_t a_job) {
        std::lock_guard lock{g_lock};

        return g_wanted.contains(a_job);
    }

    // Loads one job at a time, the newest skin asked for gets through quickly since the older ones stop
    void LoadJobs() {
        while (true) {
            Job job;

            {
                std::unique_lock lock{g_lock};
                g_wake.wait(lock, [] { return !g_jobs.empty(); });
                job = std::move(g_jobs.front());
                g_jobs.pop_front();
            }

            Textures textures;

            for (const auto& path : job.paths) {
                if (!IsWanted(job.id)) break;

                RE::NiPointer<RE::NiTexture> texture;
                RE::BSShaderManager::GetTexture(path.c_str(), true, texture, false);

                if (texture) {
                    textures.push_back(std::move(texture));
                }
            }

            // Handed to the main thread, which lets go of them
            std::lock_guard lock{g_lock};
            g_done.emplace_back(job.id, std::move(textures));
        }
    }

    std::uint64_t StartJob(std::vector<std::string> a_paths) {
        const auto id{++g_lastJob};

        {
            std::lock_guard lock{g_lock};

            if (!g_started) {
                g_started = true;
                std::thread{LoadJobs}.detach();
            }

            g_wanted.insert(id);
            g_jobs.push_back(Job{.id = id, .paths = std::move(a_paths)});
        }

        g_wake.notify_one();

        return id;
    }

    void DropJob(const std::uint64_t a_job) {
        std::lock_guard lock{g_lock};

        g_wanted.erase(a_job);
    }

    RE::NiTexture* GetStandIn() {
        if (!g_standIn) {
            RE::NiPointer<RE::NiTexture> texture;
            RE::BSShaderManager::GetTexture("textures\\OSkin\\stand in probe.dds", false, texture, false);
            g_standIn = texture.get();
        }

        return *g_standIn;
    }

    bool IsFirstPerson(RE::Actor* a_actor, const RE::NiAVObject* a_root) {
        const auto* const firstPerson{a_actor->Get3D(true)};

        if (!firstPerson || firstPerson == a_actor->Get3D(false)) return false;

        for (const auto* node{a_root}; node; node = node->parent) {
            if (node == firstPerson) return true;
        }

        return false;
    }

    void Replace(const RE::FormID a_formID, const bool a_firstPerson) {
        std::erase_if(g_waiting, [&](const Waiting& a_waiting) {
            const bool same{a_waiting.formID == a_formID && a_waiting.firstPerson == a_firstPerson};

            if (same) {
                DropJob(a_waiting.job);
            }

            return same;
        });
    }

    // Runs the swap again. Its textures are in memory now, so it doesn't wait
    void Finish(Waiting& a_waiting) {
        const auto actor{a_waiting.actor.get()};
        auto* const root{actor ? actor->Get3D(a_waiting.firstPerson) : nullptr};

        if (!root) return;

        // Kept until the swap holds them
        const auto textures{std::move(a_waiting.textures)};

        g_finishing = true;
        Skin::Distribution::ApplyForActor(actor.get(), root, a_waiting.source);
        g_finishing = false;
    }
}  // namespace

void Skin::Loader::Pump() {
    g_pumping = true;

    // Taken each frame, so loads nobody waits for anymore are let go soon
    std::vector<std::pair<std::uint64_t, Textures>> done;

    {
        std::lock_guard lock{g_lock};
        done = std::exchange(g_done, {});
    }

    if (g_waiting.empty()) return;

    auto waiting{std::exchange(g_waiting, {})};

    for (auto& entry : waiting) {
        const auto loaded{std::ranges::find(done, entry.job, &std::pair<std::uint64_t, Textures>::first)};

        if (loaded == done.end() && Clock::now() - entry.start < kMaxWait) {
            g_waiting.push_back(std::move(entry));

            continue;
        }

        DropJob(entry.job);

        if (loaded != done.end()) {
            std::ranges::move(loaded->second, std::back_inserter(entry.textures));
        }

        if (loaded == done.end()) {
            logger::warn("{} {:08X}: the textures of {} took too long to load, it's shown anyway", entry.source,
                         entry.formID, entry.skin);
        }

        Finish(entry);
    }
}

void Skin::Loader::Stop() {
    if (!stl::IsMainThread()) return;

    g_pumping = false;

    auto waiting{std::exchange(g_waiting, {})};

    for (auto& entry : waiting) {
        DropJob(entry.job);
        Finish(entry);
    }
}

bool Skin::Loader::CanWait() { return stl::IsMainThread() && g_pumping && !g_finishing; }

bool Skin::Loader::Request(const std::vector<std::string>& a_paths, Textures& a_textures) {
    auto* const standIn{GetStandIn()};
    bool loaded{standIn != nullptr};

    for (const auto& path : a_paths) {
        RE::NiPointer<RE::NiTexture> texture;
        RE::BSShaderManager::GetTexture(path.c_str(), false, texture, false);

        if (!texture || texture.get() == standIn) {
            loaded = false;

            continue;
        }

        a_textures.push_back(std::move(texture));
    }

    return loaded;
}

void Skin::Loader::Wait(RE::Actor* a_actor, RE::NiAVObject* a_root, const std::string_view a_source,
                        const std::string_view a_skin, std::vector<std::string> a_paths, Textures a_textures) {
    const auto formID{a_actor->GetFormID()};
    const bool firstPerson{IsFirstPerson(a_actor, a_root)};

    Replace(formID, firstPerson);

    g_waiting.push_back(Waiting{.actor = a_actor->GetHandle(),
                                .formID = formID,
                                .firstPerson = firstPerson,
                                .source = std::string{a_source},
                                .skin = std::string{a_skin},
                                .textures = std::move(a_textures),
                                .job = StartJob(std::move(a_paths)),
                                .start = Clock::now()});
}

void Skin::Loader::Forget(RE::Actor* a_actor, RE::NiAVObject* a_root) {
    const bool firstPerson{IsFirstPerson(a_actor, a_root)};

    // Only a swap on the whole 3D stands for the one waiting, not one on a part that was just put on
    if (a_actor->Get3D(firstPerson) != a_root) return;

    Replace(a_actor->GetFormID(), firstPerson);
}

bool Skin::Loader::IsLoading(const RE::FormID a_actor) {
    return std::ranges::any_of(g_waiting, [&](const Waiting& a_waiting) { return a_waiting.formID == a_actor; });
}
