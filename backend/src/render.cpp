#include "render.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
constexpr float selection_radius = 18.0f;
constexpr unsigned int text_size = 14;
constexpr float line_height = 19.0f;
constexpr float entity_list_top = 40.0f;
constexpr float entity_row_height = 28.0f;
constexpr std::size_t max_log_lines = 1000;
constexpr std::size_t max_log_line_bytes = 4096;
constexpr std::size_t max_log_file_bytes = 4 * 1024 * 1024;
constexpr std::size_t max_script_bytes = 1024 * 1024;

// Explicit instantiations can name private members. Keep this bridge local
// so the renderer can use Craft's existing process lifecycle and saved path.
template<class Member, typename Member::Pointer pointer>
struct CraftScriptAccess
{
    friend typename Member::Pointer ScriptMember(Member) { return pointer; }
};

struct StopScriptMember
{
    using Pointer = void (Craft::*)();
    friend Pointer ScriptMember(StopScriptMember);
};

struct ScriptPathMember
{
    using Pointer = std::string Craft::*;
    friend Pointer ScriptMember(ScriptPathMember);
};

template struct CraftScriptAccess<StopScriptMember, &Craft::StopScript>;
template struct CraftScriptAccess<ScriptPathMember, &Craft::script_path_>;

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
        // Append-only writes remain valid when the capture file is truncated.
        const auto append_handle = ::CreateFileW(filename, FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (append_handle == INVALID_HANDLE_VALUE) return;
        const int append_fd = ::_open_osfhandle(reinterpret_cast<intptr_t>(append_handle),
                                               _O_WRONLY | _O_BINARY);
        if (append_fd < 0) {
            ::CloseHandle(append_handle);
            return;
        }
        std::fflush(stderr);
        const int redirected = ::_dup2(append_fd, 2);
        ::_close(append_fd);
        if (redirected < 0) return;
        original_mode_ = ::_setmode(2, _O_BINARY);
        const auto handle = reinterpret_cast<HANDLE>(::_get_osfhandle(2));
        ::SetHandleInformation(handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
        // Boost.Process inherits the Win32 standard handle, as well as CRT stderr.
        ::SetStdHandle(STD_ERROR_HANDLE, handle);
#else
        file_ = std::tmpfile();
        if (!file_) return;
        const int flags = ::fcntl(::fileno(file_), F_GETFL);
        if (flags < 0 || ::fcntl(::fileno(file_), F_SETFL, flags | O_APPEND) < 0) return;
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
            const auto count = ::pread(::fileno(file_), buffer, sizeof(buffer), bytes_read_);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) break;
#endif
            bytes_read_ += static_cast<std::size_t>(count);
            changed = true;
            for (std::size_t i = 0; i < static_cast<std::size_t>(count); ++i) {
                if (buffer[i] == '\n') NewLine();
                else if (buffer[i] != '\r') {
                    // Keep UTF-8 continuation bytes with the preceding character.
                    if (lines.back().size() >= max_log_line_bytes &&
                        (lines.back().size() >= max_log_line_bytes + 3 ||
                         (static_cast<unsigned char>(buffer[i]) & 0xC0) != 0x80)) NewLine();
                    if (buffer[i] == '\t') lines.back() += "    ";
                    else lines.back() += buffer[i];
                }
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
        if (bytes_read_ >= max_log_file_bytes) {
#ifdef _WIN32
            if (::_chsize_s(::_fileno(file_), 0) == 0) {
                LARGE_INTEGER beginning{};
                ::SetFilePointerEx(reader_, beginning, nullptr, FILE_BEGIN);
                bytes_read_ = 0;
            }
#else
            if (::ftruncate(::fileno(file_), 0) == 0) bytes_read_ = 0;
#endif
        }
        return changed;
    }

    bool IsActive() const { return active_; }

    void Clear()
    {
        if (active_) {
            std::fflush(stderr);
            // Skip unread output too, so a busy script cannot restore old rows.
#ifdef _WIN32
            LARGE_INTEGER distance{}, end{};
            if (::SetFilePointerEx(reader_, distance, &end, FILE_END))
                bytes_read_ = static_cast<std::size_t>(end.QuadPart);
#else
            struct stat info{};
            if (::fstat(::fileno(file_), &info) == 0)
                bytes_read_ = static_cast<std::size_t>(info.st_size);
#endif
        }
        lines.assign(1, std::string{});
    }

    std::deque<std::string> lines{std::string{}};

private:
    void NewLine()
    {
        lines.emplace_back();
        if (lines.size() > max_log_lines) lines.pop_front();
    }

    FILE* file_ = nullptr;
    int original_ = -1;
#ifdef _WIN32
    HANDLE reader_ = INVALID_HANDLE_VALUE;
    std::wstring temp_path_;
    int original_mode_ = -1;
#endif
    std::size_t bytes_read_ = 0;
    bool active_ = false;
};

StderrLog stderr_log;

struct CraftInterfaceState
{
    sf::String script_path;
    sf::String launched_path;
};

struct ScriptEditor
{
    std::filesystem::path path;
    sf::String code;
    std::string saved_bytes;
    std::vector<std::size_t> line_starts{0};
    std::size_t cursor = 0;
    std::size_t anchor = 0;
    std::size_t first_row = 0;
    std::optional<std::size_t> preferred_column;
    float scroll_x = 0.0f;
    bool crlf = false;
    bool bom = false;
    bool modified = false;
    bool dragging = false;
    bool confirm_discard = false;
    bool closing_game = false;
    sf::String message;
};

struct InterfaceState
{
    bool main_menu = true;
    bool has_active_game = false;
    bool fullscreen = false;
    sf::Vector2u windowed_size{800, 600};
    sf::Vector2i windowed_position;
    sf::String save_path;
    std::size_t save_cursor = 0;
    bool save_editing = false;
    std::optional<std::size_t> selected;
    sf::String script_path;
    std::size_t cursor = 0;
    bool editing = false;
    std::optional<ScriptEditor> editor;
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
    std::unordered_map<std::size_t, CraftInterfaceState> crafts;
    float camera_zoom = 1.0f;
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
    ui.editor.reset();
    ui.selected.reset();
    ui.font.reset();
    window.close();
}

std::string ToUtf8(const sf::String& value)
{
    const auto bytes = value.toUtf8();
    return {bytes.begin(), bytes.end()};
}

sf::String FromUtf8(const std::string& bytes)
{
    return sf::String::fromUtf8(bytes.begin(), bytes.end());
}

