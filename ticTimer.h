#pragma once

#include <chrono>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <iostream>
#include <optional>
#include <vector>
#include <string>

namespace tic {
    namespace detail {
        class TimerDispatcher {
    private:
        struct TimerTask {
            uint64_t id;
            std::chrono::steady_clock::time_point deadline;
            std::function<void()> callback;
            std::chrono::steady_clock::duration interval;
            bool isRepeating;
            bool isCancelled = false;
            uint32_t repeatCount = 0;
            uint32_t currentRepeat = 0;

            bool operator<(const TimerTask& other) const {
                if (deadline != other.deadline) {
                    return deadline > other.deadline;
                }
                return id > other.id;
            }
        };

    public:
        static TimerDispatcher& instance() {
            static TimerDispatcher instance;
            return instance;
        }

        TimerDispatcher() : m_nextId(1), m_running(false) {
            start();
        }

        ~TimerDispatcher() {
            stop();
        }

        void start() {
            if (m_running.exchange(true)) {
                return;
            }

            m_workerThread = std::thread([this]() {
                run();
                });

            m_callbackThread = std::thread([this]() {
                runCallbacks();
                });
        }

        void stop() {
            if (!m_running.exchange(false)) {
                return;
            }

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_cv.notify_all();
            }

            {
                std::lock_guard<std::mutex> lock(m_callbackMutex);
                m_callbackCv.notify_all();
            }

            if (m_workerThread.joinable()) {
                m_workerThread.join();
            }

            if (m_callbackThread.joinable()) {
                m_callbackThread.join();
            }
        }

        template <class Rep, class Period>
        uint64_t addSingleShot(std::chrono::duration<Rep, Period> delay,
            std::function<void()> callback) {
            if (!m_running.load()) {
                start();
            }

            std::lock_guard<std::mutex> lock(m_mutex);

            TimerTask task;
            task.id = m_nextId++;
            task.deadline = std::chrono::steady_clock::now() + delay;
            task.callback = std::move(callback);
            task.interval = std::chrono::steady_clock::duration::zero();
            task.isRepeating = false;
            task.isCancelled = false;

            m_tasks.push(task);
            m_cv.notify_one();

            return task.id;
        }

        template <class Rep, class Period>
        uint64_t addRapid(std::chrono::duration<Rep, Period> interval,
            std::function<void()> callback, std::optional<uint32_t> repeatCount = std::nullopt) {
            if (!m_running.load()) {
                start();
            }

            std::lock_guard<std::mutex> lock(m_mutex);

            TimerTask task;
            task.id = m_nextId++;
            task.deadline = std::chrono::steady_clock::now() + interval;
            task.callback = std::move(callback);
            task.interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(interval);
            task.isRepeating = true;
            task.isCancelled = false;
            task.repeatCount = repeatCount.value_or(0);
            task.currentRepeat = 0;

            m_tasks.push(task);
            m_cv.notify_one();

            return task.id;
        }

        bool remove(uint64_t timerId) {
            std::lock_guard<std::mutex> lock(m_mutex);

            auto it = m_cancelledIds.find(timerId);
            if (it != m_cancelledIds.end()) {
                return false;
            }

            m_cancelledIds.insert(timerId);
            m_cv.notify_one();
            return true;
        }

        bool isActive(uint64_t timerId) const {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_cancelledIds.find(timerId) == m_cancelledIds.end();
        }

    private:
        void run() {
            while (m_running) {
                std::unique_lock<std::mutex> lock(m_mutex);

                if (m_tasks.empty()) {
                    m_cv.wait(lock, [this]() {
                        return !m_tasks.empty() || !m_running;
                        });

                    if (!m_running) {
                        break;
                    }
                }

                auto currentTime = std::chrono::steady_clock::now();
                auto& nextTask = m_tasks.top();

                if (nextTask.deadline > currentTime) {
                    auto waitTime = nextTask.deadline - currentTime;
                    m_cv.wait_for(lock, waitTime, [this, nextTaskId = nextTask.id]() {
                        return m_cancelledIds.find(nextTaskId) != m_cancelledIds.end() || !m_running;
                        });

                    if (m_cancelledIds.find(nextTask.id) != m_cancelledIds.end()) {
                        m_tasks.pop();
                        m_cancelledIds.erase(nextTask.id);
                        continue;
                    }

                    if (!m_running) {
                        break;
                    }

                    currentTime = std::chrono::steady_clock::now();
                }

                if (!m_tasks.empty() && m_tasks.top().deadline <= currentTime) {
                    TimerTask task = m_tasks.top();
                    m_tasks.pop();

                    if (m_cancelledIds.find(task.id) != m_cancelledIds.end()) {
                        m_cancelledIds.erase(task.id);
                        continue;
                    }

                    {
                        std::lock_guard<std::mutex> callbackLock(m_callbackMutex);
                        m_callbackQueue.push(task.callback);
                    }

                    m_callbackCv.notify_one();

                    if (task.isRepeating &&
                        m_cancelledIds.find(task.id) == m_cancelledIds.end()) {
                        task.currentRepeat++;
                        if (task.repeatCount == 0 || task.currentRepeat < task.repeatCount) {
                            task.deadline = currentTime + task.interval;
                            m_tasks.push(task);
                        }
                        else {
                            m_cancelledIds.erase(task.id);
                            m_cancelledIds.insert(task.id);
                        }
                    }
                    else {
                        m_cancelledIds.erase(task.id);
                        m_cancelledIds.insert(task.id);
                    }
                }
            }
        }

