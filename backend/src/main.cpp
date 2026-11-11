#include <SFML/Graphics.hpp>
#include <optional>
#include <iostream>

#include "game.hpp"
#include "render.hpp"

int main(int argc, char* argv[]) {

    World* world = argc < 2 ? new World() : new World(argv[1]);

    sf::Clock clock;

    sf::RenderWindow window = CreateWindow();

    while (window.isOpen()) {
        float delta_time = clock.restart().asSeconds();

        HandleEvents(window, world);
        world->Update(delta_time);

        Render(window, world);
    }

    delete world;

    return 0;
}