sf::String AttachedScriptPath(Craft& craft)
{
    std::lock_guard lock(craft.send_mutex_);
    return FromUtf8(craft.*ScriptMember(ScriptPathMember{}));
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
    const bool changed = selected != ui.selected;
    if (changed) {
        if (auto* craft = dynamic_cast<Craft*>(SelectedEntity(world)))
            ui.crafts[craft->GetId()].script_path = ui.script_path;
        ui.script_path.clear();
    }
    ui.selected = selected;
    auto* craft = dynamic_cast<Craft*>(SelectedEntity(world));
    if (changed && craft) {
        auto& state = ui.crafts[craft->GetId()];
        ui.script_path = state.script_path.isEmpty() ? AttachedScriptPath(*craft) : state.script_path;
    }
    ui.cursor = ui.script_path.getSize();
    ui.editing = craft != nullptr;
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

sf::String EntityLabel(const Entity* entity)
{
    const auto name = entity->GetName();
    return sf::String::fromUtf8(name.begin(), name.end());
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

sf::FloatRect ClearConsoleBounds(const sf::RenderWindow& window)
{
    return {{std::max(8.0f, static_cast<float>(window.getSize().x) - 96.0f), LogTop(window) + 3.0f},
            {88.0f, 24.0f}};
}

sf::FloatRect MainMenuButtonBounds(const sf::RenderWindow& window)
{
    return {{std::max(SidebarWidth(window) + 12.0f,
                     static_cast<float>(window.getSize().x) - 202.0f), 6.0f}, {190.0f, 26.0f}};
}

enum class ScriptAction { Run, Stop, Restart, Edit, Create };

constexpr std::array script_actions{
    ScriptAction::Run, ScriptAction::Stop, ScriptAction::Restart, ScriptAction::Edit, ScriptAction::Create
};
constexpr std::array script_action_titles{"Run", "Stop", "Restart", "Edit code", "New script"};

sf::FloatRect ScriptButtonBounds(const sf::RenderWindow& window, std::size_t index)
{
    const auto input = InputBounds(window);
    const float gap = std::min(6.0f, input.size.x / 20.0f);
    const float width = (input.size.x - gap * (script_actions.size() - 1)) / script_actions.size();
    return {{input.position.x + index * (width + gap), 78.0f}, {width, 28.0f}};
}

sf::FloatRect ZoomBounds(const sf::RenderWindow& window, bool zoom_in)
{
    return {{std::max(SidebarWidth(window) + 12.0f,
                     static_cast<float>(window.getSize().x) - 52.0f), zoom_in ? 130.0f : 174.0f},
            {40.0f, 36.0f}};
}

void ZoomCamera(sf::RenderWindow& window, float factor)
{
    auto& ui = Interface();
    const float zoom = std::clamp(ui.camera_zoom * factor, 0.1f, 8.0f);
    auto view = window.getView();
    view.zoom(zoom / ui.camera_zoom);
    window.setView(view);
    ui.camera_zoom = zoom;
}

void ReturnToMainMenu()
{
    auto& ui = Interface();
    ui.main_menu = true;
    ui.has_active_game = true;
    ui.save_editing = false;
    ui.editing = false;
    ui.console_editing = false;
}

void ResetWorldInterface(sf::RenderWindow& window)
{
    auto& ui = Interface();
    ui.selected.reset();
    ui.crafts.clear();
    ui.editor.reset();
    ui.script_path.clear();
    ui.cursor = 0;
    ui.editing = false;
    ui.console_input.clear();
    ui.console_cursor = 0;
    ui.console_editing = false;
    ui.console_history.clear();
    ui.history_index = 0;
    ui.console_draft.clear();
    ui.save_editing = false;
    stderr_log.Clear();
    ui.log_rows.clear();
    ui.log_scroll = 0;
    ui.log_columns = 0;
    ui.entity_scroll = 0;
    ui.camera_zoom = 1.0f;
    sf::View view(sf::FloatRect({0.0f, 0.0f}, sf::Vector2f(window.getSize())));
    view.setCenter({0.0f, 0.0f});
    window.setView(view);
}

void StartNewGame(sf::RenderWindow& window, World* world)
{
    world->Restart();
    ResetWorldInterface(window);
    auto& ui = Interface();
    ui.has_active_game = true;
    ui.main_menu = false;
}

void LaunchSelectedScript(World* world, bool restart)
{
    auto* craft = dynamic_cast<Craft*>(SelectedEntity(world));
    if (!craft) return;
    auto& ui = Interface();
    auto& state = ui.crafts[craft->GetId()];
    sf::String path = ui.script_path;
    if (restart) {
        const auto attached = AttachedScriptPath(*craft);
        if (!attached.isEmpty()) path = attached;
        else if (!state.launched_path.isEmpty()) path = state.launched_path;
    }
    if (path.isEmpty()) return;
    std::error_code error;
    const auto file = std::filesystem::absolute(std::filesystem::u8path(ToUtf8(path)), error);
    if (error || !std::filesystem::is_regular_file(file, error)) {
        std::cerr << "Cannot run script: " << ToUtf8(path) << '\n';
        return;
    }
    const auto filename = ToUtf8(path);
    craft->LaunchScript(filename.front() == '-' ? file.u8string() : filename);
    state.script_path = ui.script_path = path;
    ui.cursor = path.getSize();
    if (!AttachedScriptPath(*craft).isEmpty()) state.launched_path = path;
}

void StopSelectedScript(Craft& craft)
{
    const auto path = AttachedScriptPath(craft);
    if (!path.isEmpty()) Interface().crafts[craft.GetId()].launched_path = path;
    (craft.*ScriptMember(StopScriptMember{}))();
}

std::optional<std::string> ReadScriptFile(const std::filesystem::path& path, sf::String& error_text)
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        error_text = "Script file not found";
        return std::nullopt;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error_text = "Cannot open script file";
        return std::nullopt;
    }
    std::string bytes(max_script_bytes + 1, '\0');
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    bytes.resize(static_cast<std::size_t>(input.gcount()));
    if (input.bad() || bytes.size() > max_script_bytes) {
        error_text = input.bad() ? "Cannot read script file" : "Script exceeds the 1 MiB editor limit";
        return std::nullopt;
    }
    return bytes;
}

enum class WriteResult { Written, Exists, Failed };

