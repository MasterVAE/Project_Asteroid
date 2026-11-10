#include "script_runtime.hpp"
#include "protocol.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
namespace {
constexpr size_t QueueLimit = 4 * 1024 * 1024;
std::atomic<unsigned> active{0};
std::atomic<unsigned> serial{0};
using Clock = std::chrono::steady_clock;
std::string setting(const char* name, const char* fallback) {
    const auto* value = std::getenv(name);
    return value && *value ? value : fallback;
}
std::string frame(const std::string& bytes) {
    std::string result(4, '\0');
    for (int i = 0; i < 4; ++i) result[i] = char((bytes.size() >> (8 * i)) & 255);
    result += bytes;
    return result;
}
pid_t spawn(std::vector<std::string> args, int input, int output, int errors) {
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, input, STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, output, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, errors, STDERR_FILENO);
#if defined(__linux__)
    posix_spawn_file_actions_addclosefrom_np(&actions, 3);
#endif
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
#ifdef __APPLE__
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_CLOEXEC_DEFAULT);
#endif
    std::vector<char*> argv;
    for (auto& arg : args) argv.push_back(arg.data());
    argv.push_back(nullptr);
    pid_t pid = -1;
    const int error = posix_spawnp(&pid, argv[0], &actions, &attributes, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    if (error) throw std::runtime_error(std::string("Docker launch: ") + std::strerror(error));
    return pid;
}
void reap(pid_t pid) {
    if (pid <= 0) return;
    const auto deadline = Clock::now() + std::chrono::seconds(3);
    while (waitpid(pid, nullptr, WNOHANG) == 0) {
        if (Clock::now() >= deadline) {
            kill(pid, SIGKILL);
            while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}
}

ScriptRuntime::ScriptRuntime(const std::string& source) {
    if (source.empty() || source.size() > protocol::MaxLongStringBytes)
        throw std::invalid_argument("Скрипт должен содержать от 1 байта до 1 МиБ.");
    if (active.fetch_add(1) >= 32) {
        --active;
        throw std::invalid_argument("Достигнут лимит 32 скриптов на сервере.");
    }
    try { worker_ = std::thread([this, source] { Run(source); }); }
    catch (...) { --active; throw; }
}
ScriptRuntime::~ScriptRuntime() {
    Stop();
    if (worker_.joinable()) worker_.join();
}
void ScriptRuntime::Stop() {
    stop_ = true;
    std::lock_guard<std::mutex> lock(mutex_);
    incoming_.clear(); outgoing_.clear();
    incoming_bytes_ = outgoing_bytes_ = 0;
}
std::string ScriptRuntime::log() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return log_;
}
void ScriptRuntime::AppendLog(const char* bytes, size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Keep arbitrary stderr bytes safe for the browser's strict UTF-8 decoder.
    for (size_t i = 0; i < size; ++i) {
        const auto c = static_cast<unsigned char>(bytes[i]);
        log_ += (c == '\n' || c == '\t' || (c >= 32 && c < 127)) ? char(c) : '?';
    }
    if (log_.size() > 4096) log_.erase(0, log_.size() - 4096);
}
bool ScriptRuntime::Send(const std::string& bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stop_ || (state() != State::Running && state() != State::Starting) ||
        bytes.size() > protocol::MaxLongStringBytes || outgoing_bytes_ + bytes.size() + 4 > QueueLimit)
        return false;
    outgoing_.push_back(frame(bytes));
    outgoing_bytes_ += bytes.size() + 4;
    return true;
}
std::vector<std::string> ScriptRuntime::TakeMessages() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> messages;
    if (stop_) return messages;
    // Bound the work performed during one world tick.
    while (!incoming_.empty() && messages.size() < 16) {
        incoming_bytes_ -= incoming_.front().size() + 4;
        messages.push_back(std::move(incoming_.front()));
        incoming_.pop_front();
    }
    return messages;
}
void ScriptRuntime::Run(std::string source) {
    const auto docker = setting("ASTEROID_DOCKER", "docker");
    const auto name = "asteroid-script-" + std::to_string(getpid()) + "-" +
        std::to_string(Clock::now().time_since_epoch().count()) + "-" + std::to_string(++serial);
    std::array<int, 6> fds{-1, -1, -1, -1, -1, -1};
    pid_t child = -1;
    bool launched = false;
    try {
        for (int i = 0; i < 6; i += 2) {
            int pair[2];
            if (pipe(pair) != 0) throw std::runtime_error("Cannot create script pipes");
            fds[i] = pair[0]; fds[i + 1] = pair[1];
            fcntl(pair[0], F_SETFD, FD_CLOEXEC); fcntl(pair[1], F_SETFD, FD_CLOEXEC);
        }
        child = spawn({docker, "run", "--rm", "--pull=never", "--name", name, "-i",
            "--network=none", "--read-only", "--cap-drop=ALL", "--security-opt=no-new-privileges",
            "--user=65534:65534", "--memory=128m", "--memory-swap=128m", "--cpus=0.25",
            "--pids-limit=16", "--ulimit=nofile=64:64", "--ulimit=core=0:0", "--shm-size=1m",
            "--tmpfs=/tmp:rw,noexec,nosuid,nodev,size=8m", "--log-driver=none",
            "--label=asteroid.script=true", setting("ASTEROID_SCRIPT_IMAGE", "asteroid-python:local")},
            fds[0], fds[3], fds[5]);
        launched = true;
        for (int i : {0, 3, 5}) { close(fds[i]); fds[i] = -1; }
        for (int i : {1, 2, 4}) fcntl(fds[i], F_SETFL, O_NONBLOCK);
        std::string pending = frame(source), received;
        source.clear();
        size_t sent = 0, stderr_bytes = 0;
        unsigned messages = 0;
        bool ready = false, stdout_closed = false, stderr_closed = false, exited = false;
        int exit_status = 0;
        auto started = Clock::now(), window = started;
        State final_state = State::Stopped;
        while (!stop_) {
            const auto now = Clock::now();
            if ((!ready && now - started > std::chrono::seconds(10)) ||
                now - started > std::chrono::hours(1)) throw std::runtime_error("Script time limit exceeded");
            if (now - window >= std::chrono::seconds(1)) {
                window = now; messages = 0; stderr_bytes = 0;
            }
            if (pending.empty()) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!outgoing_.empty()) {
                    pending = std::move(outgoing_.front()); outgoing_.pop_front();
                    outgoing_bytes_ -= pending.size(); sent = 0;
                }
            }
            pollfd events[] = {{stdout_closed ? -1 : fds[2], POLLIN, 0},
                              {stderr_closed ? -1 : fds[4], POLLIN, 0},
                               {fds[1], short(pending.empty() ? 0 : POLLOUT), 0}};
            if (poll(events, 3, 20) < 0 && errno != EINTR) throw std::runtime_error("Script poll failed");
            if (events[2].revents & POLLOUT) {
                const auto count = write(fds[1], pending.data() + sent, pending.size() - sent);
                if (count > 0) {
                    sent += size_t(count);
                    if (sent == pending.size()) { pending.clear(); sent = 0; }
                } else if (count < 0 && errno != EAGAIN && errno != EINTR) {
                    throw std::runtime_error("Script input pipe closed");
                }
            }
            for (int i = 0; i < 2; ++i) {
                if (!(events[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
                char buffer[4096];
                const auto count = read(events[i].fd, buffer, sizeof(buffer));
                if (count < 0 && errno != EAGAIN && errno != EINTR) throw std::runtime_error("Script pipe read failed");
                if (count == 0 && i == 0) stdout_closed = true;
                if (count == 0 && i == 1) stderr_closed = true;
                if (count <= 0) continue;
                if (i == 1) {
                    stderr_bytes += size_t(count);
                    if (stderr_bytes > 65536) throw std::runtime_error("Script log rate exceeded");
                    AppendLog(buffer, size_t(count));
                } else received.append(buffer, size_t(count));
            }
            while (received.size() >= 4) {
                uint32_t size = 0;
                for (int i = 0; i < 4; ++i) size |= uint32_t(uint8_t(received[i])) << (8 * i);
                if (size > protocol::MaxLongStringBytes) throw std::runtime_error("Script message exceeds 1 MiB");
                if (received.size() < size + 4) break;
                auto message = received.substr(4, size);
                received.erase(0, size + 4);
                if (!ready) {
                    if (!message.empty()) throw std::runtime_error("Invalid script startup handshake");
                    ready = true; state_ = State::Running;
                } else {
                    if (++messages > 128) throw std::runtime_error("Script message rate exceeded");
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (incoming_bytes_ + size + 4 > QueueLimit) throw std::runtime_error("Script message queue full");
                    incoming_bytes_ += size + 4;
                    incoming_.push_back(std::move(message));
                }
            }
            int status = 0;
            const auto result = child > 0 ? waitpid(child, &status, WNOHANG) : 0;
            if (result == child) {
                child = -1;
                exited = true;
                exit_status = status;
            }
            if (exited && stdout_closed && stderr_closed) {
                final_state = ready && received.empty() && WIFEXITED(exit_status) && WEXITSTATUS(exit_status) == 0
                    ? State::Exited : State::Failed;
                break;
            }
        }
        state_ = stop_ ? State::Stopped : final_state;
    } catch (const std::exception& error) {
        AppendLog(error.what(), std::strlen(error.what()));
        state_ = stop_ ? State::Stopped : State::Failed;
    }
    for (int fd : fds) if (fd >= 0) close(fd);
    if (launched) {
        // Killing only the Docker CLI would leave its container running.
        const int null = open("/dev/null", O_RDWR);
        if (null >= 0) {
            try { reap(spawn({docker, "rm", "--force", name}, null, null, null)); }
            catch (...) {}
            close(null);
        }
    }
    reap(child);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        outgoing_.clear(); outgoing_bytes_ = 0;
    }
    --active;
}
