
import websocket
import json
import threading

SERVER_URL = "ws://localhost:8080"

def SendJSONMessage(message):
    data = {
        "type": "message",
        "content": message
    }
    return json.dumps(data)


def ParseJSON(message):
    try:
        return json.loads(message)
    except (json.JSONDecodeError, TypeError) as e:
        print(f"JSON parsing error: {e}")
        return None


def PrintJSON(data, indent=0):
    if isinstance(data, dict):
        for key, value in data.items():
            print(" " * indent + f"{key}:")
            PrintJSON(value, indent + 4)

    elif isinstance(data, list):
        for i, value in enumerate(data):
            print(" " * indent + f"[{i}]:")
            PrintJSON(value, indent + 4)

    else:
        print(" " * indent + str(data))

def on_open(ws):
    print("Подключено к серверу!")


def on_message(ws, message):
    print(f"\nПолучено: ")
    PrintJSON(ParseJSON(message))
    print("> ", end="", flush=True)


def on_error(ws, error):
    print(f"\nОшибка: {error}")


def on_close(ws, status_code, message):
    print(f"\nСоединение закрыто: {status_code}, {message}")


def send_messages(ws):
    while True:
        try:
            message = input("> ")

            if message.lower() == "exit":
                ws.close()
                break

            if message.lower() == "map":
                request = {
                    "type": "request",
                    "request_type": "get_map"
                }
                ws.send(json.dumps(request))
                continue
            if message.lower().startswith("entity"):
                parts = message.split()
                if len(parts) == 2 and parts[1].isdigit():
                    entity_id = int(parts[1])
                    request = {
                        "type": "request",
                        "request_type": "get_entity",
                        "entity_id": entity_id
                    }
                    ws.send(json.dumps(request))
                else:
                    print("Использование: entity <id>")
                continue

            if ws.sock and ws.sock.connected:
                ws.send(SendJSONMessage(message))
            else:
                print("Нет соединения с сервером")
                break

        except (EOFError, KeyboardInterrupt):
            ws.close()
            break
        except websocket.WebSocketConnectionClosedException:
            print("Соединение потеряно")
            break

def SendJSONMessage(message):
    data = {
        "type": "message",
        "content": message
    }
    return json.dumps(data)

ws = websocket.WebSocketApp(
    SERVER_URL,
    on_open=on_open,
    on_message=on_message,
    on_error=on_error,
    on_close=on_close,
)

# Приём сообщений в отдельном потоке
thread = threading.Thread(
    target=ws.run_forever,
    daemon=True
)
thread.start()

# Отправка сообщений из основного потока
send_messages(ws)
