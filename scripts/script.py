
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


ship.start()
modules = ship.getModules()
print(modules, file=sys.stderr)


# Независимая программа корабля
while ship.is_running():
    try:
        #ship.throttle(1)
        ship.gyro(1)


    except (TimeoutError, RuntimeError) as error:
        print(error, file=sys.stderr)
        break
