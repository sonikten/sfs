// src/engine/voice_pool.cpp

#include "voice_pool.h"

#include <thread>

namespace sfs::engine
{

VoicePool::VoicePool(int numWorkers)
{
    if (numWorkers <= 0)
    {
        numWorkers = 1;
    }
    if (numWorkers > kMaxJobs)
    {
        numWorkers = kMaxJobs;
    }
    workers_.reserve(static_cast<std::size_t>(numWorkers));
    for (int i = 0; i < numWorkers; ++i)
    {
        workers_.emplace_back([this] { workerLoop(); });
    }
}

VoicePool::~VoicePool()
{
    stop_.store(true, std::memory_order_release);
    // Release one token per worker so each acquire() returns and the
    // worker observes stop_ == true.
    for (std::size_t i = 0; i < workers_.size(); ++i)
    {
        jobsAvailable_.release();
    }
    for (auto& w : workers_)
    {
        if (w.joinable())
        {
            w.join();
        }
    }
}

void VoicePool::submit(int numActive) noexcept
{
    if (numActive <= 0)
    {
        return;
    }
    if (numActive > kMaxJobs)
    {
        numActive = kMaxJobs;
    }
    // Wake `numActive` workers. Each worker will acquire one token and
    // execute exactly one job from the table.
    jobsAvailable_.release(numActive);
}

void VoicePool::waitFor(int voiceIndex) noexcept
{
    if (voiceIndex < 0 || voiceIndex >= kMaxJobs)
    {
        return;
    }
    // Spin-wait. The audio thread can't kernel-block; per-voice render
    // takes ~100 µs in typical load so the spin is brief. Pause hints
    // would help on x86 but std::this_thread::yield is the portable
    // primitive. We avoid yield in the inner loop because it can context-
    // switch to an unrelated thread; just spin on the atomic.
    auto& j = jobs_[static_cast<std::size_t>(voiceIndex)];
    while (!j.done.load(std::memory_order_acquire))
    {
        // Empty body — pure spin. The audio thread is bounded by the
        // worker's progress, which is bounded by per-voice render cost.
    }
}

void VoicePool::workerLoop() noexcept
{
    while (true)
    {
        jobsAvailable_.acquire();
        if (stop_.load(std::memory_order_acquire))
        {
            return;
        }
        // Look for an unclaimed active job and execute it. The release(N)
        // in submit() guarantees there are exactly N tokens for N jobs;
        // each worker grabs one job and returns to wait.
        for (int v = 0; v < kMaxJobs; ++v)
        {
            auto& j = jobs_[static_cast<std::size_t>(v)];
            if (!j.active.load(std::memory_order_acquire))
            {
                continue;
            }
            bool expected = false;
            if (!j.claimed.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            {
                continue;
            }
            if (j.work != nullptr)
            {
                j.work(j.userData);
            }
            j.done.store(true, std::memory_order_release);
            break;
        }
    }
}

} // namespace sfs::engine
