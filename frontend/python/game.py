import os
from asteroidAPI import AsteroidAPI

api = AsteroidAPI(api_key=os.environ.get("ASTEROID_API_KEY"))

try:
    ships = [entity for entity in api.getMyEntities()["entities"] if entity["type"] == "craft"]
    if ships:
        print(api.flyTo(ships[0]["id"], 0, 0))
    else:
        print("Сначала создайте корабль в браузере.")
finally:
    api.disconnect()
