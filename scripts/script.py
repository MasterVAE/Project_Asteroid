import math
import sys
import time

import api as ship
from api import Storage as Store
ship.start()

storage = Store("stor_1")
print(storage.getStorage())

storage.transfer(8, "silicon")

while ship.is_running():
    try:
        time.sleep(0.1)


    except (TimeoutError, RuntimeError) as error:
        print(error, file=sys.stderr)
        break
