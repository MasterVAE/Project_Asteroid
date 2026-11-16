"""Run on Miner: capture one asteroid, throw it toward Shipyard #0, then brake."""

import math
import time

import api as ship
from demo_common import STEP, run_demo


THROW_SPEED = 4.0


def main(pilot):
    asteroid = pilot.choose_asteroid()
    pilot.capture(asteroid)
    base_x, base_y = pilot.base_position
    x, y = ship.getPos()
    dx, dy = x - base_x, y - base_y
    distance = math.hypot(dx, dy)
    outward = (dx / distance, dy / distance) if distance > 0.01 else (1, 0)
    acceleration = pilot.max_thrust / ship.getMass()
    # Leave room to reach the release speed before passing the base.
    stand_off = max(120, THROW_SPEED * THROW_SPEED / (2 * acceleration) + 60)
    ox, oy = pilot.asteroid_offset
    pilot.go_to((base_x + outward[0] * stand_off - ox,
                 base_y + outward[1] * stand_off - oy), max_speed=6)
    pilot.scan()
    base_x, base_y = pilot.base_position
    deadline = time.monotonic() + 120
    print(f"Разгон астероида к базе до {THROW_SPEED:.1f} м/с")
    while True:
        pilot.ensure_running()
        if pilot.grab.getGrabbed() != asteroid["id"]:
            raise RuntimeError("Захват астероида потерян")
        x, y = ship.getPos()
        dx, dy = base_x - x - ox, base_y - y - oy
        distance = math.hypot(dx, dy)
        if distance <= 20:
            raise RuntimeError("Недостаточно места для разгона перед базой")
        velocity = (dx / distance * THROW_SPEED, dy / distance * THROW_SPEED)
        pilot.steer_velocity(velocity)
        vx, vy = ship.getVel()
        if math.hypot(vx - velocity[0], vy - velocity[1]) <= 0.03:
            break
        if time.monotonic() >= deadline:
            raise RuntimeError("Не удалось набрать скорость броска")
        time.sleep(STEP)

    pilot.rcs.setThrust((0, 0))
    pilot.grab.release()
    pilot.wait_until(lambda: pilot.grab.getGrabbed() is None, 5, "Не удалось отпустить астероид")
    print(f"Астероид #{asteroid['id']} летит к базе; подлёт примерно через {distance / THROW_SPEED:.1f} с")
    pilot.brake()
    print("Бросок выполнен, Miner затормозил. Астероид продолжает свободный полёт.")


if __name__ == "__main__":
    run_demo(main)
