# Игроки и подключения

Один объект `World` создаётся в `main.cpp` и передаётся игровому и веб-серверу.
В `backend/include/game_server.hpp` у него есть два контейнера:

```cpp
std::unordered_map<uint32_t, Player> players;
std::unordered_map<uint64_t, Connection*> connections;
```

`players` содержит всех зарегистрированных игроков по ID из SQLite, включая
игроков не в сети. Они загружаются при запуске через `Accounts::LoadPlayers`;
новый игрок добавляется после успешной регистрации. Игрок остаётся в контейнере
после выхода и отключения. Игровые поля можно добавлять в класс `Player`
в `backend/include/player.hpp`. Пока в SQLite сохраняются только данные аккаунта.

`connections` содержит все открытые WebSocket-подключения, включая тех, кто ещё
не вошёл. Обычные HTTP-запросы в этот контейнер не входят. ID подключения
выдаётся последовательно в пределах запуска сервера и не совпадает по смыслу с ID игрока.

Связь хранится в двух указателях:

```cpp
player.connection  // Connection* или nullptr, если игрок не в сети.
connection.player  // Player* или nullptr, если подключение не авторизовано.
```

Для связанной пары всегда выполняется `player.connection->player == &player`.
У игрока может быть только одно подключение, у подключения — только один игрок.
Повторный вход в аккаунт из другого соединения отклоняется; сначала нужно выйти
из первого. Поэтому соответствие один к одному действует между игроками в сети
и авторизованными подключениями. У остальных указатели равны `nullptr`.

## Прочитать или изменить игрока

Сетевые обработчики и игровой цикл работают в разных потоках. Для доступа
к контейнерам, указателям и данным игроков удерживайте `world.mutex`:

```cpp
std::lock_guard<std::mutex> lock(world.mutex);
auto it = world.players.find(player_id);
if (it != world.players.end()) {
    Player& player = it->second;
    std::cout << player.username << '\n';
    if (player.connection) {
        std::cout << "Подключение: " << player.connection->id << '\n';
    }
}
```

В `UpdateGameServer(World* world, float delta_time)` можно обойти игроков:

```cpp
std::lock_guard<std::mutex> lock(world->mutex);
for (auto& [id, player] : world->players) {
    if (!player.connection) continue;
    // Обновить игровые данные player.
}
```

Все подключения, в том числе без авторизации:

```cpp
std::lock_guard<std::mutex> lock(world.mutex);
for (const auto& [id, connection] : world.connections) {
    if (connection->player) {
        std::cout << id << ": " << connection->player->username << '\n';
    }
}
```

В `handle_packet` уже доступен `Connection& connection`. Для будущего игрового
запроса нужно передать туда ещё `World& world` и работать так:

```cpp
std::lock_guard<std::mutex> lock(world.mutex);
if (!connection.player) return protocol::error("Сначала войдите.");
Player& player = *connection.player;
// Изменить игровые поля player; ID из браузера для выбора игрока не нужен.
```

## Создание и удаление связей

Обычно вызывать эти методы вручную не нужно: вход, регистрация и выход уже
подключены в `handle_packet`.

```cpp
connection.IsLoggedIn();          // Есть ли связанный игрок.
connection.Login(id, username);   // После проверки пароля; false, если одна из сторон занята.
connection.Logout();              // Обнулить оба указателя, оставить WebSocket открытым.
```

Эти методы сами блокируют `world.mutex`. Вызывайте их **без уже захваченного
мьютекса**, иначе поток заблокируется повторно. Проверку пароля выполняет
`Accounts::authenticate`; сам `Connection::Login` только устанавливает связь.

`Connection` создаётся локально в `serve_websocket` после успешного WebSocket
handshake. Конструктор добавляет `this` в `world.connections`. При закрытии сокета,
сетевой ошибке или исключении деструктор обнуляет оба указателя и удаляет запись
из `world.connections`. Игрок при этом остаётся в `world.players`.

Контейнер `players` владеет объектами игроков. Указатели в `connections` и поля
`player`/`connection` не владеют объектами: вызывать для них `delete` не нужно.
Добавление новых игроков в `unordered_map` не меняет адреса существующих игроков.
Объекты `Connection` копировать нельзя.

Связи меняйте через `Login`/`Logout`, контейнер подключений — через время жизни
`Connection`. Не удаляйте игрока, пока он связан с подключением. Указатель на
подключение используйте только под `world.mutex`: после снятия блокировки
соединение может закрыться и объект исчезнуть. Долгие операции выполняйте вне
блокировки, предварительно скопировав нужные ID/данные. `World` должен жить
дольше всех подключений; сейчас он существует до завершения процесса.