WriteResult WriteNewFile(const std::filesystem::path& path, const std::string& bytes, std::error_code& error)
{
#ifdef _WIN32
    const int fd = ::_wopen(path.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
                            _S_IREAD | _S_IWRITE);
    if (fd < 0) {
#else
    FILE* file = std::fopen(path.c_str(), "wbx");
    if (!file) {
#endif
        if (errno == EEXIST) return WriteResult::Exists;
        error = std::error_code(errno, std::generic_category());
        return WriteResult::Failed;
    }
#ifdef _WIN32
    FILE* file = ::_fdopen(fd, "wb");
    if (!file) {
        const auto open_error = errno;
        ::_close(fd);
        error = std::error_code(open_error, std::generic_category());
    }
#endif
    if (file) {
        if (std::fwrite(bytes.data(), 1, bytes.size(), file) != bytes.size())
            error = std::error_code(errno ? errno : EIO, std::generic_category());
        if (std::fclose(file) != 0 && !error)
            error = std::error_code(errno, std::generic_category());
    }
    if (error) {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        return WriteResult::Failed;
    }
    return WriteResult::Written;
}

sf::String NormalizeNewlines(const sf::String& text)
{
    std::u32string normalized;
    normalized.reserve(text.getSize());
    for (std::size_t i = 0; i < text.getSize(); ++i) {
        if (text[i] == U'\r') {
            if (i + 1 < text.getSize() && text[i + 1] == U'\n') ++i;
            normalized += U'\n';
        }
        else if (text[i] == U'\n' || text[i] == U'\t' || (text[i] >= U' ' && text[i] != 127)) {
            normalized += text[i];
        }
    }
    return sf::String(normalized);
}

void IndexScriptLines(ScriptEditor& editor)
{
    editor.line_starts.assign(1, 0);
    for (std::size_t i = 0; i < editor.code.getSize(); ++i)
        if (editor.code[i] == U'\n') editor.line_starts.push_back(i + 1);
}

void ShowScriptEditor(const std::filesystem::path& path, std::string bytes)
{
    auto code = FromUtf8(bytes);
    const auto normalized = NormalizeNewlines(code);
    // Avoid rewriting files with unsupported encodings or embedded control characters.
    if (ToUtf8(code) != bytes ||
        std::any_of(code.begin(), code.end(), [](char32_t c) {
            return (c < U' ' && c != U'\r' && c != U'\n' && c != U'\t') || c == 127;
        })) {
        std::cerr << "Script editor requires a UTF-8 text file: " << path.u8string() << '\n';
        return;
    }
    auto& ui = Interface();
    auto& editor = ui.editor.emplace();
    editor.path = path;
    const auto first_break = bytes.find_first_of("\r\n");
    editor.crlf = first_break != std::string::npos && bytes.compare(first_break, 2, "\r\n") == 0;
    editor.bom = !code.isEmpty() && code[0] == U'\uFEFF';
    editor.code = normalized;
    if (editor.bom) editor.code.erase(0, 1);
    editor.saved_bytes = std::move(bytes);
    IndexScriptLines(editor);
    ui.editing = false;
    ui.console_editing = false;
}

void OpenScriptEditor(Craft& craft)
{
    auto& ui = Interface();
    if (ui.script_path.isEmpty()) ui.script_path = AttachedScriptPath(craft);
    const auto path = ToUtf8(ui.script_path);
    if (path.empty()) return;
    std::error_code error;
    const auto file = std::filesystem::canonical(std::filesystem::u8path(path), error);
    sf::String message = "Cannot open script file";
    if (!error) {
        if (auto bytes = ReadScriptFile(file, message)) {
            ShowScriptEditor(file, std::move(*bytes));
            return;
        }
    }
    std::cerr << ToUtf8(message) << ": " << path << '\n';
}

void CreateScript(Craft& craft)
{
    std::error_code error;
    const auto directory = std::filesystem::current_path(error);
    if (error) {
        std::cerr << "Cannot find the game directory: " << error.message() << '\n';
        return;
    }
    // The packaged API is at the game root. Source checkouts keep it in scripts/.
    std::error_code ignored;
    const bool source_tree = !std::filesystem::is_regular_file(directory / "api.py", ignored) &&
        std::filesystem::is_regular_file(directory / "scripts" / "api.py", ignored);
    const std::string starter = std::string("import time\n") +
        (source_tree ? "import scripts.api as ship\n" : "import api as ship\n") +
        "\nship.start()\n\nwhile ship.is_running():\n    time.sleep(0.1)\n";
    for (std::size_t index = 1;; ++index) {
        const auto name = index == 1 ? std::string("new_script.py")
                                    : "new_script_" + std::to_string(index) + ".py";
        const auto path = directory / name;
        const auto result = WriteNewFile(path, starter, error);
        if (result == WriteResult::Exists) continue;
        if (result == WriteResult::Failed) {
            std::cerr << "Cannot create " << name << ": " << error.message() << '\n';
            return;
        }
        auto& ui = Interface();
        ui.crafts[craft.GetId()].script_path = ui.script_path = FromUtf8(name);
        ui.cursor = ui.script_path.getSize();
        ShowScriptEditor(path, starter);
        return;
    }
}

void SaveScript(ScriptEditor& editor)
{
    editor.confirm_discard = false;
    editor.closing_game = false;
    if (!editor.modified) return;
    auto bytes = ToUtf8(editor.code);
    if (editor.crlf) {
        std::string converted;
        for (const auto c : bytes) {
            if (c == '\n') converted += '\r';
            converted += c;
        }
        bytes = std::move(converted);
    }
    if (editor.bom) bytes.insert(0, "\xEF\xBB\xBF");
    if (bytes.size() > max_script_bytes) {
        editor.message = "Script exceeds the 1 MiB editor limit";
        return;
    }
    sf::String message;
    const auto current = ReadScriptFile(editor.path, message);
    if (!current || *current != editor.saved_bytes) {
        editor.message = "The file changed on disk. Copy your edits before closing.";
        return;
    }
    // Write beside the original so a failed write leaves the script intact.
    std::filesystem::path temporary;
    std::error_code error;
    for (std::size_t index = 1;; ++index) {
        temporary = editor.path;
        temporary += ".asteroid-" + std::to_string(index) + ".tmp";
        if (WriteNewFile(temporary, bytes, error) != WriteResult::Exists) break;
    }
    if (error) {
        editor.message = FromUtf8("Cannot save script: " + error.message());
        return;
    }
#ifndef _WIN32
    const auto original = std::filesystem::status(editor.path, error);
    if (!error) std::filesystem::permissions(temporary, original.permissions(), error);
#endif
    if (!error) std::filesystem::rename(temporary, editor.path, error);
    if (error) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        editor.message = FromUtf8("Cannot save script: " + error.message());
        return;
    }
    editor.saved_bytes = std::move(bytes);
    editor.modified = false;
    editor.message = "Script saved";
}

struct ScriptEditorLayout
{
    sf::FloatRect panel;
    sf::FloatRect code;
    sf::FloatRect save;
    sf::FloatRect close;
    sf::Vector2f status;
};

ScriptEditorLayout ScriptEditorBounds(const sf::RenderWindow& window)
{
    const auto size = sf::Vector2f(window.getSize());
    const float margin = std::min(24.0f, std::min(size.x, size.y) * 0.04f);
    const sf::FloatRect panel{{margin, margin}, size - sf::Vector2f(2.0f * margin, 2.0f * margin)};
    const float bottom = panel.position.y + panel.size.y;
    const float right = panel.position.x + panel.size.x;
    const float button_width = std::min(104.0f, panel.size.x * 0.3f);
    return {
        panel,
        {{panel.position.x + 70.0f, panel.position.y + 39.0f},
         {std::max(0.0f, panel.size.x - 82.0f), std::max(0.0f, panel.size.y - 112.0f)}},
        {{right - 20.0f - 2.0f * button_width, bottom - 40.0f}, {button_width, 28.0f}},
        {{right - 12.0f - button_width, bottom - 40.0f}, {button_width, 28.0f}},
        {panel.position.x + 12.0f, bottom - 63.0f}
    };
}

std::size_t EditorRow(const ScriptEditor& editor, std::size_t position)
{
    return std::upper_bound(editor.line_starts.begin(), editor.line_starts.end(), position) -
           editor.line_starts.begin() - 1;
}

std::size_t EditorLineEnd(const ScriptEditor& editor, std::size_t row)
{
    return row + 1 < editor.line_starts.size() ? editor.line_starts[row + 1] - 1 : editor.code.getSize();
}

sf::Text EditorLine(const ScriptEditor& editor, std::size_t row)
{
    const auto start = editor.line_starts[row];
    return sf::Text(*Interface().font, editor.code.substring(start, EditorLineEnd(editor, row) - start), text_size);
}

