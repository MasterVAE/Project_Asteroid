# Project Asteroid

C++17-сервер мира и браузерный/Python-клиенты единого HTTP/WebSocket API.

```text
backend/include/        Модель мира, авторизация и интерфейс API
backend/src/            Симуляция, SQLite, общий диспетчер команд, HTTP/WebSocket
frontend/python/        SDK AsteroidAPI и консольный клиент
frontend/browser/       Карта с интерполяцией, регистрация, вход, API-ключи
docs/authentication.md  Протокол, примеры SDK и настройки безопасности
tests/                 Интеграционные проверки
Makefile                Сборка в build/app
```

## Сборка и запуск

На Ubuntu установите Boost.JSON, OpenSSL, SQLite и инструменты сборки:

```sh
sudo apt update
sudo apt install build-essential pkg-config libboost-all-dev libssl-dev libsqlite3-dev
make
ASTEROID_PUBLIC_ORIGIN=https://46.173.218.137 ./build/app world.json
```

На macOS: `brew install boost openssl sqlite pkg-config`, затем `make`.
`ASTEROID_PUBLIC_ORIGIN` задаёт точный публичный origin без завершающего `/`.
Он обязателен. В systemd укажите его через `Environment=`.

Для локальной разработки:

```sh
ASTEROID_PUBLIC_ORIGIN=http://127.0.0.1:8080 ASTEROID_DEV_AUTH=1 ./build/app world.json
```

Откройте `http://127.0.0.1:8080`. HTTP с авторизацией допускается только в явном
локальном режиме; публичный сервер работает через HTTPS/WSS.

Запускайте приложение из корня проекта: оно раздаёт `frontend/browser`. Путь к
файлам клиента можно передать вторым аргументом. Аккаунты сохраняются в
`data/auth.sqlite3`; другой путь задаётся через `ASTEROID_AUTH_DB`.
Подробности хранения и резервного копирования: [регистрация и API](docs/authentication.md).

`LoadWorld` пока создаёт случайный мир из 100 астероидов и 10 кораблей. Загрузка
содержимого `world.json` ещё не реализована; отсутствие файла не мешает запуску.
Мир непрерывно обновляется в основном потоке с реальным `delta_time`, HTTP и
WebSocket обслуживаются отдельно с синхронизацией доступа к миру.

## HTTPS/WSS

C++-сервер слушает **127.0.0.1:8080**. Публичные подключения принимает Nginx на
порту 443 с доверенным TLS-сертификатом. Для авторизации Nginx должен сохранять
эти заголовки при передаче запроса в `location /`:

```nginx
proxy_pass http://127.0.0.1:8080;
proxy_http_version 1.1;
proxy_set_header Host $host;
proxy_set_header X-Forwarded-Proto $scheme;
proxy_set_header X-Real-IP $remote_addr;
proxy_set_header Upgrade $http_upgrade;
proxy_set_header Connection $asteroid_connection;
proxy_read_timeout 300s;
```

Переменная для WebSocket задаётся в контексте `http`, например перед блоками
`server` в файле `/etc/nginx/sites-available/default`:

```nginx
map $http_upgrade $asteroid_connection {
    default upgrade;
    ''      close;
}
```

Значение `ASTEROID_PUBLIC_ORIGIN` должно совпадать с адресом в браузере и `Host`.
Для нестандартного публичного порта передавайте его в Host явно. Порт 8080
остаётся локальным. Сохраните на порту 80 маршрут `/.well-known/acme-challenge/`
для продления сертификата, если он выпущен через Certbot webroot.

## Сайт и Python

Откройте `https://46.173.218.137` (или свой публичный origin), создайте аккаунт
и войдите. Сессия восстанавливается после перезагрузки страницы. Через
«API-ключи» можно создать отдельный ключ Python-бота и отозвать его.

«Создать корабль» добавляет собственный корабль. «Мои корабли» открывает меню всего
вашего флота; нажатие на строку показывает корабль на карте. Свои корабли голубые,
все остальные — жёлтые. Список автоматически обновляется вместе с картой.

Карта обновляется раз в секунду; положение и направление кораблей плавно
интерполируются через `requestAnimationFrame` с задержкой примерно в один снимок.
При потере связи последняя отображённая карта сохраняется, клиент переподключается.
Выход из аккаунта очищает карту и завершает текущую сессию, сохраняя работу ботов.

Перетаскивайте карту мышью или пальцем, меняйте масштаб колесом или кнопками
`+`/`−`, нажмите «Вся карта» для обзора. Клик по объекту показывает параметры.
Сфокусированная карта поддерживает стрелки, `+`, `−` и `F`. «Полный экран»
работает при поддержке этого режима браузером. Для другого сервера откройте его
собственный сайт: регистрация и cookie работают на одном origin.

```sh
python3 -m pip install -r frontend/python/requirements.txt
python3 frontend/python/asteroidAPI.py --username alice
```

SDK поддерживают одинаковые методы: `register`, `login`, `me`, `connect`,
`getMap`, `getMyEntities`, `getEntity`, `createCraft`, `flyTo`, `createApiKey`, `listApiKeys`, `revokeApiKey`, `logout`,
`disconnect`. В JS они асинхронные, в Python — синхронные. Игровые команды
обслуживает единый диспетчер, результаты передаются в конверте
`{"ok":true,"result":...}`, ошибки — `{"ok":false,"error":...}`.

[Полный контракт, примеры Python/JS и границы реализации](docs/authentication.md).
Доступны аккаунты, общая карта, постоянный Principal владельца Entity и создание
кораблей через `create_craft` с сессионной авторизацией. Компании и экономические
правила создания кораблей ещё предстоит реализовать.

## Проверки

```sh
make
make test-auth-ids test-principals
node tests/browser_fleet.test.mjs
```

Тесты используют временные SQLite-базы и мир в памяти. Проверяют резервирование
ID 0, стабильность Principal, параллельные сессии/ключи и владельцев кораблей.
Сетевые порты и публичный сервер не затрагиваются.
Браузерный тест проверяет меню флота, цвета, создание и переключение аккаунтов
в моделируемом DOM; Node.js требуется только для запуска этого теста.
