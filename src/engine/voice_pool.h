// src/engine/voice_pool.h
//
// Phase 3 §9 step 9 — voice-pool threading.
//
// Parallelises per-voice render across worker threads while preserving the
// bit-exact determinism contract:
//
//   * Per-voice render is independent (each Voice owns its substrate +
//     agents + harvester state). Workers can run them in any order.
//   * Bus mix MUST happen in voice-index order on the audio thread
//     because float addition is non-associative; merging in a different
//     order produces different bits across thread schedules.
//   * No allocations or kernel waits on the audio thread. Workers
//     kernel-sleep on a counting semaphore while idle; the audio thread
//     spin-waits on per-voice atomic done flags during the parallel
//     phase, then merges sequentially.
//
// The pool is OWNED by VoiceManager (one pool per VoiceManager instance).
// Worker threads are created at VoiceManager construction (not on the
// audio thread) and live for the manager's lifetime.

#pragma once

#include <array>
#include <atomic>
#include <semaphore>
#include <thread>
#include <vector>

namespace sfs::engine
{

// One job per voice slot. The audio thread populates the function pointer
// + arguments, then signals workers to claim and execute. Each voice slot
// has its own job; up to 8 jobs in flight per render call.
struct VoiceRenderJob
{
    // Type-erased work function. Workers call work(userData) which
    // dispatches the right Voice::renderBlock* method with the right
    // scratch buffers (set up in VoiceManager).
    void (*work)(void* userData) = nullptr;
    void* userData = nullptr;

    // Lifecycle flags. claimed: a worker has taken this job; done: the
    // worker has finished. Audio thread spin-reads `done` in voice-index
    // order. Both default-cleared at job submission.
    std::atomic<bool> claimed{false};
    std::atomic<bool> done{false};
    // True iff this slot has a real job for the current render call.
    std::atomic<bool> active{false};
};

class VoicePool
{
public:
    static constexpr int kMaxJobs = 8; // matches VoiceManager::kMaxVoices

    explicit VoicePool(int numWorkers);
    ~VoicePool();

    VoicePool(const VoicePool&) = delete;
    VoicePool& operator=(const VoicePool&) = delete;

    // Submit `numActive` jobs (slots 0..numActive-1) for parallel
    // execution. Caller must have populated jobs_[0..numActive-1].work
    // and .userData and reset .claimed / .done before calling. Returns
    // immediately; workers wake up and start rendering.
    void submit(int numActive) noexcept;

    // Spin-wait until job `voiceIndex` is done (.done == true). Audio
    // thread calls this in voice-index order and merges scratch into
    // bus output between waits. Lock-free; no kernel call.
    void waitFor(int voiceIndex) noexcept;

    // Mutable access to the job table (audio thread populates jobs
    // before submit()). Caller is responsible for not racing with
    // workers — only call between submit/waitFor sequences.
    [[nodiscard]] VoiceRenderJob& job(int voiceIndex) noexcept { return jobs_[static_cast<std::size_t>(voiceIndex)]; }

    [[nodiscard]] int numWorkers() const noexcept { return static_cast<int>(workers_.size()); }

private:
    void workerLoop() noexcept;

    std::array<VoiceRenderJob, kMaxJobs> jobs_{};
    // Counting semaphore: audio thread releases N tokens (one per job);
    // workers each acquire one token and execute one job. Capacity is
    // kMaxJobs because at most that many jobs can be in flight.
    std::counting_semaphore<kMaxJobs> jobsAvailable_{0};
    std::atomic<bool> stop_{false};
    std::vector<std::thread> workers_;
};

} // namespace sfs::engine