std::size_t EditorVisibleRows(const sf::FloatRect& code)
{
    return static_cast<std::size_t>(std::max(1.0f, std::floor(code.size.y / line_height)));
}

void ClampEditorScroll(ScriptEditor& editor, std::size_t visible_rows)
{
    const auto count = editor.line_starts.size();
    editor.first_row = std::min(editor.first_row, count > visible_rows ? count - visible_rows : 0);
}

void RevealEditorCursor(const sf::FloatRect& bounds, ScriptEditor& editor)
{
    const auto row = EditorRow(editor, editor.cursor);
    const auto visible_rows = EditorVisibleRows(bounds);
    if (row < editor.first_row) editor.first_row = row;
    if (row >= editor.first_row + visible_rows) editor.first_row = row - visible_rows + 1;
    ClampEditorScroll(editor, visible_rows);
    const auto text = EditorLine(editor, row);
    const float x = text.findCharacterPos(editor.cursor - editor.line_starts[row]).x;
    const float width = std::max(1.0f, bounds.size.x - 12.0f);
    editor.scroll_x = std::clamp(editor.scroll_x, std::max(0.0f, x - width), x);
}

std::size_t EditorPositionAt(const ScriptEditor& editor, const sf::FloatRect& area, sf::Vector2f point)
{
    const auto offset = static_cast<std::size_t>(std::max(0.0f, (point.y - area.position.y) / line_height));
    const auto row = std::min({editor.first_row + offset, editor.line_starts.size() - 1,
                               editor.first_row + EditorVisibleRows(area) - 1});
    const auto text = EditorLine(editor, row);
    const float x = point.x - area.position.x - 4.0f + editor.scroll_x;
    std::size_t first = 0;
    std::size_t last = text.getString().getSize();
    while (first < last) {
        const auto middle = first + (last - first) / 2;
        const float boundary = (text.findCharacterPos(middle).x + text.findCharacterPos(middle + 1).x) / 2.0f;
        if (x < boundary) last = middle;
        else first = middle + 1;
    }
    return editor.line_starts[row] + first;
}

void EditScriptSelection(ScriptEditor& editor, const sf::String& replacement)
{
    const auto first = std::min(editor.cursor, editor.anchor);
    const auto count = std::max(editor.cursor, editor.anchor) - first;
    if (editor.code.getSize() - count + replacement.getSize() > max_script_bytes) {
        editor.message = "Script exceeds the 1 MiB editor limit";
        return;
    }
    if (!count && replacement.isEmpty()) return;
    editor.code.erase(first, count);
    editor.code.insert(first, replacement);
    editor.cursor = editor.anchor = first + replacement.getSize();
    editor.preferred_column.reset();
    editor.modified = true;
    editor.confirm_discard = false;
    editor.closing_game = false;
    editor.message.clear();
    IndexScriptLines(editor);
}

void RequestCloseEditor(sf::RenderWindow& window, bool closing_game = false)
{
    auto& editor = *Interface().editor;
    if (editor.modified) {
        editor.confirm_discard = true;
        editor.closing_game = closing_game;
    }
    else if (closing_game) CloseWindow(window);
    else Interface().editor.reset();
}

void SaveEditor(sf::RenderWindow& window)
{
    auto& editor = *Interface().editor;
    const bool close = editor.confirm_discard;
    const bool exit = editor.closing_game;
    SaveScript(editor);
    if (close && !editor.modified) RequestCloseEditor(window, exit);
}

void EditScriptKey(ScriptEditor& editor, const sf::Event::KeyPressed& key, std::size_t visible_rows)
{
    using Key = sf::Keyboard::Key;
    const bool shortcut = key.control || key.system;
    if (shortcut && key.code == Key::A) {
        editor.anchor = 0;
        editor.cursor = editor.code.getSize();
        editor.preferred_column.reset();
        return;
    }
    if (shortcut && (key.code == Key::C || key.code == Key::X)) {
        if (editor.cursor != editor.anchor) {
            const auto first = std::min(editor.cursor, editor.anchor);
            const auto last = std::max(editor.cursor, editor.anchor);
            sf::Clipboard::setString(editor.code.substring(first, last - first));
            if (key.code == Key::X) EditScriptSelection(editor, {});
        }
        return;
    }
    if (shortcut && key.code == Key::V) {
        EditScriptSelection(editor, NormalizeNewlines(sf::Clipboard::getString()));
        return;
    }
    const auto row = EditorRow(editor, editor.cursor);
    const auto start = editor.line_starts[row];
    if (key.code == Key::Enter) {
        std::size_t indent = start;
        while (indent < editor.cursor && (editor.code[indent] == U' ' || editor.code[indent] == U'\t'))
            ++indent;
        EditScriptSelection(editor, sf::String(U'\n') + editor.code.substring(start, indent - start));
        return;
    }
    if (key.code == Key::Tab) {
        EditScriptSelection(editor, "    ");
        return;
    }
    if (key.code == Key::Backspace || key.code == Key::Delete) {
        if (editor.cursor == editor.anchor) {
            if (key.code == Key::Backspace && editor.cursor > 0) --editor.anchor;
            if (key.code == Key::Delete && editor.cursor < editor.code.getSize()) ++editor.anchor;
        }
        EditScriptSelection(editor, {});
        return;
    }
    const bool selection = editor.cursor != editor.anchor;
    if (key.code == Key::Up || key.code == Key::Down || key.code == Key::PageUp || key.code == Key::PageDown) {
        if (!editor.preferred_column) editor.preferred_column = editor.cursor - start;
        const auto amount = key.code == Key::PageUp || key.code == Key::PageDown ? visible_rows : 1;
        const auto target = key.code == Key::Up || key.code == Key::PageUp
            ? row - std::min(row, amount) : std::min(editor.line_starts.size() - 1, row + amount);
        editor.cursor = std::min(editor.line_starts[target] + *editor.preferred_column,
                                 EditorLineEnd(editor, target));
    }
    else {
        switch (key.code) {
        case Key::Left:
            if (selection && !key.shift) editor.cursor = std::min(editor.cursor, editor.anchor);
            else if (editor.cursor > 0) --editor.cursor;
            break;
        case Key::Right:
            if (selection && !key.shift) editor.cursor = std::max(editor.cursor, editor.anchor);
            else if (editor.cursor < editor.code.getSize()) ++editor.cursor;
            break;
        case Key::Home: editor.cursor = shortcut ? 0 : start; break;
        case Key::End: editor.cursor = shortcut ? editor.code.getSize() : EditorLineEnd(editor, row); break;
        default: return;
        }
        editor.preferred_column.reset();
    }
    if (!key.shift) editor.anchor = editor.cursor;
}

