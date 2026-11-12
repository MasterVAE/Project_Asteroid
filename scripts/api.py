
import concurrent.futures
import itertools
import json
import os
import queue
import sys
import threading
import traceback

_handlers = {}
_pending = {}
_pending_lock = threading.Lock()
_send_lock = threading.Lock()
_next_id = itertools.count(1)
_events = queue.Queue()
_running = threading.Event()
_started = False

# Keep RPC on its own descriptor so print(), native writes and child processes
# can all use stdout without mixing their output with protocol packets.
sys.stdout.flush()
_protocol_stdout = os.fdopen(os.dup(sys.stdout.fileno()), "w", encoding="utf-8")
os.dup2(sys.stderr.fileno(), sys.stdout.fileno())
sys.stdout = sys.stderr

# Системная фигня


def on(name):
    """Регистрация обработчика события."""
    def register(callback):
        _handlers[name] = callback
        return callback
    return register


def _send(message):
    raw = json.dumps(message) + "\n"
    with _send_lock:
        _protocol_stdout.write(raw)
        _protocol_stdout.flush()


def command(name, module = "none", **params):
    """Команда C++ без ожидания ответа."""
    _send({
        "type": "command",
        "module" : module,
        "method": name,
        **params
    })


def _request(method, module="none", timeout=2.0):
    """Вызов функции в C++ с ожиданием ответа."""
    if not _running.is_set():
        raise RuntimeError("Connection to C++ closed")

    request_id = next(_next_id)
    future = concurrent.futures.Future()

    # Запоминаем ожидающий запрос
    with _pending_lock:
        _pending[request_id] = future

    try:
        _send({
            "type": "request",
            "id": request_id,
            "module" : module,
            "method": method
        })

        # Ждём только ответ на этот запрос
        response = future.result(timeout=timeout)

    except concurrent.futures.TimeoutError:
        raise TimeoutError(
            f"C++ did not answer {method}"
        ) from None

    finally:
        with _pending_lock:
            _pending.pop(request_id, None)

    if "error" in response:
        raise RuntimeError(response["error"])

    return response["result"]



def _read_messages():
    """Отдельный поток читает всё из C++."""
    try:
        for line in sys.stdin:
            try:
                message = json.loads(line)
                kind = message.get("type")

                if kind == "response":
                    request_id = message.get("id")

                    with _pending_lock:
                        future = _pending.pop(
                            request_id, None
                        )

                    if future is not None:
                        future.set_result(message)

                elif kind == "event":
                    _events.put((
                        message["name"],
                        message.get("data", {})
                    ))

                elif kind == "shutdown":
                    break

            except Exception:
                traceback.print_exc(file=sys.stderr)

    finally:
        _running.clear()

        with _pending_lock:
            outstanding = list(_pending.values())
            _pending.clear()

        for future in outstanding:
            future.set_exception(
                RuntimeError("Connection closed")
            )

        _events.put(None)


def _dispatch_events():
    """Обработчики работают не в потоке чтения."""
    while True:
        event = _events.get()

        if event is None:
            return

        name, payload = event
        callback = _handlers.get(name)

        if callback is not None:
            try:
                callback(payload)
            except Exception:
                traceback.print_exc(file=sys.stderr)


def start():
    global _started

    if _started:
        raise RuntimeError("Already started")

    _started = True
    _running.set()

    threading.Thread(
        target=_read_messages,
        daemon=True
    ).start()

    threading.Thread(
        target=_dispatch_events,
        daemon=True
    ).start()


def is_running():
    return _running.is_set()


# Запросы

def getId(timeout=2.0):
    return _request("get_id", timeout=timeout)

def getPos(timeout=2.0):
    return _request("get_position", timeout=timeout)

def getVel(timeout=2.0):
    return _request("get_velocity", timeout=timeout)

def getAngle(timeout=2.0):
    return _request("get_angle", timeout=timeout)

def getAngularVelocity(timeout=2.0):
    return _request("get_angular_velocity", timeout=timeout)

def getModules(timeout=2.0):
    return _request("get_modules", timeout=timeout)



# модули

class Prop:
    name = "prop"
    def __init__(self, name):
        self.name = name

    def getMaxThrottle(self, timeout=2.0):
        return _request("get_max_throttle", module=self.name, timeout=timeout)

    def throttle(self, value=1.0):
        command("set_throttle", module=self.name, value=value)


class RCS:
    name = "rcs"
    def __init__(self, name):
            self.name = name

    def getMaxGyro(self, timeout=2.0):
        return _request("get_max_gyro", module=self.name, timeout=timeout)

    def gyro(self, value=1.0):
        command("set_gyro", module=self.name, value=value)

    def getThrust(self, timeout=2.0):
        return _request("get_max_thrust", module=self.name, timeout=timeout)

    def setThrust(self, value=(0, 0)):
        command("set_thrust", module=self.name, value=value)