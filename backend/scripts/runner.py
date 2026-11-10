"""Trusted container entry point. The host never executes uploaded Python."""
import signal
import sys
import os
import select
import threading

sys.path.insert(0, '/opt/asteroid')
import asteroid

signal.alarm(3600)  # Container also exits if the host process disappears.
source = asteroid.receive().decode('utf-8')
code = compile(source, '<ship-script>', 'exec')


def watch_server():
    # Observe EOF without consuming protocol data. Docker forwards CLI stdin EOF.
    poller = select.poll()
    poller.register(sys.stdin.fileno(), select.POLLHUP | select.POLLERR)
    poller.poll()
    os._exit(0)


threading.Thread(target=watch_server, daemon=True).start()
sys.stdout = sys.stderr  # print() is a log; it must not corrupt framed stdout.
asteroid.send(b'')  # Startup handshake, consumed by ScriptRuntime.
exec(code, {'__name__': '__main__', '__file__': '<ship-script>'})
