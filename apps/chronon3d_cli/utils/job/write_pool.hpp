#pragma once

// write_pool.hpp
//
// Bounded thread pool for parallel I/O during a render job.
//
// Replaces the std::async(std::launch::async, ...) chain in
// render_job_execute.cpp that launched one detached future per frame.
// The chain was correct but had three problems:
//   1) std::async returns a future that the caller MUST .get() to
//      propagate exceptions; otherwise the destructor blocks.
//   2) Each launch creates a fresh std::thread (no thread reuse).
//      For 900 frames that's 900 thread spawns.
//   3) The "previous write blocks" serialisation is implicit (via
//      prev_write.get()) which makes pipeline overlap fragile.
//
// WritePool is a fixed-size pool of worker threads (default 2) plus
// a thread-safe queue.  Producers (the render thread) call submit()
// and get a std::future<double> back.  Workers process jobs FIFO.
// drain() blocks until all submitted jobs complete.
//
// Usage:
//   WritePool pool(/*workers=*/2);
//   auto fut = pool.submit([&]{ return write_frame_to_disk(...); });
//   double encode_ms = fut.get();   // blocks until done
//   pool.drain();                    // wait for any background jobs
//   // (destructor also drains)

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace chronon3d::cli {

class WritePool {
public:
    using Job = std::packaged_task<double()>;

    explicit WritePool(std::size_t workers = 2) {
        if (workers == 0) workers = 1;
        m_workers.reserve(workers);
        for (std::size_t i = 0; i < workers; ++i) {
            m_workers.emplace_back([this] { worker_loop(); });
        }
    }

    ~WritePool() {
        shutdown();
    }

    // Non-copyable, non-movable.
    WritePool(const WritePool&) = delete;
    WritePool& operator=(const WritePool&) = delete;
    WritePool(WritePool&&) = delete;
    WritePool& operator=(WritePool&&) = delete;

    /// Submit a job that returns double (encode_ms).  Returns a future
    /// that resolves to the job's return value.  The job runs on one
    /// of the worker threads.
    std::future<double> submit(Job job) {
        auto fut = job.get_future();
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_queue.push(std::move(job));
        }
        m_cv.notify_one();
        return fut;
    }

    /// Block until all submitted jobs complete.  Idempotent.
    void drain() {
        std::unique_lock<std::mutex> lk(m_mutex);
        m_drain.wait(lk, [this] { return m_queue.empty() && m_in_flight == 0; });
    }

    /// Stop the pool and join all workers.  Any jobs still in the
    /// queue are discarded.  Idempotent.
    void shutdown() {
        if (m_stopping.exchange(true)) return;
        m_cv.notify_all();
        for (auto& t : m_workers) {
            if (t.joinable()) t.join();
        }
    }

    std::size_t worker_count() const { return m_workers.size(); }
    std::size_t queue_size() const {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_queue.size();
    }

private:
    void worker_loop() {
        while (true) {
            Job job;
            {
                std::unique_lock<std::mutex> lk(m_mutex);
                m_cv.wait(lk, [this] { return m_stopping.load() || !m_queue.empty(); });
                if (m_stopping.load() && m_queue.empty()) return;
                if (m_queue.empty()) continue;
                job = std::move(m_queue.front());
                m_queue.pop();
                ++m_in_flight;
            }
            try {
                job();
            } catch (...) {
                // The packaged_task's promise is already satisfied
                // with the exception; we just continue.
            }
            {
                std::lock_guard<std::mutex> lk(m_mutex);
                --m_in_flight;
            }
            m_drain.notify_all();
        }
    }

    std::vector<std::thread> m_workers;
    std::queue<Job> m_queue;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::condition_variable m_drain;
    std::atomic<bool> m_stopping{false};
    std::size_t m_in_flight{0};
};

} // namespace chronon3d::cli