void HandleScriptEditorEvent(sf::RenderWindow& window, const sf::Event& event)
{
    auto& editor = *Interface().editor;
    const auto bounds = ScriptEditorBounds(window);
    if (const auto* mouse = event.getIf<sf::Event::MouseButtonPressed>()) {
        if (mouse->button != sf::Mouse::Button::Left) return;
        const auto point = sf::Vector2f(mouse->position);
        if (bounds.save.contains(point)) SaveEditor(window);
        else if (bounds.close.contains(point)) {
            if (!editor.confirm_discard) RequestCloseEditor(window);
            else if (editor.closing_game) CloseWindow(window);
            else Interface().editor.reset();
        }
        else if (bounds.code.contains(point) && Interface().font) {
            editor.cursor = EditorPositionAt(editor, bounds.code, point);
            if (!sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) &&
                !sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift)) editor.anchor = editor.cursor;
            editor.preferred_column.reset();
            editor.dragging = true;
        }
    }
    else if (event.is<sf::Event::MouseButtonReleased>()) editor.dragging = false;
    else if (const auto* mouse = event.getIf<sf::Event::MouseMoved>()) {
        if (editor.dragging && Interface().font)
            editor.cursor = EditorPositionAt(editor, bounds.code, sf::Vector2f(mouse->position));
    }
    else if (const auto* wheel = event.getIf<sf::Event::MouseWheelScrolled>()) {
        if (!bounds.code.contains(sf::Vector2f(wheel->position))) return;
        if (wheel->wheel == sf::Mouse::Wheel::Horizontal ||
            sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) ||
            sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift)) {
            editor.scroll_x = std::max(0.0f, editor.scroll_x - wheel->delta * 48.0f);
        }
        else {
            const auto amount = static_cast<std::size_t>(std::ceil(std::abs(wheel->delta) * 3.0f));
            if (wheel->delta > 0) editor.first_row -= std::min(editor.first_row, amount);
            else editor.first_row += amount;
            ClampEditorScroll(editor, EditorVisibleRows(bounds.code));
        }
    }
    else if (const auto* key = event.getIf<sf::Event::KeyPressed>()) {
        if (key->code == sf::Keyboard::Key::Escape) {
            if (editor.confirm_discard) {
                editor.confirm_discard = false;
                editor.closing_game = false;
            }
            else RequestCloseEditor(window);
        }
        else if ((key->control || key->system) && key->code == sf::Keyboard::Key::S) SaveEditor(window);
        else {
            const auto old_cursor = editor.cursor;
            const auto old_anchor = editor.anchor;
            EditScriptKey(editor, *key, EditorVisibleRows(bounds.code));
            if (Interface().font && (editor.cursor != old_cursor || editor.anchor != old_anchor ||
                                     key->code == sf::Keyboard::Key::Delete))
                RevealEditorCursor(bounds.code, editor);
        }
    }
    else if (const auto* text = event.getIf<sf::Event::TextEntered>()) {
        if (text->unicode >= U' ' && text->unicode != 127) {
            EditScriptSelection(editor, sf::String(text->unicode));
            if (Interface().font) RevealEditorCursor(bounds.code, editor);
        }
    }
}

