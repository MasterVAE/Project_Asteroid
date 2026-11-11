#include <boost/asio.hpp>
#include <boost/process/v2/popen.hpp>
#include <boost/process/v2/environment.hpp>
#include <boost/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#include <string>
#include <iostream>
#include <random>

#include "game.hpp"
#include "vector.hpp"

namespace asio = boost::asio;
namespace bp = boost::process::v2;
namespace json = boost::json;

using namespace std::chrono_literals;

float RandomFloat(float min, float max) {
    static std::random_device rd;
    static std::mt19937 gen(rd());

    std::uniform_real_distribution<float> dist(min, max);
    return dist(gen);
}

Vector2 RandomVector(float min, float max)
{
    return Vector2(RandomFloat(min ,max), RandomFloat(min, max));
}






// World
World::World() : next_id_(0) {
    for(int i = 0; i < 10; i++)
    {
        Craft c(RandomVector(-500, 500));
        c.SetVelocity(RandomVector(-5, 5));
        SpawnEntity(c);
    }
}
World::World(std::string filename) {}
void World::Update(float delta_time) {
    for(auto& entity : entities_)
    {
        entity.second.SetPosition(entity.second.GetPosition() + entity.second.GetVelocity() * delta_time);
        entity.second.Update(delta_time);
    }
}

void World::SpawnEntity(Entity entity)
{
    std::size_t id = next_id_++;
    entity.SetId(id);
    entities_[id] = entity;
}




// Craft
Craft::Craft() : Entity() {}
Craft::Craft(Vector2 pos) : Entity(pos, Vector2(0), 0, 0) {}

void Craft::Send(const json::object& message)
{
    if (!process_ || !process_->running()) {
        return;
    }

    std::string packet = json::serialize(message) + "\n";

    std::lock_guard lock(send_mutex_);

    asio::write(
        process_->get_stdin(),
        asio::buffer(packet)
    );
}

void Craft::SendEvent(const json::object& event_data)
{
    Send(event_data);
}


void Craft::LaunchScript(std::string filename)
{
    StopScript();

    try {
        auto python = bp::environment::find_executable("python3");
        if (python.empty()) {
            std::cerr << "Python not found\n";
            return;
        }

        // Инициализируем процесс как поле класса
        process_.emplace(io_, python, std::vector<std::string>{"-u", filename});

        // В потоке захватываем this вместо локальных переменных
        reader_ = std::thread([this]() {
            asio::streambuf buffer;

            while (true) {
                boost::system::error_code ec;
                asio::read_until(process_->get_stdout(), buffer, '\n', ec);
                if (ec) break;

                // ... парсинг JSON ...
                // Внутри RPC-запроса вызываем метод класса:
                // Send(response);
            }
        });

        Send(json::object{
            {"type", "event"},
            {"name", "enemy_detected"},
            {"data", json::object{{"enemy_id", 42}}}
        });

        // Примечание: если убрать отсюда shutdown, wait() и join() 
        // (перенеся их в деструктор ~Craft()), вы сможете вызывать 
        // SendEvent() в любой момент во время работы игры.

    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        // Внимание: метод возвращает void, поэтому return 1; здесь вызовет ошибку компиляции
    }
}


Craft::~Craft()
{
    StopScript();
}

void Craft::StopScript()
{
    // 1. Отправляем команду завершения, если процесс ещё работает
    if (process_ && process_->running()) {
        try {
            Send(json::object{{"type", "shutdown"}});
        } catch (...) {
            // Игнорируем ошибки записи, если Python уже закрыл stdin
        }
    }

    // 2. Закрываем stdin процесса, чтобы Python гарантированно вышел 
    // из ожидания ввода, даже если не обработал JSON "shutdown"
    if (process_) {
        boost::system::error_code ec;
        process_->get_stdin().close(ec);
    }

    // 3. Ждём завершения фонового потока чтения, пока поля класса ещё живы
    if (reader_.joinable()) {
        reader_.join();
    }

    // 4. Дожидаемся полного завершения процесса в ОС (убираем зомби-процесс)
    if (process_) {
        boost::system::error_code ec;
        process_->wait(ec);
        process_.reset();
    }
}

void Craft::Update(float delta_time)
{
    
}



// Entity

Entity::Entity() : position_(0), velocity_(0), rotation_(0), angular_velocity_(0) {}
Entity::Entity(Vector2 pos, Vector2 vel, float rot, float a_vel)
                    : position_(pos), velocity_(vel), rotation_(rot), angular_velocity_(a_vel) {}
