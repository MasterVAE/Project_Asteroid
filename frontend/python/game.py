import os
from asteroidAPI import AsteroidAPI

api = AsteroidAPI(api_key=os.environ.get("ASTEROID_API_KEY"))

api.getMap()
api.getEntity(10)
api.createCraft()