void DrawEditorLines(sf::RenderWindow& window, ScriptEditor& editor, const sf::FloatRect& area)
{
    if (area.size.x <= 0.0f || area.size.y < line_height) return;
    const auto visible_rows = EditorVisibleRows(area);
    ClampEditorScroll(editor, visible_rows);
    const auto end = std::min(editor.first_row + visible_rows, editor.line_starts.size());
    const auto cursor_row = EditorRow(editor, editor.cursor);
    const auto selection_start = std::min(editor.cursor, editor.anchor);
    const auto selection_end = std::max(editor.cursor, editor.anchor);
    const auto row_y = [&](std::size_t row) {
        return area.position.y + (row - editor.first_row) * line_height;
    };
    auto& font = *Interface().font;
    for (auto row = editor.first_row; row < end; ++row) {
        sf::Text number(font, std::to_string(row + 1), text_size);
        number.setFillColor(row == cursor_row ? sf::Color(210, 220, 235) : sf::Color(125, 140, 158));
        number.setPosition({area.position.x - 10.0f - number.getLocalBounds().size.x, row_y(row) + 1.0f});
        window.draw(number);
    }
    const auto view = window.getView();
    const auto size = sf::Vector2f(window.getSize());
    auto clipped = view;
    clipped.setScissor({{area.position.x / size.x, area.position.y / size.y},
                        {area.size.x / size.x, area.size.y / size.y}});
    window.setView(clipped);
    sf::RectangleShape rectangle;
    for (auto row = editor.first_row; row < end; ++row) {
        const auto y = row_y(row);
        if (row == cursor_row) {
            rectangle.setSize({area.size.x, line_height});
            rectangle.setPosition({area.position.x, y});
            rectangle.setFillColor(sf::Color(37, 48, 62));
            window.draw(rectangle);
        }
        const auto start = editor.line_starts[row];
        const auto finish = EditorLineEnd(editor, row);
        auto text = EditorLine(editor, row);
        text.setPosition({std::round(area.position.x + 4.0f - editor.scroll_x), std::round(y + 1.0f)});
        const auto x_at = [&](std::size_t position) { return text.findCharacterPos(position - start).x; };
        if (selection_start != selection_end && selection_start <= finish && selection_end > start) {
            const float from = x_at(std::max(start, selection_start));
            float to = x_at(std::min(finish, selection_end));
            if (selection_end > finish) to += font.getGlyph(U' ', text_size, false).advance;
            rectangle.setSize({to - from, line_height});
            rectangle.setPosition({from, y});
            rectangle.setFillColor(sf::Color(58, 93, 126));
            window.draw(rectangle);
        }
        text.setFillColor(sf::Color(230, 235, 243));
        window.draw(text);
        if (row == cursor_row && window.hasFocus()) {
            rectangle.setSize({1.0f, line_height - 2.0f});
            rectangle.setPosition({x_at(editor.cursor), y + 1.0f});
            rectangle.setFillColor(sf::Color(235, 241, 250));
            window.draw(rectangle);
        }
    }
    window.setView(view);
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
               std::size_t cursor, bool editing, sf::Color color,
               unsigned int character_size = text_size, sf::Vector2f padding = {8.0f, 5.0f})
{
    sf::String visible = value;
    if (editing) visible.insert(cursor, "|");
    sf::Text input(*Interface().font, visible, character_size);
    std::size_t first = 0;
    while (first < cursor &&
           input.findCharacterPos(cursor - first + (editing ? 1 : 0)).x > bounds.size.x - 2.0f * padding.x) {
        visible.erase(0, 1);
        ++first;
        input.setString(visible);
    }
    while (!visible.isEmpty() && input.getLocalBounds().size.x > bounds.size.x - 2.0f * padding.x) {
        visible.erase(visible.getSize() - 1, 1);
        input.setString(visible);
    }
    input.setPosition({std::round(bounds.position.x + padding.x),
                       std::round(bounds.position.y + padding.y)});
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

float MainMenuScale(const sf::RenderWindow& window)
{
    const auto size = sf::Vector2f(window.getSize());
    return std::max(0.001f, std::min(size.x / 800.0f, size.y / 600.0f));
}

sf::View MainMenuView(const sf::RenderWindow& window)
{
    // Keep one view unit equal to one pixel; rasterize glyphs at their final size.
    return sf::View(sf::FloatRect({0.0f, 0.0f}, sf::Vector2f(window.getSize())));
}

struct MainMenuLayout
{
    sf::FloatRect panel;
    sf::FloatRect fullscreen;
    sf::FloatRect start;
    sf::FloatRect resume;
    sf::FloatRect path;
    sf::FloatRect load;
    sf::FloatRect save;
    sf::FloatRect exit;
};

MainMenuLayout MainMenuBounds(const sf::RenderWindow& window)
{
    const auto size = sf::Vector2f(window.getSize());
    const float scale = MainMenuScale(window);
    const float resume_height = Interface().has_active_game ? 52.0f : 0.0f;
    const sf::Vector2f origin = (size - sf::Vector2f(440.0f, 410.0f + resume_height) * scale) / 2.0f;
    const auto rectangle = [&](float x, float y, float width, float height) {
        return sf::FloatRect({std::round(origin.x + x * scale), std::round(origin.y + y * scale)},
                             {std::round(width * scale), std::round(height * scale)});
    };
    return {
        rectangle(0.0f, 0.0f, 440.0f, 410.0f + resume_height),
        rectangle(24.0f, 82.0f, 392.0f, 40.0f),
        rectangle(24.0f, 134.0f, 392.0f, 44.0f),
        rectangle(24.0f, 186.0f, 392.0f, 44.0f),
        rectangle(24.0f, 226.0f + resume_height, 392.0f, 32.0f),
        rectangle(24.0f, 274.0f + resume_height, 190.0f, 44.0f),
        rectangle(226.0f, 274.0f + resume_height, 190.0f, 44.0f),
        rectangle(24.0f, 326.0f + resume_height, 392.0f, 44.0f)
    };
}

void ToggleFullscreen(sf::RenderWindow& window)
{
    auto& ui = Interface();
    const auto view = window.getView();
    if (!ui.fullscreen) {
        ui.windowed_size = window.getSize();
        ui.windowed_position = window.getPosition();
    }
    ui.fullscreen = !ui.fullscreen;
    ui.save_editing = false;
    ui.font.reset();
    window.create(ui.fullscreen ? sf::VideoMode::getDesktopMode() : sf::VideoMode(ui.windowed_size),
                  "PROJECT ASTEROID", sf::Style::Default,
                  ui.fullscreen ? sf::State::Fullscreen : sf::State::Windowed);
    if (!ui.fullscreen) window.setPosition(ui.windowed_position);
    window.setFramerateLimit(60);
    auto resized_view = view;
    resized_view.setSize(sf::Vector2f(window.getSize()) * ui.camera_zoom);
    window.setView(resized_view);
    LoadInterfaceFont();
}

void LoadGameFromMenu(sf::RenderWindow& window, World* world)
{
    auto& ui = Interface();
    if (ui.save_path.isEmpty()) return;
    const auto utf8 = ui.save_path.toUtf8();
    world->Load(std::string(utf8.begin(), utf8.end()));
    ResetWorldInterface(window);
    ui.has_active_game = true;
    ui.main_menu = false;
}

void SaveGameFromMenu(World* world)
{
    const auto& ui = Interface();
    if (!ui.has_active_game || ui.save_path.isEmpty()) return;
    const auto utf8 = ui.save_path.toUtf8();
    world->Save(std::string(utf8.begin(), utf8.end()));
}

void HandleMainMenuEvent(sf::RenderWindow& window, World* world, const sf::Event& event)
{
    auto& ui = Interface();
    if (const auto* mouse = event.getIf<sf::Event::MouseButtonPressed>()) {
        if (mouse->button != sf::Mouse::Button::Left) return;
        const auto point = window.mapPixelToCoords(mouse->position, MainMenuView(window));
        const auto bounds = MainMenuBounds(window);
        ui.save_editing = bounds.path.contains(point);
        if (ui.save_editing) ui.save_cursor = ui.save_path.getSize();
        else if (bounds.fullscreen.contains(point)) ToggleFullscreen(window);
        else if (bounds.start.contains(point)) StartNewGame(window, world);
        else if (ui.has_active_game && bounds.resume.contains(point)) ui.main_menu = false;
        else if (bounds.load.contains(point)) LoadGameFromMenu(window, world);
        else if (bounds.save.contains(point)) SaveGameFromMenu(world);
        else if (bounds.exit.contains(point)) CloseWindow(window);
    }
    else if (const auto* key = event.getIf<sf::Event::KeyPressed>()) {
        if (key->code == sf::Keyboard::Key::Escape) ui.save_editing = false;
        else if (ui.save_editing) {
            if (key->code == sf::Keyboard::Key::Enter) LoadGameFromMenu(window, world);
            else EditInput(*key, ui.save_path, ui.save_cursor);
        }
    }
    else if (const auto* text = event.getIf<sf::Event::TextEntered>()) {
        if (!ui.save_editing) return;
        if (text->unicode == U'\b') {
            if (ui.save_cursor > 0) ui.save_path.erase(--ui.save_cursor, 1);
        }
        else if (text->unicode >= U' ' && text->unicode != 127) {
            ui.save_path.insert(ui.save_cursor++, sf::String(text->unicode));
        }
    }
}

void DrawMainMenu(sf::RenderWindow& window)
{
    auto& ui = Interface();
    if (!ui.font || window.getSize().x == 0 || window.getSize().y == 0) return;
    const auto world_view = window.getView();
    const auto menu_view = MainMenuView(window);
    const auto bounds = MainMenuBounds(window);
    const float scale = MainMenuScale(window);
    const auto font_size = [scale](unsigned int size) {
        return static_cast<unsigned int>(std::max(1.0f, std::round(size * scale)));
    };
    window.setView(menu_view);
    const auto panel = [&](const sf::FloatRect& rectangle, sf::Color color) {
        sf::RectangleShape shape(rectangle.size);
        shape.setPosition(rectangle.position);
        shape.setFillColor(color);
        window.draw(shape);
    };
    const auto button = [&](const sf::FloatRect& rectangle, const sf::String& title, bool enabled = true) {
        const bool hovered = enabled && window.hasFocus() && rectangle.contains(
            window.mapPixelToCoords(sf::Mouse::getPosition(window), menu_view));
        panel(rectangle, !enabled ? sf::Color(30, 34, 40)
                                 : hovered ? sf::Color(65, 85, 105) : sf::Color(42, 54, 68));
        sf::Text text(*ui.font, title, font_size(18));
        const auto local = text.getLocalBounds();
        const auto position = rectangle.position + (rectangle.size - local.size) / 2.0f - local.position;
        text.setPosition({std::round(position.x), std::round(position.y)});
        text.setFillColor(enabled ? sf::Color::White : sf::Color(120, 130, 145));
        window.draw(text);
    };
    panel(bounds.panel, sf::Color(20, 24, 30));
    sf::Text title(*ui.font, "PROJECT ASTEROID", font_size(26));
    const auto title_bounds = title.getLocalBounds();
    title.setPosition({std::round(bounds.panel.position.x + (bounds.panel.size.x - title_bounds.size.x) / 2.0f -
                                     title_bounds.position.x),
                       std::round(bounds.panel.position.y + 24.0f * scale)});
    window.draw(title);
    button(bounds.fullscreen, ui.fullscreen ? sf::String(U"В оконный режим") : sf::String(U"На весь экран"));
    button(bounds.start, sf::String(U"Начать новую игру"));
    if (ui.has_active_game) button(bounds.resume, sf::String(U"Вернуться в игру"));
    sf::Text label(*ui.font, sf::String(U"Путь к файлу сохранения"), font_size(text_size));
    label.setPosition({bounds.path.position.x, std::round(bounds.path.position.y - 24.0f * scale)});
    label.setFillColor(sf::Color(180, 190, 205));
    window.draw(label);
    panel(bounds.path, ui.save_editing ? sf::Color(46, 61, 75) : sf::Color(35, 40, 48));
    DrawInput(window, bounds.path, ui.save_path, ui.save_cursor, ui.save_editing, sf::Color::White,
              font_size(text_size), sf::Vector2f(8.0f, 5.0f) * scale);
    button(bounds.load, sf::String(U"Загрузить игру"), !ui.save_path.isEmpty());
    button(bounds.save, sf::String(U"Сохранить игру"), ui.has_active_game && !ui.save_path.isEmpty());
    button(bounds.exit, sf::String(U"Выйти из игры"));
    window.setView(world_view);
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
    const auto fitted_label = [&](sf::String value, sf::Vector2f position, float width, sf::Color color) {
        sf::Text text(*ui.font, value, text_size);
        while (!value.isEmpty() && text.getLocalBounds().size.x > width) {
            value.erase(value.getSize() - 1, 1);
            text.setString(value);
        }
        text.setPosition(position);
        text.setFillColor(color);
        window.draw(text);
    };
    const auto button = [&](sf::FloatRect bounds, const sf::String& title, bool enabled = true) {
        const bool hovered = enabled && window.hasFocus() &&
            bounds.contains(sf::Vector2f(sf::Mouse::getPosition(window)));
        panel(bounds, !enabled ? sf::Color(30, 34, 40)
                              : hovered ? sf::Color(65, 85, 105) : sf::Color(42, 54, 68));
        sf::Text text(*ui.font, title, text_size);
        auto local = text.getLocalBounds();
        const float width = std::max(1.0f, bounds.size.x - 6.0f);
        if (local.size.x > width) {
            text.setCharacterSize(static_cast<unsigned int>(std::max(1.0f, std::floor(text_size * width / local.size.x))));
            local = text.getLocalBounds();
        }
        const auto position = bounds.position + (bounds.size - local.size) / 2.0f - local.position;
        text.setPosition({std::round(position.x), std::round(position.y)});
        text.setFillColor(enabled ? sf::Color::White : sf::Color(120, 130, 145));
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
        label(EntityLabel(world->GetEntities().at(ids[row]).get()),
              {12.0f, y + 4.0f}, selected ? sf::Color(255, 215, 70) : sf::Color::White);
    }

    panel({{sidebar_width, 0.0f}, {size.x - sidebar_width, 36.0f}}, sf::Color(20, 24, 30, 245));
    if (auto* entity = SelectedEntity(world)) {
        auto* craft = dynamic_cast<Craft*>(entity);
        const bool running = craft && craft->IsScriptRunning();
        const auto bounds = InputBounds(window);
        panel({{sidebar_width, 0.0f}, {size.x - sidebar_width, 116.0f}}, sf::Color(20, 24, 30, 245));
        fitted_label(EntityLabel(entity) +
                  (craft ? (running ? " | Running" : " | Stopped") : " | This entity cannot run scripts"),
              {bounds.position.x, 12.0f}, MainMenuButtonBounds(window).position.x - bounds.position.x - 12.0f,
              running ? sf::Color(110, 220, 140) : sf::Color::White);
        if (craft) {
            panel(bounds, ui.editing ? sf::Color(46, 61, 75) : sf::Color(35, 40, 48));
            DrawInput(window, bounds, ui.script_path, ui.cursor, ui.editing, sf::Color::White);
            const bool has_path = !ui.script_path.isEmpty();
            if (!has_path) {
                const float padding = ui.editing ? 20.0f : 8.0f;
                fitted_label("Python script path (Enter to run)", bounds.position + sf::Vector2f(padding, 5.0f),
                             bounds.size.x - padding - 8.0f, sf::Color(150, 160, 175));
            }
            const bool can_edit = has_path || !AttachedScriptPath(*craft).isEmpty();
            const bool can_restart = can_edit || !ui.crafts[craft->GetId()].launched_path.isEmpty();
            const std::array enabled{has_path, running, can_restart, can_edit, true};
            for (std::size_t i = 0; i < script_actions.size(); ++i)
                button(ScriptButtonBounds(window, i), script_action_titles[i], enabled[i]);
        }
    }
    button(MainMenuButtonBounds(window), sf::String(U"В главное меню"));
    button(ZoomBounds(window, true), "+", ui.camera_zoom > 0.1f);
    button(ZoomBounds(window, false), "-", ui.camera_zoom < 8.0f);

    const float top = LogTop(window);
    panel({{0.0f, top}, {size.x, size.y - top}}, sf::Color(18, 20, 25, 250));
    const auto* target = ConsoleTarget(world);
    const sf::String console_title = target ? sf::String("Console -> ") + EntityLabel(target)
                                            : sf::String("Console | Select a craft");
    fitted_label(stderr_log.IsActive() ? console_title + " | Enter: send | Up/Down: history"
                               : "Console capture failed",
          {12.0f, top + 6.0f}, ClearConsoleBounds(window).position.x - 24.0f,
          sf::Color(180, 190, 205));
    button(ClearConsoleBounds(window), "Clear");
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

    if (ui.editor) {
        auto& editor = *ui.editor;
        const auto bounds = ScriptEditorBounds(window);
        panel({{0.0f, 0.0f}, size}, sf::Color(8, 12, 17, 195));
        panel(bounds.panel, sf::Color(29, 36, 46));
        const sf::String title = sf::String(editor.modified ? "* " : "") + "Edit code  |  " + ui.script_path;
        fitted_label(title, bounds.panel.position + sf::Vector2f(12.0f, 10.0f),
                     bounds.panel.size.x - 24.0f, sf::Color(230, 235, 243));
        panel(bounds.code, sf::Color(20, 25, 33));
        DrawEditorLines(window, editor, bounds.code);
        const sf::String status = editor.confirm_discard ? sf::String("Unsaved changes. Save, discard, or press Esc to keep editing.")
            : !editor.message.isEmpty() ? editor.message : sf::String("Ctrl/Cmd+S: save  |  Tab: indent  |  Esc: close");
        fitted_label(status, bounds.status, bounds.panel.size.x - 24.0f,
                     editor.confirm_discard ? sf::Color(255, 199, 114) : sf::Color(165, 180, 201));
        button(bounds.save, "Save", editor.modified);
        button(bounds.close, editor.confirm_discard ? "Discard" : "Close");
    }
    window.setView(world_view);
}
} // namespace

