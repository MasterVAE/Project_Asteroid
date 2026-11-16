"""Shared RCS navigation for the two one-shot Miner examples; importing does not start a script."""

import math
import threading
import time

import api as ship


STEP = 0.05
BASE_ID = 0


class DemoPilot:
    def __init__(self):
        self.cancelled = threading.Event()

        @ship.on("console_message")
        def on_message(message):
            if message.strip().lower() == "stop":
                self.cancelled.set()

        ship.start()
        modules = ship.getModules()

        def name(kind):
            matches = [module for module in modules if module.startswith(kind + "_")]
            if not matches:
                raise RuntimeError(f"Нужен модуль {kind}; начните новую игру или установите его на верфи")
            return matches[0]

        self.propulsion = ship.Propulsion(name("propulsion"))
        self.rcs = ship.RCS(name("rcs"))
        self.drill = ship.Drill(name("drill"))
        self.stop_controls()
        self.grab = ship.Grab(name("grab"))
        self.scanner = ship.Scanner(name("scanner"))
        self.power = ship.Power(name("power"))
        self.storage = ship.Storage(name("storage"))
        ship.readyToConnect(False)
        if ship.getConnection()["connected"] is not None:
            raise RuntimeError("Перед запуском расстыкуйте Miner")
        if self.grab.getGrabbed() is not None:
            raise RuntimeError("Перед запуском освободите grab_0")
        self.max_thrust = self.rcs.getThrust()
        self.base_position = None
        self.asteroid_offset = (0.0, 0.0)

    def ensure_running(self):
        if not ship.is_running():
            raise RuntimeError("Соединение с игрой закрыто")
        if self.cancelled.is_set():
            raise RuntimeError("Остановлено командой stop")

    def wait_until(self, predicate, timeout, message):
        deadline = time.monotonic() + timeout
        while True:
            self.ensure_running()
            if predicate():
                return
            if time.monotonic() >= deadline:
                raise RuntimeError(message)
            time.sleep(STEP)

    def stop_controls(self):
        if ship.is_running():
            self.propulsion.throttle(0)
            self.rcs.gyro(0)
            self.rcs.setThrust((0, 0))
            self.drill.disable()

    def scan(self):
        self.wait_until(lambda: self.power.getCharge() >= 30, 60, "Не хватает энергии для сканирования")
        objects = self.scanner.scan()
        for obj in objects:
            if obj["type"] == "craft" and obj["id"] == BASE_ID:
                self.base_position = (obj["x"], obj["y"])
        return objects

    def choose_asteroid(self):
        objects = self.scan()
        if self.base_position is None:
            raise RuntimeError("Стартовая база с ID 0 не найдена в радиусе сканера")
        position = ship.getPos()
        asteroids = [obj for obj in objects if obj["type"] == "asteroid"]
        if not asteroids:
            raise RuntimeError("В радиусе 500 нет астероидов")
        return min(asteroids, key=lambda obj: math.hypot(obj["x"] - position[0], obj["y"] - position[1]))

    def steer_velocity(self, target_velocity):
        """Convert a bounded world acceleration into the RCS local thrust vector."""
        self.ensure_running()
        vx, vy = ship.getVel()
        acceleration = self.max_thrust / ship.getMass()
        tx = (target_velocity[0] - vx) / (0.75 * acceleration)
        ty = (target_velocity[1] - vy) / (0.75 * acceleration)
        length = math.hypot(tx, ty)
        if length > 1:
            tx, ty = tx / length, ty / length
        angle = ship.getAngle()
        cosine, sine = math.cos(angle), math.sin(angle)
        self.rcs.setThrust((cosine * tx + sine * ty, -sine * tx + cosine * ty))

    def go_to(self, target, max_speed=10.0, timeout=300):
        print(f"Перелёт к ({target[0]:.1f}, {target[1]:.1f})")
        deadline = time.monotonic() + timeout
        try:
            while True:
                self.ensure_running()
                x, y = ship.getPos()
                vx, vy = ship.getVel()
                dx, dy = target[0] - x, target[1] - y
                distance = math.hypot(dx, dy)
                if distance <= 0.2 and math.hypot(vx, vy) <= 0.03:
                    return
                if time.monotonic() >= deadline:
                    raise RuntimeError("Перелёт не завершён; проверьте, что игра не на паузе")
                acceleration = self.max_thrust / ship.getMass()
                speed = min(max_speed, 0.65 * math.sqrt(2 * acceleration * distance), distance)
                velocity = (dx / distance * speed, dy / distance * speed) if distance else (0, 0)
                self.steer_velocity(velocity)
                time.sleep(STEP)
        finally:
            if ship.is_running():
                self.rcs.setThrust((0, 0))

    def brake(self):
        try:
            def stopped():
                self.steer_velocity((0, 0))
                return math.hypot(*ship.getVel()) <= 0.03

            self.wait_until(stopped, 120, "Не удалось затормозить")
        finally:
            if ship.is_running():
                self.rcs.setThrust((0, 0))

    def capture(self, asteroid):
        self.go_to((asteroid["x"], asteroid["y"]))
        self.grab.grab(asteroid["id"])
        self.wait_until(lambda: self.grab.getGrabbed() == asteroid["id"], 5,
                        "Захват не выполнен: астероид занят или вышел из радиуса")
        # Refresh the actual attachment offset after the command has taken effect.
        for obj in self.scan():
            if obj["id"] == asteroid["id"] and obj["type"] == "asteroid":
                x, y = ship.getPos()
                self.asteroid_offset = (obj["x"] - x, obj["y"] - y)
                print(f"Захвачен астероид #{asteroid['id']}; общая масса {ship.getMass():.2f}")
                return
        raise RuntimeError("Захваченный астероид не найден сканером")

    def dock_at_base(self):
        self.go_to(self.base_position)
        # Correct for any base drift since the first scan.
        for _ in range(3):
            objects = self.scan()
            if not any(obj["type"] == "craft" and obj["id"] == BASE_ID for obj in objects):
                raise RuntimeError("База не найдена у точки возвращения")
            self.go_to(self.base_position, max_speed=2)
            if math.dist(ship.getPos(), self.base_position) <= 0.5:
                break
        def try_connect():
            if ship.getConnection()["connected"] == BASE_ID:
                return True
            ship.connect(BASE_ID)
            return False

        self.wait_until(try_connect, 10,
                        "База не готова к стыковке или её узел занят")
        print("Miner пристыкован к базе")


def run_demo(main):
    pilot = None
    try:
        pilot = DemoPilot()
        main(pilot)
    except (RuntimeError, TimeoutError) as error:
        print(f"Сценарий остановлен: {error}")
    finally:
        if pilot is not None and ship.is_running():
            try:
                pilot.stop_controls()
            except (RuntimeError, TimeoutError):
                pass
