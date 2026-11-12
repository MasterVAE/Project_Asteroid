# Project Asteroid

## Сборка

Нужны CMake **3.24+** и компилятор C++17: Apple Clang на macOS,
GCC/Clang на Linux или Visual Studio 2022 (компонент Desktop development with C++)
на Windows. Python 3 нужен **при запуске** скриптов корабля; для компиляции игры
он не требуется. Python должен быть в `PATH` (`python3`, на Windows также `python`).

Есть два режима работы с зависимостями:

- По умолчанию используются установленные **SFML 3.x** и **Boost 1.87+**
  (компоненты JSON, Filesystem, Process v2). SFML 2.x не подходит.
- `ASTEROID_FETCH_DEPENDENCIES=ON` скачивает и собирает **SFML 3.0.2** и
  **Boost 1.87.0** статически тем же компилятором, что и игру. На Windows/macOS
  SFML также собирает FreeType 2.13.2. Первой сборке нужен интернет.
  Для загрузки FreeType нужен Git (на Windows установите Git for Windows).
  Исходники и результаты зависимостей кешируются в каталоге сборки.

### macOS

```sh
xcode-select --install  # если Command Line Tools ещё не установлены
brew install cmake sfml boost
make                   # build/release/bin/Release/app
make run               # запуск из корня проекта, где находится scripts/
make debug             # build/debug/bin/Debug/app
make run-debug
make sanitize          # отдельная Debug-сборка с ASan и UBSan
make run-sanitize
```

Makefile вызывает CMake и не зависит от Homebrew. CMake обычно сам находит
библиотеки Homebrew; при нестандартном размещении задайте
`CMAKE_ARGS='-DCMAKE_PREFIX_PATH=/путь/к/зависимостям'`.

### Linux

Сборка выполняется непосредственно на Linux-машине. Например, на Ubuntu 24.04
установите компилятор и системные зависимости SFML,
затем соберите SFML и Boost из закреплённых исходников:

```sh
sudo apt-get update
sudo apt-get install build-essential cmake git ninja-build xz-utils \
  libfreetype-dev libx11-dev libxrandr-dev libxcursor-dev libxi-dev \
  libudev-dev libgl1-mesa-dev python3
make standalone
cmake --build build/standalone --target run
```

Если подходящие SFML и Boost уже установлены, достаточно `make` / `make run`.
Результат собирается для архитектуры этой Linux-машины. При переносе на другую
машину нужны совместимые версии glibc, libstdc++, X11, OpenGL, udev и FreeType.

### Windows

В Developer PowerShell для Visual Studio 2022, из корня проекта:

```powershell
cmake --preset standalone
cmake --build --preset standalone --parallel 4
cmake --build build/standalone --config Release --target run
```

Результат: `build/standalone/bin/Release/app.exe`. Make для Windows не нужен.
При работе с готовыми зависимостями используйте preset `release` и задайте
`-DCMAKE_PREFIX_PATH=...` или toolchain vcpkg. Архитектура, компилятор и тип
runtime готовых библиотек должны совпадать с настройками приложения.

### CMake без Makefile

```sh
cmake --preset release
cmake --build --preset release --parallel 4
cmake --preset debug
cmake --build --preset debug --parallel 4
```

Также доступны presets `sanitize`, `standalone`, `windows-mingw`. Каждый
использует отдельный каталог `build/`. Можно выбрать Ninja, Xcode или Visual
Studio через `-G` при первой конфигурации нового каталога:

```sh
cmake -S . -B build/custom -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DASTEROID_FETCH_DEPENDENCIES=ON
cmake --build build/custom --parallel 4
```

### С macOS под Windows (MinGW-w64)

```sh
brew install cmake mingw-w64
make windows                  # build/windows-mingw/bin/Release/app.exe
```

Команда использует `cmake/toolchains/windows-mingw-x64.cmake`: SFML и Boost
собираются под Windows, runtime MinGW связывается статически. Запустить
полученный `.exe` можно на Windows. Число параллельных задач задаётся через
`JOBS=4`. `make windows-local` сохранён как синоним `make windows`.

Архив с исполняемым файлом, скриптами и лицензиями создаётся командой:

```sh
cmake --build build/windows-mingw --target package
# build/windows-mingw/packages/ProjectAsteroid-0.1.0-Windows-x86_64.zip
```

Поддерживаемые цели с Mac: Windows x64 и macOS.
Для другой архитектуры macOS используйте отдельный каталог и сборку зависимостей
из исходников, например на Apple Silicon для Intel:

```sh
cmake -S . -B build/macos-x64 -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 -DASTEROID_FETCH_DEPENDENCIES=ON
cmake --build build/macos-x64 --parallel 4
```

На Windows нужен OpenGL-драйвер и Python 3 для скриптов. Python не включается
в дистрибутив. На macOS минимальную версию ОС задаёт используемая версия SFML
и SDK; подпись приложения и notarization выполняются отдельно.

### Установка и архив

```sh
make install                              # dist/native/
make package                              # build/release/packages/
make package BUILD_DIR=build/standalone FETCH_DEPS=ON
make clean                                # очистить текущую сборку
make clean-all                            # удалить build/ со всеми кешами
make help
```

