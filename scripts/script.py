
import sys
import time
import api as ship


@ship.on("console_message")
def console_message(message):
    print(f"Console: {message}", file=sys.stderr)

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

while ship.is_running():
    try:

        prop = ship.Prop("prop_0")
        prop.throttle(1)

        rcs = ship.RCS("rcs_2")
        rcs.gyro(0)

        rcs.setThrust((-1, 0))


    except (TimeoutError, RuntimeError) as error:
        print(error, file=sys.stderr)
        break
