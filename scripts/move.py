"""Перелёт в (50, 50). Скрипт запускается кораблём через Craft::LaunchScript."""

import math
import sys
import time

import api as ship


TARGET = (50.0, 50.0)
CONTROL_INTERVAL = 0.05
POSITION_TOLERANCE = 0.5
SPEED_TOLERANCE = 0.05
CRUISE_SPEED = 5.0


def clamp(value, minimum, maximum):
    return max(minimum, min(maximum, value))


def move_to_target():
    ship.throttle(0.0)
    ship.gyro(0.0)

    max_thrust = ship.getMaxThrottle()["max_throttle"]
    max_gyro = ship.getMaxGyro()["max_gyro"]
    if max_thrust <= 0.0 or max_gyro <= 0.0:
        raise RuntimeError("Для перелёта нужны тяга и возможность поворота")

    # Оставляем время для разворота двигателя в сторону торможения.
    velocity_time = max(1.0, math.pi / max_gyro)
    position_time = 2.0 * velocity_time
    cruise_speed = min(CRUISE_SPEED, max_thrust * velocity_time)

    while ship.is_running():
        position = ship.getPos()
        velocity = ship.getVel()
        angle = ship.getAngle()["angle"]

        dx = TARGET[0] - position["x"]
        dy = TARGET[1] - position["y"]
        distance = math.hypot(dx, dy)
        speed = math.hypot(velocity["x"], velocity["y"])

        if distance <= POSITION_TOLERANCE and speed <= SPEED_TOLERANCE:
            print(
                f"Цель достигнута: ({position['x']:.2f}, {position['y']:.2f})",
                file=sys.stderr,
            )
            return

        # Желаемая скорость уменьшается до нуля возле цели.
        desired_speed = min(cruise_speed, distance / position_time)
        if distance > 0.0:
            desired_vx = dx / distance * desired_speed
            desired_vy = dy / distance * desired_speed
        else:
            desired_vx = desired_vy = 0.0

        ax = (desired_vx - velocity["x"]) / velocity_time
        ay = (desired_vy - velocity["y"]) / velocity_time
        acceleration = math.hypot(ax, ay)

        if acceleration < 1e-6:
            ship.throttle(0.0)
            ship.gyro(0.0)
        else:
            desired_angle = math.atan2(ay, ax)
            angle_error = math.atan2(
                math.sin(desired_angle - angle),
                math.cos(desired_angle - angle),
            )

            # Гироскоп задаёт угловую скорость, а не угловое ускорение.
            ship.gyro(clamp(2.0 * angle_error / max_gyro, -1.0, 1.0))
            if abs(angle_error) < 0.15:
                ship.throttle(clamp(acceleration / max_thrust, 0.0, 1.0))
            else:
                ship.throttle(0.0)

        time.sleep(CONTROL_INTERVAL)


def main():
    ship.start()
    try:
        move_to_target()
    except (TimeoutError, RuntimeError, OSError, KeyboardInterrupt) as error:
        print(f"Перелёт прерван: {error}", file=sys.stderr)
    finally:
        if ship.is_running():
            try:
                ship.throttle(0.0)
                ship.gyro(0.0)
            except OSError:
                pass


if __name__ == "__main__":
    main()