        void runCallbacks() {
            while (m_running) {
                std::unique_lock<std::mutex> lock(m_callbackMutex);

                m_callbackCv.wait(lock, [this]() {
                    return !m_callbackQueue.empty() || !m_running;
                    });

                while (!m_callbackQueue.empty() && m_running) {
                    std::function<void()> callback = std::move(m_callbackQueue.front());
                    m_callbackQueue.pop();
                    lock.unlock();

                    try {
                        if (callback) {
                            callback();
                        }
                    }
                    catch (...) {}

                    lock.lock();
                }

                if (!m_running) {
                    break;
                }
            }
        }

    private:
        std::priority_queue<TimerTask> m_tasks;
        std::unordered_set<uint64_t> m_cancelledIds;
        mutable std::mutex m_mutex;
        std::condition_variable m_cv;
        std::thread m_workerThread;
        std::atomic<bool> m_running;
        uint64_t m_nextId;

        std::queue<std::function<void()>> m_callbackQueue;
        std::mutex m_callbackMutex;
        std::condition_variable m_callbackCv;
        std::thread m_callbackThread;
    };
    }

    class SingleShotTimer {
    public:
        SingleShotTimer() = default;

        ~SingleShotTimer() {
            stop();
        }

        SingleShotTimer(const SingleShotTimer&) = delete;
        SingleShotTimer& operator=(const SingleShotTimer&) = delete;

        SingleShotTimer(SingleShotTimer&& other) noexcept
            : m_timerId(other.m_timerId) {
            other.m_timerId = 0;
        }

        SingleShotTimer& operator=(SingleShotTimer&& other) noexcept {
            if (this != &other) {
                stop();
                m_timerId = other.m_timerId;
                other.m_timerId = 0;
            }
            return *this;
        }

        template <class Rep, class Period>
        void start(std::chrono::duration<Rep, Period> delay,
            std::function<void()> onExpired) {
            stop();
            m_timerId = detail::TimerDispatcher::instance().addSingleShot(delay,
                std::move(onExpired));
        }

        void stop() {
            if (m_timerId != 0) {
                detail::TimerDispatcher::instance().remove(m_timerId);
                m_timerId = 0;
            }
        }

        bool isActive() const {
            return m_timerId != 0 && detail::TimerDispatcher::instance().isActive(m_timerId);
        }

    private:
        uint64_t m_timerId = 0;
    };

    class RapidTimer {
    public:
        RapidTimer() = default;

        ~RapidTimer() {
            stop();
        }

        RapidTimer(const RapidTimer&) = delete;
        RapidTimer& operator=(const RapidTimer&) = delete;

        RapidTimer(RapidTimer&& other) noexcept
            : m_timerId(other.m_timerId) {
            other.m_timerId = 0;
        }

        RapidTimer& operator=(RapidTimer&& other) noexcept {
            if (this != &other) {
                stop();
                m_timerId = other.m_timerId;
                other.m_timerId = 0;
            }
            return *this;
        }

        template <class Rep, class Period>
        void start(std::chrono::duration<Rep, Period> interval,
            std::function<void()> onExpired, std::optional<uint32_t> repeatCount = std::nullopt) {
            stop();
            m_timerId = detail::TimerDispatcher::instance().addRapid(interval,
                std::move(onExpired), repeatCount);
        }

        void stop() {
            if (m_timerId != 0) {
                detail::TimerDispatcher::instance().remove(m_timerId);
                m_timerId = 0;
            }
        }

        bool isActive() const {
            return m_timerId != 0 && detail::TimerDispatcher::instance().isActive(m_timerId);
        }

    private:
        uint64_t m_timerId = 0;
    };

    enum class Mode {
        SingleShot,
        Rapid
    };

    class Timer {
    public:
        Timer(Mode mode = Mode::SingleShot) : m_mode(mode) {}

        ~Timer() {
            stop();
        }

        Timer(const Timer&) = delete;
        Timer& operator=(const Timer&) = delete;

        Timer(Timer&& other) noexcept
            : m_mode(other.m_mode),
            m_singleShot(std::move(other.m_singleShot)),
            m_rapid(std::move(other.m_rapid)) {
        }

        Timer& operator=(Timer&& other) noexcept {
            if (this != &other) {
                stop();
                m_mode = other.m_mode;
                m_singleShot = std::move(other.m_singleShot);
                m_rapid = std::move(other.m_rapid);
            }
            return *this;
        }

        void setMode(Mode mode) {
            if (mode != m_mode) {
                stop();
                m_mode = mode;
            }
        }

        template <class Rep, class Period>
        void start(std::chrono::duration<Rep, Period> duration,
            std::function<void()> onExpired, std::optional<uint32_t> repeatCount = std::nullopt) {
            stop();

            if (m_mode == Mode::SingleShot) {
                m_singleShot.start(duration, std::move(onExpired));
            }
            else {
                m_rapid.start(duration, std::move(onExpired), repeatCount);
            }
        }

        void stop() {
            if (m_mode == Mode::SingleShot) {
                m_singleShot.stop();
            }
            else {
                m_rapid.stop();
            }
        }

        bool isActive() const {
            return m_mode == Mode::SingleShot ?
                m_singleShot.isActive() :
                m_rapid.isActive();
        }

    private:
        Mode m_mode;
        SingleShotTimer m_singleShot;
        RapidTimer m_rapid;
    };
}

