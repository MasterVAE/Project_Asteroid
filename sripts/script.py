
import sys
import time
import api as ship


@ship.on("enemy_detected")
def enemy_detected(event):
    # Получаем позицию прямо из обработчика события
    pos = ship.getPos()

    print(
        f"Enemy {event['enemy_id']} at {pos}",
        file=sys.stderr
    )

    ship.shoot(event["enemy_id"])


ship.start()

# Независимая программа корабля
while ship.is_running():
    try:
        pos = ship.getPos()

        print(f"My position: {pos}", file=sys.stderr)

        ship.move_forward(1.0)

        time.sleep(0.5)

    except (TimeoutError, RuntimeError) as error:
        print(error, file=sys.stderr)
        break
