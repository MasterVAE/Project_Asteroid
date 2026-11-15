"""Запускать на Shipyard #0. Из доставленного сырья строит склад и пустой корабль."""

from collections import Counter
import queue
import time

import api as ship

RAW = {"silicon", "carbon", "iron", "ice", "copper", "gold"}
_commands = queue.Queue()


@ship.on("console_message")
def on_message(message):
    _commands.put(str(message).strip().lower())


class ShipyardDemo:
    def __init__(self):
        names = ship.getModules()
        yards = [name for name in names if name.startswith("shipyard_")]
        factories = [name for name in names if name.startswith("factory_")]
        if not yards or not factories:
            raise RuntimeError("Нужны верфь и завод; запустите скрипт на Shipyard #0")
        self.yard = ship.Shipyard(yards[0])
        self.factory = ship.Factory(factories[0])
        self.recipies = self.factory.getRecipies()
        self.building = {entry["output"]: Counter(entry["input"])
                         for entry in self.yard.getRecipies()}
        self.id = ship.getId()
        self.state = "запуск"
        for name in names:
            if name.startswith("storage_"):
                ship.Storage(name).readyForRecieve()

    def inventory(self):
        total = Counter()
        for name in ship.getModules():
            if name.startswith("storage_"):
                total.update(ship.Storage(name).getStorage())
        return total

    def poll(self):
        if not ship.is_running():
            raise InterruptedError("Соединение закрыто")
        while not _commands.empty():
            command = _commands.get_nowait()
            if command == "stop":
                raise InterruptedError("Скрипт остановлен; уже отправленные заказы остаются в очередях")
            if command == "status":
                print(f"[Shipyard] {self.state}; инвентарь: {dict(self.inventory())}")

    def plan(self, required, inventory):
        stock = inventory.copy()
        operations = []
        missing = Counter()

        def ensure(resource, count, path=()):
            if stock[resource] >= count:
                return
            if resource in RAW:
                shortage = count - stock[resource]
                missing[resource] += shortage
                stock[resource] += shortage
                return
            if resource in path:
                raise RuntimeError(f"Цикл рецептов: {path + (resource,)}")
            choice = next(((index, recipe) for index, recipe in enumerate(self.recipies)
                           if resource in recipe["output"]), None)
            if choice is None:
                raise RuntimeError(f"Нет рецепта для {resource}")
            index, recipe = choice
            while stock[resource] < count:
                ingredients = Counter(recipe["input"])
                for item, amount in ingredients.items():
                    ensure(item, amount, path + (resource,))
                    stock[item] -= amount
                stock.update(recipe["output"])
                operations.append(index)

        for resource, count in required.items():
            ensure(resource, count)
            stock[resource] -= count
        return operations, missing

    def wait(self, predicate, description, timeout=120):
        self.state = description
        deadline = time.monotonic() + timeout
        while True:
            self.poll()
            if predicate():
                return
            if time.monotonic() > deadline:
                raise RuntimeError(f"Тайм-аут: {description}. Проверьте питание и очереди; "
                                   "заказ повторно не отправлен")
            time.sleep(0.3)

    def manufacture(self, required):
        self.state = "ожидание сырья от добытчика"
        previous = None
        while True:
            self.poll()
            operations, missing = self.plan(required, self.inventory())
            if not missing:
                break
            if missing != previous:
                print(f"[Shipyard] Ожидаю сырьё: {dict(missing)}")
                previous = missing
            time.sleep(1)
        for index in operations:
            self.poll()
            recipe = self.recipies[index]
            before = self.inventory()
            gains = Counter(recipe["output"]) - Counter(recipe["input"])
            print(f"[Shipyard] Рецепт #{index}: {recipe['input']} -> {recipe['output']}")
            self.factory.produce(index)

            def completed():
                current = self.inventory()
                return all(current[item] >= before[item] + count for item, count in gains.items())

            self.wait(completed, f"изготовление рецепта #{index}")

    def run(self):
        # Готовим всё заранее, чтобы проверка не оставляла заведомо заблокированные заказы.
        required = self.building["craft"] + self.building["storage"]
        self.manufacture(required)

        count = sum(name.startswith("storage_") for name in ship.getModules())
        self.yard.addModule(self.id, "storage")
        self.wait(lambda: sum(name.startswith("storage_") for name in ship.getModules()) > count,
                  "установка дополнительного склада")
        # Новый склад также должен принимать следующие партии сырья.
        for name in ship.getModules():
            if name.startswith("storage_"):
                ship.Storage(name).readyForRecieve()
        print("[Shipyard] Дополнительный склад установлен")

        before = self.inventory()
        ingredients = self.building["craft"]
        self.yard.constructCraft()

        def craft_completed():
            current = self.inventory()
            return all(current[item] <= before[item] - count for item, count in ingredients.items())

        self.wait(craft_completed, "строительство корабля")
        print("[Shipyard] Ресурсы корабля списаны: пустой корабль создан рядом с верфью")
        print("[Shipyard] Проверка завершена: завод, установка склада и строительство корабля работают")


def main():
    ship.start()
    try:
        demo = ShipyardDemo()
        print(f"[Shipyard] Корабль #{demo.id}; команды: status, stop")
        demo.run()
    except InterruptedError as error:
        print(f"[Shipyard] {error}")


if __name__ == "__main__":
    main()
