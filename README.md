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

При запуске создаются 10 кораблей с модулями Prop, Storage и RCS.
Выберите корабль в списке слева или нажатием на него, введите путь
`scripts/script.py` в поле скрипта и нажмите Enter. Скрипт запускается для выбранного
корабля; API находится в [`scripts/api.py`](scripts/api.py).
Вызовите `ship.start()` один раз, затем работайте, пока `ship.is_running()` возвращает `True`.
`stdin` занят протоколом. При импорте `api` обычный `print()` перенаправляется
в игровую консоль; `sys.stderr`, logging и traceback также видны в ней.
Служебные JSON-сообщения API идут по отдельному сохранённому потоку `stdout`.

В нижней панели нажмите на консоль, введите команду и нажмите Enter.
Она отправится скрипту выбранного корабля событием `console_message`, где
`data` — введённая строка. Если корабль один, выбирать его не обязательно.
Стрелки ↑/↓ перебирают историю, колесо мыши прокручивает вывод;
поддерживаются перемещение курсора, Home/End, Backspace/Delete и Ctrl/Cmd+V.
Пока консоль активна, WASD не двигают камеру; Escape снимает фокус ввода.

Зарегистрируйте обработчик до `ship.start()`:

```python
@ship.on("console_message")
def console_message(message):
    print(f"Получена команда: {message}")
```

