#include "script_runtime.hpp"
#include <cassert>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>

using namespace std::chrono_literals;
template<class Predicate> void until(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!predicate()) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(10ms);
    }
}
int main(int argc, char**) {
    std::signal(SIGPIPE, SIG_IGN);
    if (argc > 1) {
        ScriptRuntime script(R"PY(
import asteroid, os, socket
assert os.getuid() == 65534
assert os.statvfs('/').f_flag & os.ST_RDONLY
with open('/tmp/test', 'w') as file:
    file.write('temporary')
sock = socket.socket()
sock.settimeout(0.2)
try:
    sock.connect(('1.1.1.1', 53))
except OSError:
    pass
else:
    raise AssertionError('Network access is enabled')
finally:
    sock.close()
asteroid.send(b'hello')
assert asteroid.receive() == b'server reply'
print('sandbox roundtrip ok')
)PY");
        std::vector<std::string> received;
        until([&] {
            received = script.TakeMessages();
            if (script.state() == ScriptRuntime::State::Failed) {
                std::cerr << script.log() << '\n';
                assert(false);
            }
            return !received.empty();
        });
        assert(received == std::vector<std::string>{"hello"});
        assert(script.Send("server reply"));
        until([&] { return script.state() == ScriptRuntime::State::Exited; });
        assert(script.log().find("sandbox roundtrip ok") != std::string::npos);
        std::cout << "Real Docker sandbox tests passed\n";
        return 0;
    }
    const auto mock = std::filesystem::absolute("tests/fake_docker.py").string();
    setenv("ASTEROID_DOCKER", mock.c_str(), 1);
    {
        ScriptRuntime script("echo");
        until([&] { return script.state() == ScriptRuntime::State::Running; });
        const std::string payload(1024 * 1024, 'x');
        assert(script.Send(payload));
        std::vector<std::string> received;
        until([&] { received = script.TakeMessages(); return !received.empty(); });
        assert(received.size() == 1 && received.front() == payload);
        assert(!script.Send(std::string(payload.size() + 1, 'x')));
        script.Stop();
        until([&] { return script.state() == ScriptRuntime::State::Stopped; });
        assert(!script.Send("stopped"));
    }
    for (const auto* source : {"fail", "oversize", "partial", "flood"}) {
        ScriptRuntime script(source);
        until([&] { return script.state() == ScriptRuntime::State::Failed; });
        if (std::string(source) != "partial") assert(!script.log().empty());
    }
    {
        ScriptRuntime script("final");
        until([&] { return script.state() == ScriptRuntime::State::Exited; });
        const auto received = script.TakeMessages();
        assert(received.size() == 1 && received[0] == std::string(100000, 'x'));
    }
    std::cout << "Script transport tests passed (mock Docker, no sandbox execution)\n";
}
