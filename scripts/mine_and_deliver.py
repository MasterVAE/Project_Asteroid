"""Run on Miner: mine exactly one isolated asteroid, dock and unload at Shipyard #0."""

import math

import api as ship
from demo_common import BASE_ID, run_demo


def main(pilot):
    if pilot.storage.getStorage():
        raise RuntimeError("Начните с пустым складом: один стартовый астероид содержит до 10 ресурсов")
    asteroid = pilot.choose_asteroid()
    pilot.capture(asteroid)

    # The drill has no target-ID command: move away from other asteroids before enabling it.
    for _ in range(6):
        x, y = ship.getPos()
        neighbours = [obj for obj in pilot.scan()
                      if obj["type"] == "asteroid" and obj["id"] != asteroid["id"]
                      and math.hypot(obj["x"] - x, obj["y"] - y) <= 20]
        if not neighbours:
            break
        nearest = min(neighbours, key=lambda obj: math.hypot(obj["x"] - x, obj["y"] - y))
        dx, dy = x - nearest["x"], y - nearest["y"]
        distance = math.hypot(dx, dy)
        direction = (dx / distance, dy / distance) if distance > 0.01 else (1, 0)
        pilot.go_to((x + 40 * direction[0], y + 40 * direction[1]), max_speed=4)
    else:
        raise RuntimeError("Не удалось отделить выбранный астероид от соседних")

    pilot.wait_until(lambda: pilot.power.getCharge() >= 60, 60, "Не удалось накопить энергию для бура")
    print(f"Добыча астероида #{asteroid['id']}")
    pilot.drill.enable()
    try:
        pilot.wait_until(lambda: pilot.grab.getGrabbed() is None, 90, "Астероид не добыт за отведённое время")
    finally:
        if ship.is_running():
            pilot.drill.disable()

    inventory = pilot.storage.getStorage()
    amount = sum(inventory.values())
    if amount == 0:
        raise RuntimeError("Астероид исчез, но добыча не появилась в складе")
    print(f"Астероид полностью добыт: {inventory}")
    pilot.dock_at_base()

    while inventory:
        pilot.ensure_running()
        if ship.getConnection()["connected"] != BASE_ID:
            raise RuntimeError("Стыковка с базой потеряна; груз остался на Miner")
        before = sum(inventory.values())
        for resource, count in inventory.items():
            for _ in range(count):
                pilot.storage.transfer(resource)
        inventory = pilot.storage.getStorage()
        if sum(inventory.values()) >= before:
            raise RuntimeError("Передача не выполнена: проверьте свободное место на складах базы")

    ship.disconnect()
    pilot.wait_until(lambda: ship.getConnection()["connected"] is None, 5, "Не удалось расстыковаться")
    pilot.brake()
    print(f"Готово: один астероид добыт, {amount} единиц ресурсов переданы базе.")


if __name__ == "__main__":
    run_demo(main)