```python
import sys
import time
import api as ship

ship.start()
print(ship.getModules())  # {"modules": ["prop_0", "stor_1", "rcs_2"]}
prop = ship.Prop("prop_0")
rcs = ship.RCS("rcs_2")

try:
    prop.throttle(0.3)
    rcs.gyro(-0.5)
    rcs.setThrust((0, 0.5))  # боковая тяга в локальных координатах корабля
    while ship.is_running():
        print(ship.getPos(), file=sys.stderr)
        time.sleep(0.05)
except (TimeoutError, RuntimeError) as error:
    print(error, file=sys.stderr)
finally:
    if ship.is_running():
        prop.throttle(0)
        rcs.gyro(0)
        rcs.setThrust((0, 0))
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
| `ship.getModules()` | `{"modules": ["prop_0", "stor_1", "rcs_2"]}` |
| `prop.getMaxThrottle()` | `{"max_throttle": 10.0}` |
| `rcs.getMaxGyro()` | `{"max_gyro": 1.0}` |
| `rcs.getThrust()` | `{"max_thrust": 1.0}` |

Имена модулей: `ship.getModules()["modules"]`. Счётчик имён общий для всех модулей
одного корабля: при текущем порядке добавления получаются `prop_0`, `stor_1`,
`rcs_2`. При другом составе или порядке добавления номера меняются.
`ship.Prop(name)` и `ship.RCS(name)` создают обёртки для указанного имени,
наличие модуля не проверяют. Запрос к отсутствующему модулю завершится по тайм-ауту,
а команда для него будет пропущена.

### Модули

| Модуль C++ | Тип | Имя в начальном мире | Назначение |
| --- | --- | --- | --- |
| `PropulsionModule` | `PROPULSION` | `prop_0` | Основная тяга вперёд, Python-обёртка `ship.Prop` |
| `StorageModule` | `STORAGE` | `stor_1` | Хранение ресурсов, управление через C++ |
| `RCSModule` | `RCS` | `rcs_2` | Вращение и тяга в любом локальном направлении, Python-обёртка `ship.RCS` |

#### Prop — основной двигатель

- `prop.getMaxThrottle(timeout=2.0)` возвращает максимальное ускорение:
  `{"max_throttle": 10.0}`.
- `prop.throttle(value=1.0)` задаёт долю тяги от `0` до `1`;
  `0` выключает двигатель. Значения вне диапазона игнорируются.
- Ускорение направлено вперёд по кораблю и равно `max_throttle * throttle`.

#### RCS — вращение и маневровая тяга

- `rcs.getMaxGyro(timeout=2.0)` возвращает `{"max_gyro": 1.0}` — максимальную
  угловую скорость в рад/с.
- `rcs.gyro(value=1.0)` задаёт долю угловой скорости от `-1` до `1`;
  `0` останавливает вращение. Значения вне диапазона игнорируются.
- `rcs.getThrust(timeout=2.0)` возвращает **максимальное ускорение**
  `{"max_thrust": 1.0}`.
- `rcs.setThrust(value=(0, 0))` принимает пару чисел `(x, y)` или список `[x, y]`.
  Это вектор доли тяги в локальных координатах корабля: X — вперёд,
  Y — влево относительно направления корабля. Например, `(-1, 0)` задаёт
  тягу назад, `(0, 1)` — боковую, `(0, 0)` выключает маневровую тягу.
- Вектор длиной больше `1` нормализуется; более короткий сохраняет свою длину.
  Тяга поворачивается на текущий угол корабля и умножается на `max_thrust`.
- `setThrust` требует ровно два конечных числа. Неправильный тип, размер массива
  или неконечное значение приводит к исключению при обработке пакета;
  игра выводит ошибку `Invalid packet` в консоль.

Вращение управляется через `RCS`; Python-класс `Prop` предоставляет управление
основной тягой через `throttle`.

#### Storage — склад ресурсов

При добавлении через `Craft::AddModule(STORAGE)` склад получает вместимость
20 единиц, общую для всех ресурсов. Доступны типы `ROCK` и `STONE`
из [`backend/include/resources.hpp`](backend/include/resources.hpp).
Конструктор `StorageModule(Craft* parent, std::size_t capacity)` позволяет
задать другую вместимость при создании модуля напрямую.

| Метод C++ | Поведение |
| --- | --- |
| `GetInventory()` | Возвращает ссылку на `std::unordered_map<RESOURCE_TYPE, std::size_t>` с содержимым склада |
| `ResourceCount(resource)` | Возвращает количество ресурса или `0`, если его нет |
| `AddToInventory({{ROCK, 5}, {STONE, 2}})` | Добавляет ресурсы, проверяя общую вместимость |
| `TakeFromInventory({{ROCK, 3}})` | Извлекает ресурсы, проверяя наличие всех запрошенных количеств |

При нехватке места или ресурсов выбрасывается `std::runtime_error`, содержимое
склада сохраняется. После полного изъятия ресурса его запись удаляется;
нулевые количества не создают новых записей. Проверки выполняются в
`AddToInventory` и `TakeFromInventory`; изменение карты через `GetInventory()`
обходит их. Python-обёртка и RPC-методы склада пока не реализованы.

### Управление и координаты

- Команды не ждут ответа. Значения сохраняются до следующей команды; перед выходом обнулите их.
- X направлена вправо, Y — вверх. Скорость измеряется в единицах мира в секунду.
- Углы — в радианах: `0` вправо, `π/2` вверх; положительное вращение — против часовой стрелки.
- Ускорения Prop и RCS складываются. В мировых координатах они равны
  `prop_max_throttle * throttle * (cos(angle), sin(angle))` и
  `rcs_max_thrust * rotate(local_thrust, angle)` соответственно.
- Угловая скорость равна `max_gyro * gyro` рад/с. Гироскоп задаёт скорость, а не угол.
- Трения нет. Для торможения направьте тягу против скорости; RCS поддерживает
  тягу назад и в стороны. Выключение тяги сохраняет набранную скорость.

### Протокол и события

Обмен идёт JSON-объектами, по одному на строку (`\n`), через стандартные потоки Python.
Запросы и команды содержат `module`: `"none"` для корабля или имя модуля, например `"prop_0"`.

| Получатель | Методы запросов (`request`) | Методы команд (`command`) |
| --- | --- | --- |
| Корабль (`"none"`) | `get_id`, `get_position`, `get_velocity`, `get_angle`, `get_angular_velocity`, `get_modules` | — |
| Prop (`"prop_0"`) | `get_max_throttle` | `set_throttle` |
| RCS (`"rcs_2"`) | `get_max_gyro`, `get_max_thrust` | `set_gyro`, `set_thrust` |
| Storage (`"stor_1"`) | Пока не реализованы | Пока не реализованы |

```json
{"type":"request","id":1,"module":"none","method":"get_position"}
{"type":"response","id":1,"result":{"x":0.0,"y":0.0}}
{"type":"command","module":"prop_0","method":"set_throttle","value":0.3}
{"type":"request","id":2,"module":"rcs_2","method":"get_max_thrust"}
{"type":"response","id":2,"result":{"max_thrust":1.0}}
{"type":"command","module":"rcs_2","method":"set_gyro","value":-0.5}
{"type":"command","module":"rcs_2","method":"set_thrust","value":[0.0,0.5]}
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