bool IsMainMenuOpen()
{
    return Interface().main_menu;
}

sf::RenderWindow CreateGameWindow()
{
    sf::RenderWindow window(sf::VideoMode({800, 600}), "PROJECT ASTEROID");
    window.setFramerateLimit(60);
    LoadInterfaceFont();
    sf::View view = window.getView();
    view.setCenter({0.0f, 0.0f});
    window.setView(view);
    Interface().camera_zoom = 1.0f;
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
            if (ui.editor) RequestCloseEditor(window, true);
            else CloseWindow(window);
            if (!window.isOpen()) return;
        }
        else if (const auto* resized = event->getIf<sf::Event::Resized>()) {
            if (resized->size.x > 0 && resized->size.y > 0) {
                sf::View view = window.getView();
                view.setSize({static_cast<float>(resized->size.x) * ui.camera_zoom,
                              static_cast<float>(resized->size.y) * ui.camera_zoom});
                window.setView(view);
            }
        }
        else if (event->is<sf::Event::FocusLost>()) {
            ui.editing = false;
            ui.console_editing = false;
            ui.save_editing = false;
            if (ui.editor) ui.editor->dragging = false;
        }
        else if (ui.main_menu) {
            HandleMainMenuEvent(window, world, *event);
            if (!window.isOpen()) return;
        }
        else if (ui.editor) {
            HandleScriptEditorEvent(window, *event);
            if (!window.isOpen()) return;
        }
        else if (const auto* mouse = event->getIf<sf::Event::MouseButtonPressed>()) {
            if (mouse->button != sf::Mouse::Button::Left) continue;
            const auto point = sf::Vector2f(mouse->position);
            if (ClearConsoleBounds(window).contains(point)) {
                stderr_log.Clear();
                ui.log_rows.clear();
                ui.log_scroll = 0;
                continue;
            }
            if (point.y >= LogTop(window)) {
                ui.editing = false;
                ui.console_editing = true;
                ui.console_cursor = ui.console_input.getSize();
                continue;
            }
            ui.console_editing = false;
            if (MainMenuButtonBounds(window).contains(point)) {
                ReturnToMainMenu();
                camera_clock.restart();
                continue;
            }
            if (ZoomBounds(window, true).contains(point) || ZoomBounds(window, false).contains(point)) {
                ui.editing = false;
                ZoomCamera(window, ZoomBounds(window, true).contains(point) ? 0.8f : 1.25f);
                continue;
            }
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
            if (point.y < 116.0f) {
                if (auto* entity = SelectedEntity(world)) {
                    auto* craft = dynamic_cast<Craft*>(entity);
                    ui.editing = craft && InputBounds(window).contains(point);
                    ui.cursor = ui.script_path.getSize();
                    if (craft) {
                        for (std::size_t i = 0; i < script_actions.size(); ++i) {
                            if (!ScriptButtonBounds(window, i).contains(point)) continue;
                            switch (script_actions[i]) {
                            case ScriptAction::Run: LaunchSelectedScript(world, false); break;
                            case ScriptAction::Stop:
                                if (craft->IsScriptRunning()) StopSelectedScript(*craft);
                                break;
                            case ScriptAction::Restart: LaunchSelectedScript(world, true); break;
                            case ScriptAction::Edit: OpenScriptEditor(*craft); break;
                            case ScriptAction::Create: CreateScript(*craft); break;
                            }
                            break;
                        }
                    }
                    continue;
                }
            }
            if (point.y < 36.0f) {
                ui.editing = false;
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
                if (!ui.console_editing) SelectEntity(window, world, std::nullopt);
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
                    LaunchSelectedScript(world, false);
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

    if (ui.main_menu || !window.isOpen() || !window.hasFocus() || ui.editor ||
        ui.editing || ui.console_editing || ui.selected) {
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
    auto& ui = Interface();
    if (ui.main_menu) {
        window.clear(sf::Color(10, 14, 20));
        DrawMainMenu(window);
        window.display();
        return;
    }
    for (auto it = ui.crafts.begin(); it != ui.crafts.end();) {
        if (world->GetEntities().find(it->first) == world->GetEntities().end())
            it = ui.crafts.erase(it);
        else ++it;
    }
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

    sf::RectangleShape shipyard({32.0f, 24.0f});
    shipyard.setOrigin({16.0f, 12.0f});
    shipyard.setFillColor(sf::Color(55, 110, 155));
    shipyard.setOutlineColor(sf::Color(120, 195, 235));
    shipyard.setOutlineThickness(2.0f);

    // An uneven rock silhouette, kept inside the selection circle.
    sf::ConvexShape asteroid(8);
    asteroid.setPoint(0, {14.0f, 0.0f});
    asteroid.setPoint(1, {10.0f, 10.0f});
    asteroid.setPoint(2, {1.0f, 14.0f});
    asteroid.setPoint(3, {-10.0f, 11.0f});
    asteroid.setPoint(4, {-14.0f, 1.0f});
    asteroid.setPoint(5, {-10.0f, -9.0f});
    asteroid.setPoint(6, {0.0f, -13.0f});
    asteroid.setPoint(7, {11.0f, -8.0f});
    asteroid.setFillColor(sf::Color(105, 100, 90));
    asteroid.setOutlineColor(sf::Color(175, 165, 145));
    asteroid.setOutlineThickness(1.5f);

    for (const auto& entity : world->GetEntities())
    {
        Vector2 pos = entity.second->GetPosition();

        if (entity.second->GetType() == ENTITY_TYPE::ASTEROID) {
            // Stable variation without changing geometry randomly each frame.
            const auto id = entity.second->GetId();
            const float scale = 0.85f + static_cast<float>(id % 4) * 0.05f;
            asteroid.setScale({scale, scale});
            asteroid.setPosition({pos.x, -pos.y});
            asteroid.setRotation(sf::radians(-entity.second->GetRotation()) +
                                 sf::degrees(static_cast<float>((id % 360) * 37 % 360)));
            window.draw(asteroid);
        } else {
            auto* craft = static_cast<Craft*>(entity.second.get());
            const bool is_shipyard = !craft->GetModulesOfType(MODULE_TYPE::SHIPYARD).empty();
            float throttle = 0.0f;
            for (auto* module : craft->GetModulesOfType(MODULE_TYPE::PROPULSION))
                throttle = std::max(throttle, static_cast<PropulsionModule*>(module)->GetThrottle());
            throttle = std::clamp(throttle, 0.0f, 1.0f);
            if (throttle > 0.0f) {
                const float rear = is_shipyard ? -18.0f : -8.0f;
                const float length = 10.0f + 32.0f * throttle;
                const float width = 2.0f + 3.0f * throttle;
                sf::ConvexShape exhaust(3);
                exhaust.setPoint(0, {rear, -width});
                exhaust.setPoint(1, {rear - length, 0.0f});
                exhaust.setPoint(2, {rear, width});
                exhaust.setPosition({pos.x, -pos.y});
                exhaust.setRotation(sf::radians(-craft->GetRotation()));
                exhaust.setFillColor(sf::Color(255, 45, 35, 200));
                window.draw(exhaust);
                exhaust.setPoint(0, {rear, -width * 0.5f});
                exhaust.setPoint(1, {rear - length * 0.55f, 0.0f});
                exhaust.setPoint(2, {rear, width * 0.5f});
                exhaust.setFillColor(sf::Color(255, 155, 100, 235));
                window.draw(exhaust);
            }
            if (is_shipyard)
            {
                shipyard.setPosition({pos.x, -pos.y});
                shipyard.setRotation(sf::radians(-craft->GetRotation()));
                window.draw(shipyard);
            }
            else
            {
                arrow.setPosition({pos.x, -pos.y});
                arrow.setRotation(sf::radians(-craft->GetRotation()));
                window.draw(arrow);
            }
        }
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
