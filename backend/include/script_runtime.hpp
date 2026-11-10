#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Every instance owns one isolated Docker container. No host-Python fallback.
class ScriptRuntime {
public:
    enum class State : uint8_t { None, Starting, Running, Stopped, Exited, Failed };
    explicit ScriptRuntime(const std::string& source);
    ~ScriptRuntime();
    ScriptRuntime(const ScriptRuntime&) = delete;
    ScriptRuntime& operator=(const ScriptRuntime&) = delete;
    State state() const { return state_.load(); }
    std::string log() const;
    bool Send(const std::string& bytes);
    std::vector<std::string> TakeMessages();
    void Stop(); // Nonblocking. Destructor joins worker and removes container.
private:
    void Run(std::string source);
    void AppendLog(const char* bytes, size_t size);
    std::atomic<State> state_{State::Starting};
    std::atomic<bool> stop_{false};
    mutable std::mutex mutex_;
    std::deque<std::string> outgoing_, incoming_;
    size_t outgoing_bytes_ = 0, incoming_bytes_ = 0;
    std::string log_;
    std::thread worker_;
};
