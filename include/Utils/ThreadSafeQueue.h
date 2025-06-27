#ifndef THREAD_SAFE_QUEUE_H
#define THREAD_SAFE_QUEUE_H

#include <deque>
#include <mutex>
#include <condition_variable>
#include <chrono> // For std::chrono::milliseconds
#include <utility> // For std::forward / std::move

template <typename T>
class ThreadSafeQueue {
public:
    ThreadSafeQueue(size_t maxSize = 0) : m_maxSize(maxSize), m_stopped(false) {}

    void push(T value) {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_stopped) {
            return;
        }
        if (m_maxSize > 0) {
            m_cond_push.wait(lock, [this] { return m_queue.size() < m_maxSize || m_stopped; });
        }
        if (m_stopped) {
            return;
        }
        m_queue.push_back(std::move(value));
        lock.unlock();
        m_cond_pop.notify_one();
    }

    void push_front(T&& item) {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_stopped) {
            return;
        }
        m_queue.push_front(std::move(item));
        lock.unlock();
        m_cond_pop.notify_one();
    }


    bool try_pop(T& value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queue.empty() || m_stopped) {
            return false;
        }
        value = std::move(m_queue.front());
        m_queue.pop_front();
        if (m_maxSize > 0) {
            m_cond_push.notify_one();
        }
        return true;
    }

    bool wait_pop(T& value, std::chrono::milliseconds timeout = std::chrono::milliseconds(0)) {
        std::unique_lock<std::mutex> lock(m_mutex);
        bool timed_out = false;
        if (timeout.count() > 0) {
            if (!m_cond_pop.wait_for(lock, timeout, [this] { return !m_queue.empty() || m_stopped; })) {
                timed_out = true;
            }
        }
        else {
            m_cond_pop.wait(lock, [this] { return !m_queue.empty() || m_stopped; });
        }

        if (m_stopped && m_queue.empty()) {
            return false;
        }
        if (timed_out && m_queue.empty()) {
            return false;
        }
        if (m_queue.empty()) { // Final check after wait
            return false;
        }

        value = std::move(m_queue.front());
        m_queue.pop_front();
        if (m_maxSize > 0) {
            m_cond_push.notify_one();
        }
        return true;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.clear();
        if (m_maxSize > 0) {
            m_cond_push.notify_all();
        }
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.empty();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.size();
    }

    void stop_operations() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopped = true;
        m_cond_pop.notify_all();
        if (m_maxSize > 0) {
            m_cond_push.notify_all();
        }
    }

    void resume_operations() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopped = false;
        m_cond_pop.notify_all();
        if (m_maxSize > 0) {
            m_cond_push.notify_all();
        }
    }

    size_t get_max_size_debug() const { // Added for AppInit logging
        return m_maxSize;
    }

private:
    std::deque<T> m_queue;
    mutable std::mutex m_mutex;
    std::condition_variable m_cond_pop;
    std::condition_variable m_cond_push;
    size_t m_maxSize;
    bool m_stopped;
};

#endif // THREAD_SAFE_QUEUE_H