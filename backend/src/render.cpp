#include "render.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace
{
constexpr float selection_radius = 18.0f;
constexpr unsigned int text_size = 14;
constexpr float line_height = 19.0f;
constexpr float entity_list_top = 40.0f;
constexpr float entity_row_height = 28.0f;

// A file captures C/C++ stderr and inherited Python stderr without pipe
// backpressure. The reader must not change the offset shared by the writers.
class StderrLog
{
public:
    StderrLog()
    {
#ifdef _WIN32
        // tmpfile() on MSVC may require write access to the drive's root.
        wchar_t directory[MAX_PATH];
        wchar_t filename[MAX_PATH];
        const auto length = ::GetTempPathW(MAX_PATH, directory);
        if (!length || length >= MAX_PATH ||
            !::GetTempFileNameW(directory, L"ast", 0, filename)) return;
        temp_path_ = filename;
        file_ = ::_wfsopen(filename, L"w+b", _SH_DENYNO);
        if (!file_) return;
        // Opening a second handle gives the reader an independent file offset.
        reader_ = ::CreateFileW(filename, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (reader_ == INVALID_HANDLE_VALUE) return;
        original_ = ::_dup(2);
        if (original_ < 0) return;
        ::SetHandleInformation(reinterpret_cast<HANDLE>(::_get_osfhandle(original_)), HANDLE_FLAG_INHERIT, 0);
        ::SetHandleInformation(reinterpret_cast<HANDLE>(::_get_osfhandle(::_fileno(file_))), HANDLE_FLAG_INHERIT, 0);
        std::fflush(stderr);
        if (::_dup2(::_fileno(file_), 2) < 0) return;
        original_mode_ = ::_setmode(2, _O_BINARY);
        const auto handle = reinterpret_cast<HANDLE>(::_get_osfhandle(2));
        ::SetHandleInformation(handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
        // Boost.Process inherits the Win32 standard handle, as well as CRT stderr.
        ::SetStdHandle(STD_ERROR_HANDLE, handle);
#else
        file_ = std::tmpfile();
        if (!file_) return;
        original_ = ::dup(STDERR_FILENO);
        if (original_ < 0) return;
        ::fcntl(original_, F_SETFD, FD_CLOEXEC);
        std::fflush(stderr);
        if (::dup2(::fileno(file_), STDERR_FILENO) < 0) return;
#endif
        active_ = true;
    }

    ~StderrLog()
    {
        while (Poll()) {}
        std::fflush(stderr);
#ifdef _WIN32
        if (active_) {
            ::_dup2(original_, 2);
            if (original_mode_ >= 0) ::_setmode(2, original_mode_);
            ::SetStdHandle(STD_ERROR_HANDLE, reinterpret_cast<HANDLE>(::_get_osfhandle(2)));
        }
        if (original_ >= 0) ::_close(original_);
        if (reader_ != INVALID_HANDLE_VALUE) ::CloseHandle(reader_);
#else
        if (active_) ::dup2(original_, STDERR_FILENO);
        if (original_ >= 0) ::close(original_);
#endif
        if (file_) std::fclose(file_);
#ifdef _WIN32
        if (!temp_path_.empty()) ::DeleteFileW(temp_path_.c_str());
#endif
    }

    bool Poll()
    {
        if (!active_) return false;
        std::fflush(stderr);
        bool changed = false;
        char buffer[4096];
        // Bound work per frame even when a script is very noisy.
        for (int chunk = 0; chunk < 16; ++chunk) {
#ifdef _WIN32
            DWORD count = 0;
            if (!::ReadFile(reader_, buffer, sizeof(buffer), &count, nullptr) || count == 0) break;
#else
            const auto count = ::pread(::fileno(file_), buffer, sizeof(buffer), offset_);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) break;
            offset_ += count;
#endif
            changed = true;
            for (std::size_t i = 0; i < static_cast<std::size_t>(count); ++i) {
                if (buffer[i] == '\n') lines.emplace_back();
                else if (buffer[i] == '\t') lines.back() += "    ";
                else if (buffer[i] != '\r') lines.back() += buffer[i];
            }
            // Keep the original terminal output as well.
            std::size_t written = 0;
            while (written < static_cast<std::size_t>(count)) {
#ifdef _WIN32
                DWORD result = 0;
                if (!::WriteFile(reinterpret_cast<HANDLE>(::_get_osfhandle(original_)), buffer + written,
                                 static_cast<DWORD>(count - written), &result, nullptr)) break;
#else
                const auto result = ::write(original_, buffer + written, count - written);
                if (result < 0 && errno == EINTR) continue;
#endif
                if (result <= 0) break;
                written += result;
            }
        }
        return changed;
    }

    bool IsActive() const { return active_; }
    std::vector<std::string> lines{std::string{}};

private:
    FILE* file_ = nullptr;
    int original_ = -1;
#ifdef _WIN32
    HANDLE reader_ = INVALID_HANDLE_VALUE;
    std::wstring temp_path_;
    int original_mode_ = -1;
#else
    off_t offset_ = 0;
#endif
    bool active_ = false;
};

// World's constructor launches a script before CreateGameWindow is called.
StderrLog stderr_log;

struct InterfaceState
{
    std::optional<std::size_t> selected;
    sf::String script_path;
    std::size_t cursor = 0;
    bool editing = false;
    sf::String console_input;
    std::size_t console_cursor = 0;
    bool console_editing = false;
    std::vector<sf::String> console_history;
    std::size_t history_index = 0;
    sf::String console_draft;
    // Release the font's GPU textures before closing the window, rather than
    // during static destruction after SFML's graphics context has shut down.
    std::optional<sf::Font> font;
    std::vector<sf::String> log_rows;
    std::size_t log_scroll = 0;
    std::size_t log_columns = 0;
    std::size_t entity_scroll = 0;
};

InterfaceState& Interface()
{
    static InterfaceState state;
    return state;
}

void CloseWindow(sf::RenderWindow& window)
{
    auto& ui = Interface();
    ui.editing = false;
    ui.console_editing = false;
    ui.selected.reset();
    ui.font.reset();
    window.close();
}

Entity* SelectedEntity(World* world)
{
    auto& ui = Interface();
    if (ui.selected) {
        const auto it = world->GetEntities().find(*ui.selected);
        if (it != world->GetEntities().end()) return it->second.get();
        ui.selected.reset();
        ui.editing = false;
    }
    return nullptr;
}

void FollowSelectedEntity(sf::RenderWindow& window, World* world)
{
    if (auto* entity = SelectedEntity(world)) {
        const auto position = entity->GetPosition();
        auto view = window.getView();
        view.setCenter({position.x, -position.y});
        window.setView(view);
    }
}

void SelectEntity(sf::RenderWindow& window, World* world, std::optional<std::size_t> selected)
{
    auto& ui = Interface();
    ui.console_editing = false;
    if (selected != ui.selected) ui.script_path.clear();
    ui.selected = selected;
    ui.cursor = ui.script_path.getSize();
    ui.editing = dynamic_cast<Craft*>(SelectedEntity(world)) != nullptr;
    FollowSelectedEntity(window, world);
}

float SidebarWidth(const sf::RenderWindow& window)
{
    return std::min(220.0f, static_cast<float>(window.getSize().x) * 0.3f);
}

float LogTop(const sf::RenderWindow& window)
{
    const float height = static_cast<float>(window.getSize().y);
    return height - std::min(180.0f, height * 0.35f);
}

std::vector<std::size_t> EntityIds(World* world)
{
    std::vector<std::size_t> ids;
    ids.reserve(world->GetEntities().size());
    for (const auto& entry : world->GetEntities()) ids.push_back(entry.first);
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::size_t VisibleEntityRows(const sf::RenderWindow& window)
{
    return static_cast<std::size_t>(std::max(0.0f,
        (LogTop(window) - entity_list_top) / entity_row_height));
}

void ClampEntityScroll(const sf::RenderWindow& window, std::size_t count)
{
    auto& scroll = Interface().entity_scroll;
    const auto visible = VisibleEntityRows(window);
    scroll = std::min(scroll, count > visible ? count - visible : 0);
}

sf::FloatRect InputBounds(const sf::RenderWindow& window)
{
    const float left = SidebarWidth(window) + 12.0f;
    return {{left, 40.0f},
            {std::max(1.0f, static_cast<float>(window.getSize().x) - left - 12.0f), 30.0f}};
}

sf::FloatRect ConsoleInputBounds(const sf::RenderWindow& window)
{
    return {{8.0f, std::max(LogTop(window), static_cast<float>(window.getSize().y) - 36.0f)},
            {std::max(1.0f, static_cast<float>(window.getSize().x) - 16.0f), 28.0f}};
}

Craft* ConsoleTarget(World* world)
{
    if (Interface().selected) return dynamic_cast<Craft*>(SelectedEntity(world));
    Craft* target = nullptr;
    for (const auto& entry : world->GetEntities()) {
        if (auto* craft = dynamic_cast<Craft*>(entry.second.get())) {
            if (target) return nullptr;
            target = craft;
        }
    }
    return target;
}

void SubmitConsole(World* world)
{
    auto& ui = Interface();
    if (ui.console_input.isEmpty()) return;
    auto* craft = ConsoleTarget(world);
    if (!craft) {
        std::cerr << "Console: select a craft to send a command\n";
        return;
    }
    if (!craft->IsScriptRunning()) {
        std::cerr << "Console: this craft has no running script\n";
        return;
    }
    const auto utf8 = ui.console_input.toUtf8();
    const std::string message(utf8.begin(), utf8.end());
    std::cerr << "> " << message << '\n';
    craft->SendEvent({{"type", "event"}, {"name", "console_message"}, {"data", message}});
    if (ui.console_history.empty() || ui.console_history.back() != ui.console_input)
        ui.console_history.push_back(ui.console_input);
    ui.history_index = ui.console_history.size();
    ui.console_draft.clear();
    ui.console_input.clear();
    ui.console_cursor = 0;
    ui.log_scroll = 0;
}

void EditInput(const sf::Event::KeyPressed& key, sf::String& value, std::size_t& cursor)
{
    if ((key.control || key.system) && key.code == sf::Keyboard::Key::V) {
        sf::String pasted;
        for (const auto character : sf::Clipboard::getString()) {
            if (character >= U' ' && character != 127) pasted += sf::String(character);
        }
        value.insert(cursor, pasted);
        cursor += pasted.getSize();
    }
    else if (key.code == sf::Keyboard::Key::Left && cursor > 0) --cursor;
    else if (key.code == sf::Keyboard::Key::Right && cursor < value.getSize()) ++cursor;
    else if (key.code == sf::Keyboard::Key::Home) cursor = 0;
    else if (key.code == sf::Keyboard::Key::End) cursor = value.getSize();
    else if (key.code == sf::Keyboard::Key::Delete && cursor < value.getSize())
        value.erase(cursor, 1);
}

void DrawInput(sf::RenderWindow& window, const sf::FloatRect& bounds, const sf::String& value,
               std::size_t cursor, bool editing, sf::Color color)
{
    sf::String visible = value;
    if (editing) visible.insert(cursor, "|");
    sf::Text input(*Interface().font, visible, text_size);
    std::size_t first = 0;
    while (first < cursor &&
           input.findCharacterPos(cursor - first + (editing ? 1 : 0)).x > bounds.size.x - 16.0f) {
        visible.erase(0, 1);
        ++first;
        input.setString(visible);
    }
    while (!visible.isEmpty() && input.getLocalBounds().size.x > bounds.size.x - 16.0f) {
        visible.erase(visible.getSize() - 1, 1);
        input.setString(visible);
    }
    input.setPosition(bounds.position + sf::Vector2f(8.0f, 5.0f));
    input.setFillColor(color);
    window.draw(input);
}

void LoadInterfaceFont()
{
    auto& ui = Interface();
    ui.font.emplace();
    for (const auto* path : {"/System/Library/Fonts/Monaco.ttf",
                             "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
                             "/usr/share/fonts/truetype/liberation2/LiberationMono-Regular.ttf",
                             "C:/Windows/Fonts/consola.ttf"}) {
        std::error_code error;
        if (std::filesystem::exists(path, error) && ui.font->openFromFile(path)) {
            return;
        }
    }
    ui.font.reset();
    std::cerr << "Cannot load a system font for the script field and console\n";
}

void DrawInterface(sf::RenderWindow& window, World* world)
{
    auto& ui = Interface();
    const bool log_changed = stderr_log.Poll();
    if (!ui.font || window.getSize().x == 0 || window.getSize().y == 0) return;

    const auto world_view = window.getView();
    const auto size = sf::Vector2f(window.getSize());
    window.setView(sf::View(sf::FloatRect({0.0f, 0.0f}, size)));
    const auto panel = [&](sf::FloatRect bounds, sf::Color color) {
        sf::RectangleShape shape(bounds.size);
        shape.setPosition(bounds.position);
        shape.setFillColor(color);
        window.draw(shape);
    };
    const auto label = [&](const sf::String& value, sf::Vector2f position, sf::Color color) {
        sf::Text text(*ui.font, value, text_size);
        text.setPosition(position);
        text.setFillColor(color);
        window.draw(text);
    };

    const float sidebar_width = SidebarWidth(window);
    panel({{0.0f, 0.0f}, {sidebar_width, LogTop(window)}}, sf::Color(20, 24, 30, 245));
    const auto ids = EntityIds(world);
    ClampEntityScroll(window, ids.size());
    label("Objects (" + std::to_string(ids.size()) + ")", {12.0f, 12.0f}, sf::Color::White);
    const auto list_end = std::min(ids.size(), ui.entity_scroll + VisibleEntityRows(window));
    for (auto row = ui.entity_scroll; row < list_end; ++row) {
        const float y = entity_list_top + (row - ui.entity_scroll) * entity_row_height;
        const bool selected = ui.selected && *ui.selected == ids[row];
        if (selected)
            panel({{4.0f, y}, {std::max(1.0f, sidebar_width - 8.0f), entity_row_height - 2.0f}},
                  sf::Color(46, 61, 75));
        const bool craft = dynamic_cast<Craft*>(world->GetEntities().at(ids[row]).get()) != nullptr;
        label(std::string(craft ? "Craft #" : "Entity #") + std::to_string(ids[row]),
              {12.0f, y + 4.0f}, selected ? sf::Color(255, 215, 70) : sf::Color::White);
    }

    if (auto* entity = SelectedEntity(world)) {
        const bool craft = dynamic_cast<Craft*>(entity) != nullptr;
        const auto bounds = InputBounds(window);
        panel({{sidebar_width, 0.0f}, {size.x - sidebar_width, 82.0f}}, sf::Color(20, 24, 30, 245));
        label("Entity #" + std::to_string(*ui.selected) +
                  (craft ? " | Script path (Enter to launch)" : " | This entity cannot run scripts"),
              {bounds.position.x, 12.0f}, sf::Color::White);
        panel(bounds, ui.editing ? sf::Color(46, 61, 75) : sf::Color(35, 40, 48));
        DrawInput(window, bounds, ui.script_path, ui.cursor, ui.editing,
                  craft ? sf::Color::White : sf::Color(140, 140, 140));
    }

    const float top = LogTop(window);
    panel({{0.0f, top}, {size.x, size.y - top}}, sf::Color(18, 20, 25, 250));
    const auto* target = ConsoleTarget(world);
    const std::string console_title = target ? "Console -> Craft #" + std::to_string(target->GetId())
                                             : "Console | Select a craft";
    label(stderr_log.IsActive() ? console_title + " | Enter: send | Up/Down: history"
                               : "Console capture failed",
          {12.0f, top + 6.0f}, sf::Color(180, 190, 205));
    const auto columns = static_cast<std::size_t>(std::max(1.0f,
        (size.x - 24.0f) / ui.font->getGlyph(U'M', text_size, false).advance));
    if (log_changed || columns != ui.log_columns) {
        const auto old_rows = ui.log_rows.size();
        ui.log_rows.clear();
        ui.log_columns = columns;
        for (std::size_t i = 0; i < stderr_log.lines.size(); ++i) {
            const auto& line = stderr_log.lines[i];
            if (i + 1 == stderr_log.lines.size() && line.empty()) break;
            const auto decoded = sf::String::fromUtf8(line.begin(), line.end());
            if (decoded.isEmpty()) ui.log_rows.emplace_back();
            for (std::size_t start = 0; start < decoded.getSize(); start += columns)
                ui.log_rows.push_back(decoded.substring(start, columns));
        }
        if (ui.log_scroll > 0 && ui.log_rows.size() > old_rows)
            ui.log_scroll += ui.log_rows.size() - old_rows;
    }
    const auto console_bounds = ConsoleInputBounds(window);
    const auto visible_rows = static_cast<std::size_t>(std::max(0.0f,
        (console_bounds.position.y - top - 32.0f) / line_height));
    const auto max_scroll = ui.log_rows.size() > visible_rows ? ui.log_rows.size() - visible_rows : 0;
    ui.log_scroll = std::min(ui.log_scroll, max_scroll);
    const auto end = ui.log_rows.size() - ui.log_scroll;
    const auto begin = end > visible_rows ? end - visible_rows : 0;
    for (auto row = begin; row < end; ++row)
        label(ui.log_rows[row], {12.0f, top + 29.0f + (row - begin) * line_height}, sf::Color(220, 225, 235));
    panel(console_bounds, ui.console_editing ? sf::Color(46, 61, 75) : sf::Color(35, 40, 48));
    if (ui.console_input.isEmpty() && !ui.console_editing)
        label("Click here to type a command", console_bounds.position + sf::Vector2f(8.0f, 5.0f),
              sf::Color(140, 150, 165));
    else
        DrawInput(window, console_bounds, ui.console_input, ui.console_cursor, ui.console_editing,
                  sf::Color::White);
    window.setView(world_view);
}
} // namespace

sf::RenderWindow CreateGameWindow()
{
    sf::RenderWindow window(sf::VideoMode({800, 600}), "PROJECT ASTEROID");
    LoadInterfaceFont();
    sf::View view = window.getView();
    view.setCenter({0.0f, 0.0f});
    window.setView(view);
    return window;
}

void HandleEvents(sf::RenderWindow& window, World* world)
{
    auto& ui = Interface();
    SelectedEntity(world);
    static sf::Clock camera_clock;
    const float delta_time = camera_clock.restart().asSeconds();

    while (const std::optional event = window.pollEvent()) {
        if (event->is<sf::Event::Closed>()) {
            CloseWindow(window);
            return;
        }
        else if (const auto* resized = event->getIf<sf::Event::Resized>()) {
            if (resized->size.x > 0 && resized->size.y > 0) {
                sf::View view = window.getView();
                view.setSize({static_cast<float>(resized->size.x),
                              static_cast<float>(resized->size.y)});
                window.setView(view);
            }
        }
        else if (event->is<sf::Event::FocusLost>()) {
            ui.editing = false;
            ui.console_editing = false;
        }
        else if (const auto* mouse = event->getIf<sf::Event::MouseButtonPressed>()) {
            if (mouse->button != sf::Mouse::Button::Left) continue;
            const auto point = sf::Vector2f(mouse->position);
            if (point.y >= LogTop(window)) {
                ui.editing = false;
                ui.console_editing = true;
                ui.console_cursor = ui.console_input.getSize();
                continue;
            }
            ui.console_editing = false;
            if (point.x < SidebarWidth(window)) {
                ui.editing = false;
                const auto ids = EntityIds(world);
                ClampEntityScroll(window, ids.size());
                if (point.y >= entity_list_top) {
                    const auto row = static_cast<std::size_t>((point.y - entity_list_top) / entity_row_height);
                    const auto index = ui.entity_scroll + row;
                    if (row < VisibleEntityRows(window) && index < ids.size())
                        SelectEntity(window, world, ids[index]);
                }
                continue;
            }
            if (SelectedEntity(world) && point.y < 82.0f) {
                ui.editing = InputBounds(window).contains(point) &&
                             dynamic_cast<Craft*>(SelectedEntity(world));
                ui.cursor = ui.script_path.getSize();
                continue;
            }
            std::optional<std::size_t> selected;
            float nearest = selection_radius * selection_radius;
            for (const auto& entry : world->GetEntities()) {
                const auto pos = entry.second->GetPosition();
                const auto pixel = sf::Vector2f(window.mapCoordsToPixel({pos.x, -pos.y}));
                const auto delta = pixel - point;
                const float distance = delta.x * delta.x + delta.y * delta.y;
                if (distance <= nearest) {
                    nearest = distance;
                    selected = entry.first;
                }
            }
            SelectEntity(window, world, selected);
        }
        else if (const auto* wheel = event->getIf<sf::Event::MouseWheelScrolled>()) {
            if (wheel->wheel == sf::Mouse::Wheel::Vertical && wheel->position.y >= LogTop(window)) {
                const auto amount = static_cast<std::size_t>(std::ceil(std::abs(wheel->delta) * 3.0f));
                if (wheel->delta > 0) ui.log_scroll += amount;
                else ui.log_scroll -= std::min(ui.log_scroll, amount);
            }
            else if (wheel->wheel == sf::Mouse::Wheel::Vertical && wheel->position.x < SidebarWidth(window)) {
                const auto amount = static_cast<std::size_t>(std::ceil(std::abs(wheel->delta) * 3.0f));
                if (wheel->delta > 0) ui.entity_scroll -= std::min(ui.entity_scroll, amount);
                else ui.entity_scroll += amount;
                ClampEntityScroll(window, world->GetEntities().size());
            }
        }
        else if (const auto* key = event->getIf<sf::Event::KeyPressed>()) {
            if (key->code == sf::Keyboard::Key::Escape) {
                if (!ui.console_editing) ui.selected.reset();
                ui.console_editing = false;
                ui.editing = false;
            }
            else if (ui.console_editing) {
                if (key->code == sf::Keyboard::Key::Enter) SubmitConsole(world);
                else if (key->code == sf::Keyboard::Key::Up || key->code == sf::Keyboard::Key::Down) {
                    if (ui.history_index == ui.console_history.size()) ui.console_draft = ui.console_input;
                    if (key->code == sf::Keyboard::Key::Up && ui.history_index > 0) --ui.history_index;
                    if (key->code == sf::Keyboard::Key::Down && ui.history_index < ui.console_history.size())
                        ++ui.history_index;
                    ui.console_input = ui.history_index < ui.console_history.size()
                        ? ui.console_history[ui.history_index] : ui.console_draft;
                    ui.console_cursor = ui.console_input.getSize();
                }
                else EditInput(*key, ui.console_input, ui.console_cursor);
            }
            else if (ui.editing) {
                if (key->code == sf::Keyboard::Key::Enter && !ui.script_path.isEmpty()) {
                    if (auto* craft = dynamic_cast<Craft*>(SelectedEntity(world))) {
                        const auto utf8 = ui.script_path.toUtf8();
                        craft->LaunchScript(std::string(utf8.begin(), utf8.end()));
                    }
                }
                else EditInput(*key, ui.script_path, ui.cursor);
            }
        }
        else if (const auto* text = event->getIf<sf::Event::TextEntered>()) {
            if (!ui.editing && !ui.console_editing) continue;
            auto& value = ui.console_editing ? ui.console_input : ui.script_path;
            auto& cursor = ui.console_editing ? ui.console_cursor : ui.cursor;
            if (text->unicode == U'\b') {
                if (cursor > 0) value.erase(--cursor, 1);
            }
            else if (text->unicode >= U' ' && text->unicode != 127) {
                value.insert(cursor++, sf::String(text->unicode));
            }
        }
    }

    if (!window.isOpen() || !window.hasFocus() || ui.editing || ui.console_editing || ui.selected) {
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
    if (!window.isOpen()) return;
    // Update after World::Update so tracking has no one-frame movement lag.
    FollowSelectedEntity(window, world);
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

        arrow.setPosition({pos.x, -pos.y});
        arrow.setRotation(sf::radians(-entity.second->GetRotation()));
        window.draw(arrow);
    }

    if (auto* entity = SelectedEntity(world)) {
        const auto pos = entity->GetPosition();
        const float radius = selection_radius * view.getSize().x /
                             std::max(1.0f, static_cast<float>(window.getSize().x));
        sf::CircleShape selection(radius, 64);
        selection.setOrigin({radius, radius});
        selection.setPosition({pos.x, -pos.y});
        selection.setFillColor(sf::Color::Transparent);
        selection.setOutlineColor(sf::Color(255, 215, 70));
        selection.setOutlineThickness(2.0f);
        window.draw(selection);
    }

    DrawInterface(window, world);
    window.display();
}
