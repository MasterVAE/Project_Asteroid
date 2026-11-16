#pragma once

#include <SFML/Graphics.hpp>
#include <optional>
#include <iostream>

#include "game.hpp"

sf::RenderWindow CreateGameWindow();
bool IsMainMenuOpen();
void HandleEvents(sf::RenderWindow& window, World* world);
void Render(sf::RenderWindow& window, World* world);
