#include <SFML/Graphics.hpp>
#include <optional>
#include <iostream>

#include "game.hpp"
#include "render.hpp"

int main(int argc, char* argv[]) {

    World* world = new World();
    if (argc >= 2) world->Load(argv[1]);

    sf::Clock clock;

    sf::RenderWindow window = CreateGameWindow();

    while (window.isOpen()) {
        float delta_time = clock.restart().asSeconds();

        HandleEvents(window, world);
        if (window.isOpen() && !IsMainMenuOpen()) world->Update(delta_time);

        Render(window, world);
    }

    delete world;

    return 0;
}
