import math
import sys
import time

import api as ship

storages = [ship.Storage(f"storage_{index}") for index in range(6)]

@ship.on("signal")
def on_signal(signal):
    if signal["message"] == "need fuel":
        for storage in storages:
                inventory = storage.getStorage()
                count = 0
                if inventory.keys().__contains__("rocket_fuel"):
                    count = inventory["rocket_fuel"]
                for i in range(count):
                    storage.transfer(1, "rocket_fuel")

@ship.on("console_message")
def on_message(message):
    for i in storages:
        print(i.getStorage())


def Contains(resource):
    for storage in storages:
        inventory = storage.getStorage()
        if inventory.keys().__contains__(resource):
            return True
    return False


ship.start()

factory = ship.Factory("factory_0")

PI = math.pi

recipes = factory.getRecipes()

for i in range(len(recipes)):
    print(i, recipes[i])

while ship._running:
    if Contains("ice"):
        factory.produce(0)
    elif Contains("water"):
        factory.produce(1)
    elif Contains("hydrogen") and Contains("oxygen"):
        factory.produce(4)
    time.sleep(3.5)