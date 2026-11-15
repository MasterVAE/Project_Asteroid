"""Запускать на Miner #1. Добывает сырьё и доставляет его на Shipyard #0."""

import math
import queue
import time

import api as ship

BASE_ID = 0
BASE_POSITION = (0.0, 0.0)
CARGO_LIMIT = 15  # Вместимость стандартного склада — 20; оставляем запас.
TICK = 0.1
_commands = queue.Queue()


@ship.on("console_message")
def on_message(message):
    _commands.put(str(message).strip().lower())


class Miner:
    def __init__(self):
        names = ship.getModules()

        def module(kind, cls):
            name = next((name for name in names if name.startswith(kind + "_")), None)
            if name is None:
                raise RuntimeError(f"На корабле нет модуля {kind}; запустите скрипт на добытчике")
            return cls(name)

        self.prop = module("propulsion", ship.Propulsion)
        self.rcs = module("rcs", ship.RCS)
        self.storage = module("storage", ship.Storage)
        self.drill = module("drill", ship.Drill)
        self.scaner = module("scaner", ship.Scaner)
        self.power = module("power", ship.Power)
        self.rcs_force = self.rcs.getThrust()
        self.prop_force = self.prop.getMaxThrottle()
        self.targets = []
        self.state = "запуск"
        self.home = False
        self.exploration = 0
        self.stop_equipment()

    def poll(self):
        if not ship.is_running():
            raise InterruptedError("Соединение закрыто")
        while not _commands.empty():
            command = _commands.get_nowait()
            if command == "stop":
                raise InterruptedError("Остановлено командой stop")
            if command == "home":
                self.home = True
            elif command == "status":
                print(f"[Miner] {self.state}; груз: {self.storage.getStorage()}; "
                      f"заряд: {self.power.getCharge():.1f}")

    def stop_equipment(self):
        self.prop.throttle(0)
        self.rcs.gyro(0)
        self.rcs.setThrust((0, 0))
        self.drill.disable()

    def steer(self, target):
        """Торможение рассчитано по силе RCS и текущей массе; тяга задана локально."""
        x, y = ship.getPos()
        vx, vy = ship.getVel()
        angle = ship.getAngle()
        mass = ship.getMass()
        dx, dy = target[0] - x, target[1] - y
        distance = math.hypot(dx, dy)
        acceleration = self.rcs_force / mass
        speed = min(6.0, math.sqrt(0.8 * acceleration * max(0, distance - 0.15)))
        desired_vx = dx / distance * speed if distance > 0.2 else 0
        desired_vy = dy / distance * speed if distance > 0.2 else 0
        ax, ay = desired_vx - vx, desired_vy - vy
        c, s = math.cos(angle), math.sin(angle)
        thrust = 0.0
        if distance > 15 and math.hypot(ax, ay) > 0.01:
            error = math.remainder(math.atan2(ay, ax) - angle, 2 * math.pi)
            self.rcs.gyro(max(-1, min(1, 2 * error)))
            if abs(error) < 0.1:
                thrust = max(0, min(1, (ax * c + ay * s) * mass / self.prop_force))
        else:
            self.rcs.gyro(0)
        self.prop.throttle(thrust)
        ax -= thrust * self.prop_force / mass * c
        ay -= thrust * self.prop_force / mass * s
        self.rcs.setThrust(((ax * c + ay * s) / acceleration,
                           (-ax * s + ay * c) / acceleration))
        return distance < 0.35 and math.hypot(vx, vy) < 0.04

    def fly_to(self, target, returning=False):
        self.state = f"перелёт к {target}"
        print(f"[Miner] {self.state}")
        distance = math.dist(ship.getPos(), target)
        deadline = time.monotonic() + max(120, distance * 3 + 60)
        while True:
            self.poll()
            if self.home and not returning:
                self.stop_equipment()
                return False
            if self.steer(target):
                self.stop_equipment()
                return True
            if time.monotonic() > deadline:
                raise RuntimeError(f"Не удалось долететь до {target}")
            time.sleep(TICK)

    def scan(self):
        self.state = "ожидание энергии для сканирования"
        self.drill.disable()
        while self.power.getCharge() < 35:
            self.poll()
            if self.home:
                return
            time.sleep(TICK)
        objects = self.scaner.scan()
        # Пустой ответ не стирает цель во время бурения: сканируем только между целями.
        self.targets = [obj for obj in objects if obj["type"] == "asteroid"]
        print(f"[Miner] Сканирование: найдено астероидов {len(self.targets)}")

    def mine(self, target):
        position = (target["x"], target["y"])
        if not self.fly_to(position):
            return
        self.state = f"добыча на {target['name']}"
        print(f"[Miner] {self.state}")
        self.drill.enable()
        count = sum(self.storage.getStorage().values())
        last_gain = time.monotonic()
        try:
            while count < CARGO_LIMIT and not self.home:
                self.poll()
                self.steer(position)
                time.sleep(TICK)
                current = sum(self.storage.getStorage().values())
                if current > count:
                    count = current
                    last_gain = time.monotonic()
                elif time.monotonic() - last_gain > 3:
                    # Астероид исчерпан либо добыча остановилась: не тратим энергию впустую.
                    break
        finally:
            self.stop_equipment()
        print(f"[Miner] Груз: {self.storage.getStorage()}")

    def unload(self):
        self.drill.disable()
        self.fly_to((BASE_POSITION[0], BASE_POSITION[1] + 6), returning=True)
        self.state = "разгрузка"
        while True:
            self.poll()
            cargo = self.storage.getStorage()
            if not cargo:
                break
            for resource, count in cargo.items():
                for _ in range(count):
                    self.storage.transfer(BASE_ID, resource)
            time.sleep(0.5)
            if self.storage.getStorage() == cargo:
                print("[Miner] Верфь не принимает груз: проверьте свободное место и приём")
                time.sleep(2)
        self.home = False
        print("[Miner] Разгрузка завершена")

    def run(self, max_deliveries=None):
        deliveries = 0
        while ship.is_running():
            self.poll()
            cargo = sum(self.storage.getStorage().values())
            if self.home or cargo >= CARGO_LIMIT or (cargo and not self.targets):
                self.unload()
                deliveries += 1
                if max_deliveries is not None and deliveries >= max_deliveries:
                    return
            if not self.targets:
                self.scan()
            if self.home:
                continue
            if not self.targets:
                # Обследуем центр, стороны и углы области генерации астероидов.
                waypoints = [(600, 0), (700, 700), (0, 600), (-700, 700),
                             (-600, 0), (-700, -700), (0, -600), (700, -700), (0, 0)]
                target = waypoints[self.exploration % len(waypoints)]
                self.exploration += 1
                self.fly_to(target)
                continue
            position = ship.getPos()
            target = min(self.targets, key=lambda obj: math.dist(position, (obj["x"], obj["y"])))
            self.targets.remove(target)
            self.mine(target)


def main(max_deliveries=None):
    miner = None
    ship.start()
    try:
        miner = Miner()
        print(f"[Miner] Корабль #{ship.getId()}; команды: status, home, stop")
        miner.run(max_deliveries)
    except InterruptedError as error:
        print(f"[Miner] {error}")
    finally:
        if miner is not None and ship.is_running():
            miner.stop_equipment()


if __name__ == "__main__":
    main()
