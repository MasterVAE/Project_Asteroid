import api as ship
import time

ship.start()

prop = ship.Propulsion("propulsion_0")


prop.throttle(1)

while ship.is_running():
    time.sleep(0.01)