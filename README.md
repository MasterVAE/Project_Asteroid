# Asteroid

Минимальный C++17 сервер и браузерный клиент без сборщика JavaScript.

Зависимости: Boost (Beast/Asio, заголовки), OpenSSL, SQLite3, pkg-config.
На macOS: `brew install boost openssl pkg-config`; SQLite доступна в системе.
На Debian/Ubuntu: `sudo apt install g++ make pkg-config libboost-dev libssl-dev libsqlite3-dev`.

Из корня проекта:

```sh
make run
```

Открыть http://127.0.0.1:8080. После регистрации пользователь сразу входит
и получает стартовое судно. Карта показывает суда всех игроков, обновляется сервером
10 раз в секунду; судно можно выбрать на карте или в списке и посмотреть информацию.
Аккаунты сохраняются между перезапусками в `data/accounts.sqlite3`.
Пароли: PBKDF2-HMAC-SHA256, 600000 итераций, случайная соль 16 байт.
Сессия привязана к WebSocket; после обновления страницы или потери соединения нужен новый вход.

## HTTPS

Если HTTPS уже настроен на обратном прокси, направьте `/` и `/ws` на
`http://127.0.0.1:8080`. Для `/ws` прокси должен поддерживать WebSocket Upgrade.
Браузер автоматически выбирает `wss://` при открытии страницы через HTTPS.
Например, внутри существующего HTTPS-блока nginx:

```nginx
location / {
    proxy_pass http://127.0.0.1:8080;
    proxy_http_version 1.1;
    proxy_set_header Upgrade $http_upgrade;
    proxy_set_header Connection "upgrade";
    proxy_set_header Host $host;
    proxy_read_timeout 1d;
}
```

Для прямого HTTPS/WSS без прокси укажите существующие сертификаты:

```sh
ASTEROID_TLS_CERT=/path/fullchain.pem ASTEROID_TLS_KEY=/path/privkey.pem make run
```

Тогда адрес — https://127.0.0.1:8080 (используйте домен из сертификата).
Доступные переменные: `ASTEROID_ADDRESS` (по умолчанию `127.0.0.1`),
`ASTEROID_PORT` (`8080`), `ASTEROID_FRONTEND` (`frontend/browser`),
`ASTEROID_DB` (`data/accounts.sqlite3`), `ASTEROID_TLS_CERT`, `ASTEROID_TLS_KEY`.
Для прямого доступа с других устройств задайте `ASTEROID_ADDRESS=0.0.0.0`.

## Где менять запросы

- `backend/include/protocol.hpp` — типы, структуры ответа, чтение и запись бинарных полей.
- `frontend/browser/protocol.js` — `loginRequest`, `registerRequest`, `logoutRequest`, `decodeResponse`.
- `backend/src/web_server.cpp` → `handle_packet` — обработка запросов и вошедший игрок в `connection.player`.
- `backend/src/accounts.cpp` — регистрация, проверка пароля и SQLite.
- `frontend/browser/app.js` — отправка запросов, переключение экранов, отрисовка поля.

Описание пакетов: [docs/protocol.md](docs/protocol.md). При изменении полей
меняйте обе стороны вместе. C++ структуры не копируются в сеть через `memcpy`:
каждое поле записывается явно, без выравнивания и зависимости от платформы.

Игроки и подключения хранятся в `World`: `players` и `connections`, со взаимными
указателями `player.connection` / `connection.player`. Описание и примеры:
[docs/players.md](docs/players.md).

Сетевой код использует простой поток на соединение. Игровой цикл работает отдельно.
Основа WebSocket — [Boost.Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/using_websocket.html).

К своему судну можно прикрепить Python-файл до 1 МиБ, остановить его и прочитать журнал.
Для запуска скриптов нужен Docker Engine / Docker Desktop и предварительно собранный образ:

```sh
make sandbox-image
make run
```

Без Docker скрипт получает состояние «Ошибка»; сервер не запускает его в Python на хосте.
Канал связи и точка расширения: [docs/scripts.md](docs/scripts.md).
Суда, координаты и прикреплённый код пока находятся в памяти; после перезапуска
восстанавливаются аккаунты и создаются стартовые суда, скрипты нужно загрузить заново.

Проверки: `make test-core` — C++ сериализация, мир и транспорт скриптов;
`make test-protocol` — C++/JS протокол (нужен Node.js);
`make test-integration` — временные WS/WSS-серверы, два игрока, рассылка мира,
проверка владельца, загрузка 1 МиБ, остановка/ошибка и сохранение аккаунтов.
`make test` запускает все эти проверки. Docker в транспортных и сетевых тестах
заменён тестовым процессом, который не исполняет загруженный код.
Для сетевых тестов нужны Python 3 и `openssl`, внешние Python-пакеты не используются.
Реальный контейнер отдельно проверяет `make test-sandbox` после `make sandbox-image`.
