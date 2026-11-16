
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

def getId(timeout=2.0) -> int:
    """Возвращает ID корабля."""
    return int(_request("get_id", timeout=timeout)["craft_id"])

def getName(timeout=2.0) -> str:
    """Возвращает текущее имя корабля из Entity.GetName()."""
    return _request("get_name", timeout=timeout)["name"]

def setName(name):
    """Переименовывает свой корабль; команда без ожидания подтверждения."""
    if not isinstance(name, str) or not name.strip():
        raise ValueError("Craft name must be a non-empty string")
    command("set_name", value=name)

def saveData(data):
    """Сохраняет строку в корабле; при запуске скрипта она приходит событием load_data."""
    if not isinstance(data, str):
        raise ValueError("Script data must be a string")
    command("save_data", value=data)

def getPos(timeout=2.0) -> tuple:
    """Возвращает мировые координаты корабля как (x, y)."""
    result = _request("get_position", timeout=timeout)
    return float(result["x"]), float(result["y"])

def getVel(timeout=2.0) -> tuple:
    """Возвращает мировую скорость корабля как (vx, vy)."""
    result = _request("get_velocity", timeout=timeout)
    return float(result["x"]), float(result["y"])

def getAngle(timeout=2.0) -> float:
    """Возвращает угол корабля в радианах."""
    return float(_request("get_angle", timeout=timeout)["angle"])

def getAngularVelocity(timeout=2.0) -> float:
    """Возвращает угловую скорость корабля в рад/с."""
    return float(_request("get_angular_velocity", timeout=timeout)["angular_velocity"])

def getModules(timeout=2.0) -> list:
    """Возвращает список имён установленных модулей."""
    return _request("get_modules", timeout=timeout)["modules"]

def getMass(timeout=2.0) -> float:
    """Возвращает массу корабля с грузом."""
    return float(_request("get_mass", timeout=timeout)["mass"])

# модули

class Propulsion:
    name = "propulsion"
    def __init__(self, name):
        self.name = name

    def getMaxThrottle(self, timeout=2.0) -> float:
        """Возвращает максимальную силу тяги; ускорение равно силе / массе."""
        return float(_request("get_max_throttle", module=self.name, timeout=timeout)["max_throttle"])

    def getFuelConsumption(self, timeout=2.0) -> float:
        """Возвращает расход топлива в секунду при полной тяге."""
        return float(_request("get_fuel_consumption", module=self.name, timeout=timeout)["fuel_consumption"])

    def throttle(self, value=1.0):
        command("set_throttle", module=self.name, value=value)


class Storage:
    name = "storage"
    def __init__(self, name):
        self.name = name

    def getStorage(self, timeout=2.0) -> dict:
        return {
            item["resource"]: item["count"]
            for item in _request("get_storage", module=self.name, timeout=timeout)
        }

    def readyToRecieve(self):
        command("ready_to_recieve", module=self.name)

    def notReadyToRecieve(self):
        command("not_ready_to_recieve", module=self.name)

    def transfer(self, id, resource):
        command("transfer", module=self.name, value=(id, resource))



class RCS:
    name = "rcs"
    def __init__(self, name):
        self.name = name

    def getMaxGyro(self, timeout=2.0) -> float:
        """Возвращает максимальную угловую скорость в рад/с."""
        return float(_request("get_max_gyro", module=self.name, timeout=timeout)["max_gyro"])

    def gyro(self, value=1.0):
        command("set_gyro", module=self.name, value=value)

    def getThrust(self, timeout=2.0) -> float:
        """Возвращает максимальную силу маневровой тяги; ускорение равно силе / массе."""
        return float(_request("get_max_thrust", module=self.name, timeout=timeout)["max_thrust"])

    def setThrust(self, value=(0, 0)):
        command("set_thrust", module=self.name, value=value)

class Drill:
    name = "drill"

    def __init__(self, name):
        self.name = name

    def getDrillCooldown(self, timeout=2.0) -> float:
        """Возвращает интервал добычи в секундах работы с питанием."""
        return float(_request("get_drill_cooldown", module=self.name, timeout=timeout)["drill_cooldown"])

    def enable(self):
        """Включает бур."""
        command("enable", module=self.name)

    def disable(self):
        """Выключает бур."""
        command("disable", module=self.name)


class Scanner:
    name = "scanner"

    def __init__(self, name):
        self.name = name

    def scan(self, timeout=2.0) -> list:
        """Объекты с полями id, name, type (craft/asteroid), x, y; без питания — []."""
        return _request("scan", module=self.name, timeout=timeout)

    def signal(self, craft_id, message):
        """Отправляет событие signal с sender_id и message обработчику @on('signal')."""
        if isinstance(craft_id, bool) or not isinstance(craft_id, int) or craft_id < 0:
            raise ValueError("Craft ID must be a non-negative integer")
        if not isinstance(message, str):
            raise ValueError("Signal message must be a string")
        command("signal", module=self.name, value=(craft_id, message))


class Power:
    name = "power"
    def __init__(self, name):
        self.name = name
    def getCapacity(self, timeout=2.0) -> float:
        return float(_request("get_capacity", module=self.name, timeout=timeout)["capacity"])
    def getCharge(self, timeout=2.0) -> float:
        return float(_request("get_charge", module=self.name, timeout=timeout)["charge"])


class FuelTank:
    name = "tank"

    def __init__(self, name):
        self.name = name

    def getMaxFuel(self, timeout=2.0) -> float:
        """Возвращает вместимость топливного бака."""
        return float(_request("get_max_fuel", module=self.name, timeout=timeout)["max_fuel"])

    def getFuel(self, timeout=2.0) -> float:
        """Возвращает текущий запас топлива в баке."""
        return float(_request("get_fuel", module=self.name, timeout=timeout)["fuel"])


class Factory:
    name = "factory"

    def __init__(self, name):
        self.name = name

    def getRecipes(self, timeout=2.0) -> list:
        """Возвращает все рецепты с input/output; индекс в списке используется в produce."""
        return _request("get_recipes", module=self.name, timeout=timeout)

    def getQueue(self, timeout=2.0) -> list:
        """Очередь рецептов с input/output в порядке выполнения, включая текущий заказ."""
        return _request("get_queue", module=self.name, timeout=timeout)

    def produce(self, index):
        """Добавляет одну операцию по рецепту с указанным индексом в очередь завода."""
        if isinstance(index, bool) or not isinstance(index, int) or index < 0:
            raise ValueError("Recipe index must be a non-negative integer")
        command("produce", module=self.name, value=index)


class Shipyard:
    name = "shipyard"

    def __init__(self, name):
        self.name = name

    def getRecipes(self, timeout=2.0) -> list:
        """Рецепты верфи: input — список ресурсов, output — craft или имя типа модуля."""
        return _request("get_recipes", module=self.name, timeout=timeout)

    def getQueue(self, timeout=2.0) -> list:
        """Очередь заказов с output и craft_id (None для нового корабля), включая текущий."""
        return _request("get_queue", module=self.name, timeout=timeout)

    def constructCraft(self):
        """Ставит строительство пустого корабля в очередь; команда без подтверждения."""
        command("construct_craft", module=self.name)

    def addModule(self, craft_id, module_name):
        """Ставит установку модуля на корабль в радиусе 20 в очередь верфи."""
        if isinstance(craft_id, bool) or not isinstance(craft_id, int) or craft_id < 0:
            raise ValueError("Craft ID must be a non-negative integer")
        if not isinstance(module_name, str) or not module_name:
            raise ValueError("Module name must be a non-empty string, e.g. 'storage'")
        command("add_module", module=self.name, value=(craft_id, module_name))