`make package` с системными зависимостями сохраняет ссылки на установленные
динамические библиотеки. Для распространения используйте сборку с
`FETCH_DEPS=ON`; системные библиотеки ОС всё равно нужны.
На Windows/MSVC также требуется соответствующий Visual C++ Runtime.
На Windows через CMake: `cmake --build build/standalone --config Release --target package`.
Установка через CMake:

```sh
cmake --install build/standalone --config Release --prefix dist/native --component Runtime
```

Распакуйте архив и запускайте `./app` или `.\app.exe` **из его каталога**,
чтобы относительные пути к `scripts/` разрешались корректно.

Часть распространяемого кода использует FreeType, copyright © The FreeType
Project (https://freetype.org), под лицензией FreeType License. Тексты лицензий
собираемых зависимостей включаются в `licenses/`.

## Управление кораблём из Python

Игра запускает `scripts/script.py` для корабля; API находится в [`scripts/api.py`](scripts/api.py).
Вызовите `ship.start()` один раз, затем работайте, пока `ship.is_running()` возвращает `True`.
`stdin` и `stdout` заняты протоколом: сообщения выводите только в `sys.stderr`.

```python
import sys
import time
import api as ship

ship.start()
prop = ship.Prop("prop0")  # имя установленного двигательного модуля

try:
    prop.throttle(0.3)
    prop.gyro(-0.5)
    while ship.is_running():
        print(ship.getPos(), file=sys.stderr)
        time.sleep(0.05)
except (TimeoutError, RuntimeError) as error:
    print(error, file=sys.stderr)
finally:
    if ship.is_running():
        prop.throttle(0)
        prop.gyro(0)
```

### Чтение состояния

Все запросы принимают `timeout=2.0` в секундах и возвращают поле `result` ответа.
При тайм-ауте возникает `TimeoutError`, при ошибке RPC или закрытии соединения — `RuntimeError`.

| Вызов | Пример результата |
| --- | --- |
| `ship.getId()` | `{"craft_id": 0}` |
| `ship.getPos()` | `{"x": 50.0, "y": 50.0}` |
| `ship.getVel()` | `{"x": 1.0, "y": 0.0}` |
| `ship.getAngle()` | `{"angle": 1.57}` |
| `ship.getAngularVelocity()` | `{"angular_velocity": 0.5}` |
| `ship.getModules()` | `[["modules", ["prop0"]]]` |
| `prop.getMaxThrottle()` | `{"max_throttle": 10.0}` |
| `prop.getMaxGyro()` | `{"max_gyro": 1.0}` |

Имена модулей: `dict(ship.getModules())["modules"]`. `ship.Prop(name)` создаёт
обёртку для указанного имени, наличие модуля не проверяет. Запрос к отсутствующему
модулю завершится по тайм-ауту.

### Управление и координаты

- `prop.throttle(value=1.0)` — доля тяги от `0` до `1`; `0` выключает двигатель.
- `prop.gyro(value=1.0)` — доля угловой скорости от `-1` до `1`; `0` останавливает вращение.
- Команды не ждут ответа. Значения сохраняются до следующей команды; перед выходом обнулите их.
  Значения вне диапазонов игнорируются.
- X направлена вправо, Y — вверх. Скорость измеряется в единицах мира в секунду.
- Углы — в радианах: `0` вправо, `π/2` вверх; положительное вращение — против часовой стрелки.
- Ускорение равно `max_throttle * throttle` вдоль `(cos(angle), sin(angle))`,
  угловая скорость — `max_gyro * gyro` рад/с. Гироскоп задаёт скорость, а не угол.
- Трения и обратной тяги нет: для торможения разверните тягу против скорости.

### Протокол и события

Обмен идёт JSON-объектами, по одному на строку (`\n`), через стандартные потоки Python.
Запросы и команды содержат `module`: `"none"` для корабля или имя модуля, например `"prop0"`.
Методы корабля: `get_id`, `get_position`, `get_velocity`, `get_angle`,
`get_angular_velocity`, `get_modules`; модуля `Prop`: `get_max_throttle`,
`get_max_gyro`, `set_throttle`, `set_gyro`.

```json
{"type":"request","id":1,"module":"none","method":"get_position"}
{"type":"response","id":1,"result":{"x":0.0,"y":0.0}}
{"type":"command","module":"prop0","method":"set_throttle","value":0.3}
{"type":"event","name":"enemy_detected","data":{"enemy_id":42}}
{"type":"shutdown"}
```

Ответ сопоставляется запросу по `id`; вместо `result` может прийти строковое поле `error`.
`shutdown` или EOF снимают флаг `is_running()` и завершают ожидающие запросы с ошибкой.
Обработчики событий выполняются в отдельном потоке; регистрируйте их до `start()`:

```python
@ship.on("enemy_detected")
def enemy_detected(data):
    print(f"Обнаружен корабль {data['enemy_id']}", file=sys.stderr)
```

Для каждого имени действует один обработчик. `enemy_detected` приведено как пример:
сейчас игра не отправляет это событие автоматически.
