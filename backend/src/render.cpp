#include "render.hpp"

#include <cmath>

sf::RenderWindow CreateWindow()
{
    return sf::RenderWindow(sf::VideoMode({800, 600}), "PROJECT ASTEROID");
}

void HandleEvents(sf::RenderWindow& window, World* world)
{
    static sf::Clock camera_clock;
    const float delta_time = camera_clock.restart().asSeconds();

    while (const std::optional event = window.pollEvent()) {
        if (event->is<sf::Event::Closed>()) {
            window.close();
        }
        else if (const auto* resized = event->getIf<sf::Event::Resized>()) {
            if (resized->size.x > 0 && resized->size.y > 0) {
                sf::View view = window.getView();
                view.setSize({static_cast<float>(resized->size.x),
                              static_cast<float>(resized->size.y)});
                window.setView(view);
            }
        }
    }

    if (!window.isOpen() || !window.hasFocus()) {
        return;
    }

    sf::Vector2f direction(0.0f, 0.0f);
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::W)) direction.y -= 1.0f;
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::S)) direction.y += 1.0f;
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::A)) direction.x -= 1.0f;
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::D)) direction.x += 1.0f;

    const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    if (length > 0.0f) {
        constexpr float camera_speed = 300.0f;
        sf::View view = window.getView();
        view.move(direction * (camera_speed * delta_time / length));
        window.setView(view);
    }
}

void Render(sf::RenderWindow& window, World* world)
{
    window.clear(sf::Color::Black);

    constexpr float grid_spacing = 50.0f;
    const sf::Color grid_color(45, 45, 45);
    const sf::View& view = window.getView();
    const sf::Vector2f top_left = view.getCenter() - view.getSize() / 2.0f;
    const sf::Vector2f bottom_right = top_left + view.getSize();
    sf::VertexArray grid(sf::PrimitiveType::Lines);

    for (float x = std::floor(top_left.x / grid_spacing) * grid_spacing;
         x <= bottom_right.x; x += grid_spacing) {
        grid.append(sf::Vertex{{x, top_left.y}, grid_color});
        grid.append(sf::Vertex{{x, bottom_right.y}, grid_color});
    }
    for (float y = std::floor(top_left.y / grid_spacing) * grid_spacing;
         y <= bottom_right.y; y += grid_spacing) {
        grid.append(sf::Vertex{{top_left.x, y}, grid_color});
        grid.append(sf::Vertex{{bottom_right.x, y}, grid_color});
    }
    window.draw(grid);

    sf::ConvexShape arrow(3);
    arrow.setPoint(0, {12.0f, 0.0f});
    arrow.setPoint(1, {-8.0f, -7.0f});
    arrow.setPoint(2, {-8.0f, 7.0f});
    arrow.setFillColor(sf::Color::Green);

    for (const auto& entity : world->GetEntities())
    {
        Vector2 pos = entity.second->GetPosition();

        arrow.setPosition({pos.x, pos.y});
        arrow.setRotation(sf::radians(-entity.second->GetRotation()));
        window.draw(arrow);
    }

    window.display();
}
