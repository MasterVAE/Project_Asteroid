import os
from asteroidAPI import AsteroidAPI

api = AsteroidAPI(api_key='ast_key_756a0009048992ab3c58a3c2621bf6299f4a396780637f38fa2fb34bffb4ddad')

entities = api.getMyEntities()
for entity in entities["entities"]:
    api.flyTo(entity['id'], entity['id'], entity['id'])
