"""Unified HTTP/WebSocket API. Importing this module never starts a connection."""

import argparse
import getpass
import http.cookiejar
import json
import os
import ssl
import threading
import urllib.error
import urllib.parse
import urllib.request

import certifi
import websocket


class APIError(Exception):
    def __init__(self, error):
        super().__init__(error.get("message", "Ошибка сервера"))
        self.code = error.get("code")
        self.status = error.get("status")


def _connection_error(error, message):
    reason = error.reason if isinstance(error, urllib.error.URLError) else error
    if isinstance(reason, ssl.SSLCertVerificationError):
        return APIError({"code": "TLS_VERIFY_FAILED", "message":
            f"Не удалось проверить TLS-сертификат сервера: {reason.verify_message}. "
            "Обновите CA-сертификаты: python3 -m pip install --upgrade certifi"})
    return APIError({"code": "CONNECTION_ERROR", "message": message})


def _result(body, status=None):
    if not isinstance(body, dict) or not isinstance(body.get("ok"), bool):
        raise APIError({"status": status, "code": "INVALID_RESPONSE", "message": "Некорректный ответ API"})
    if not body["ok"]:
        error = body.get("error")
        if not isinstance(error, dict):
            error = {"code": "INVALID_RESPONSE", "message": "Некорректный ответ API"}
        raise APIError({"status": status, **error})
    if "result" not in body:
        raise APIError({"status": status, "code": "INVALID_RESPONSE", "message": "В ответе API отсутствует result"})
    return body["result"]


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None  # Never forward credentials to another endpoint.


class AsteroidAPI:
    """Synchronous client: connect(), getMap(), getEntity(id), disconnect().

    Defaults come from ASTEROID_SERVER and ASTEROID_API_KEY. Construction and
    import do not connect to the server. Use login() before connect() when
    authenticating with a password instead of an API key.
    """

    def __init__(self, server=None, api_key=None, timeout=15):
        server = server if server is not None else os.environ.get("ASTEROID_SERVER", "https://46.173.218.137")
        url = urllib.parse.urlsplit(server)
        scheme = {"wss": "https", "ws": "http"}.get(url.scheme, url.scheme)
        if scheme not in ("https", "http") or not url.hostname or url.username or url.password or url.query or url.fragment:
            raise ValueError("Ожидается HTTPS-адрес игрового сервера")
        if scheme == "http" and url.hostname not in ("localhost", "127.0.0.1", "::1"):
            raise ValueError("Для удалённого сервера требуется HTTPS/WSS")
        if url.path not in ("", "/", "/ws"):
            raise ValueError("Укажите корневой адрес сервера")
        self.base = f"{scheme}://{url.netloc}"
        self.ws_url = f"{'wss' if scheme == 'https' else 'ws'}://{url.netloc}/ws"
        self.api_key = api_key if api_key is not None else os.environ.get("ASTEROID_API_KEY")
        self.timeout = timeout
        # Supplement system trust with Mozilla roots, including on python.org macOS
        # installs where the default OpenSSL CA bundle has not been installed.
        self._ssl_context = ssl.create_default_context()
        self._ssl_context.load_verify_locations(cafile=certifi.where())
        self.cookies = http.cookiejar.CookieJar()
        self.opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(self.cookies),
            urllib.request.HTTPSHandler(context=self._ssl_context), _NoRedirect())
        self.socket = None
        self._lock = threading.RLock()
        self.connect()

    def __enter__(self):
        return self.connect()

    def __exit__(self, exc_type, exc_value, traceback):
        self.disconnect()

    @property
    def connected(self):
        return self.socket is not None and self.socket.connected

    def httpRequest(self, requestType, **fields):
        payload = {**fields, "type": "request", "request_type": requestType}
        headers = {"Content-Type": "application/json", "X-Asteroid-Client": "1"}
        if self.api_key:
            headers["Authorization"] = f"Bearer {self.api_key}"
        request = urllib.request.Request(self.base + "/api", data=json.dumps(payload).encode(), headers=headers, method="POST")
        try:
            response = self.opener.open(request, timeout=self.timeout)
        except urllib.error.HTTPError as error:
            response = error
        except (OSError, urllib.error.URLError) as error:
            raise _connection_error(error, "Не удалось связаться с сервером. Проверьте адрес и запуск сервера.") from error
        with response:
            try:
                body = json.load(response)
            except (ValueError, UnicodeDecodeError) as error:
                raise APIError({"status": response.code, "code": "INVALID_RESPONSE", "message": "Сервер вернул ответ без JSON"}) from error
            except OSError as error:
                raise APIError({"code": "CONNECTION_ERROR", "message": "Соединение прервалось при получении ответа API."}) from error
            return _result(body, response.code)

    def register(self, username, password):
        return self._authenticate("register", username, password)

    def login(self, username, password):
        return self._authenticate("login", username, password)

    def _authenticate(self, command, username, password):
        result = self.httpRequest(command, username=username, password=password)
        self.disconnect()
        self.api_key = None
        return result

    def me(self):
        return self.httpRequest("me")

    def logout(self):
        result = self.httpRequest("logout")
        self.disconnect()
        return result

    def createApiKey(self, name, password, expiresDays=30):
        return self.httpRequest("create_api_key", name=name, password=password, expires_days=expiresDays)

    def listApiKeys(self):
        return self.httpRequest("list_api_keys")

    def revokeApiKey(self, keyId):
        return self.httpRequest("revoke_api_key", key_id=keyId)

    def connect(self):
        """Open an authenticated connection and return this client.

        Set ASTEROID_API_KEY, pass api_key=..., or call login() first.
        """
        with self._lock:
            request = urllib.request.Request(self.base + "/ws")
            self.cookies.add_cookie_header(request)
            cookie = request.get_header("Cookie")
            if not self.api_key and not cookie:
                raise APIError({"status": 401, "code": "AUTH_REQUIRED", "message":
                    "Нет API-ключа или сессии. Задайте ASTEROID_API_KEY, передайте api_key=... или вызовите login(логин, пароль) перед connect()."})
            self.disconnect()
            headers = [f"Authorization: Bearer {self.api_key}"] if self.api_key else []
            try:
                self.socket = websocket.create_connection(self.ws_url, header=headers,
                    cookie=cookie, timeout=self.timeout, suppress_origin=True, redirect_limit=0,
                    sslopt={"context": self._ssl_context})
            except websocket.WebSocketBadStatusException as error:
                try:
                    body = json.loads(error.resp_body)
                except (ValueError, TypeError, UnicodeDecodeError):
                    if error.status_code == 401:
                        raise APIError({"status": 401, "code": "AUTH_REQUIRED", "message": "API-ключ или сессия недействительны. Войдите снова или используйте действующий ключ."}) from error
                    raise APIError({"status": error.status_code, "code": "CONNECTION_ERROR", "message": "Сервер отклонил WebSocket-подключение."}) from error
                _result(body, error.status_code)
                raise APIError({"status": error.status_code, "code": "CONNECTION_ERROR", "message": "Не удалось открыть WebSocket."}) from error
            except (OSError, websocket.WebSocketException) as error:
                raise _connection_error(error, "Не удалось открыть WebSocket. Проверьте адрес и запуск сервера.") from error
            return self

    def disconnect(self):
        with self._lock:
            socket, self.socket = self.socket, None
            if socket is not None:
                socket.close()

    close = disconnect

    def request(self, requestType, **fields):
        with self._lock:
            if not self.connected:
                return self.httpRequest(requestType, **fields)
            try:
                self.socket.send(json.dumps({**fields, "type": "request", "request_type": requestType}))
                while True:
                    message = self.socket.recv()
                    if isinstance(message, str) and message.startswith("Your ID:"):
                        continue
                    if not message:
                        self.disconnect()
                        raise APIError({"code": "DISCONNECTED", "message": "Соединение закрыто. Подключитесь снова через connect()."})
                    return _result(json.loads(message))
            except (ValueError, UnicodeDecodeError) as error:
                self.disconnect()
                raise APIError({"code": "INVALID_RESPONSE", "message": "Сервер вернул ответ без JSON"}) from error
            except (OSError, websocket.WebSocketException) as error:
                self.disconnect()
                # A timed-out reply cannot be matched safely to the next request.
                raise APIError({"code": "CONNECTION_ERROR", "message": "Соединение потеряно или истекло время ожидания. Вызовите connect() повторно."}) from error

    def getMap(self):
        """Return the server's map dict, with its list in result['entities']."""
        return self.request("get_map")

    def getMyEntities(self):
        """Return {'entities': [...]} for the authenticated account only."""
        return self.request("get_my_entities")

    def getEntity(self, entity_id):
        return self.request("get_entity", entity_id=entity_id)

    def createCraft(self):
        return self.request("create_craft")

    def flyTo(self, entity_id, x, y):
        """Send your ship to (x, y); return its state, without waiting for arrival."""
        return self.request("fly_to", entity_id=entity_id, x=x, y=y)

    def mine(self, craft_id, asteroid_id):
        """Approach an asteroid and mine into your ship's storage; return immediately."""
        return self.request("mine", craft_id=craft_id, asteroid_id=asteroid_id)



def main():
    parser = argparse.ArgumentParser(description="Клиент Project Asteroid")
    parser.add_argument("--server", default=None, help="Адрес сервера; по умолчанию ASTEROID_SERVER или публичный сервер")
    parser.add_argument("--username")
    parser.add_argument("--register", action="store_true")
    args = parser.parse_args()
    api = AsteroidAPI(args.server, api_key=os.environ.get("ASTEROID_API_KEY"))
    try:
        if not api.api_key:
            username = args.username or input("Логин: ")
            password = getpass.getpass("Пароль: ")
            (api.register if args.register else api.login)(username, password)
        print(json.dumps(api.me(), ensure_ascii=False, indent=2))
        api.connect()
        print("Команды: map, mine, mine <craft_id> <asteroid_id>, entity <id>, fly <id> <x> <y>, exit")
        while True:
            command = input("> ").split()
            if not command:
                continue
            if command == ["exit"]:
                break
            if command == ["map"]:
                result = api.getMap()
            elif command == ["mine"]:
                result = api.getMyEntities()
            elif len(command) == 3 and command[0] == "mine":
                try:
                    craft_id, asteroid_id = int(command[1]), int(command[2])
                except ValueError:
                    print("Использование: mine <craft_id> <asteroid_id>")
                    continue
                result = api.mine(craft_id, asteroid_id)
            elif len(command) == 2 and command[0] == "entity" and command[1].isdigit():
                result = api.getEntity(int(command[1]))
            elif len(command) == 4 and command[0] == "fly":
                try:
                    entity_id, x, y = int(command[1]), float(command[2]), float(command[3])
                except ValueError:
                    print("Использование: fly <id> <x> <y>")
                    continue
                result = api.flyTo(entity_id, x, y)
            else:
                print("Команды: map, mine, mine <craft_id> <asteroid_id>, entity <id>, fly <id> <x> <y>, exit")
                continue
            print(json.dumps(result, ensure_ascii=False, indent=2))
    except (APIError, OSError, websocket.WebSocketException) as error:
        print(str(error))
    except (EOFError, KeyboardInterrupt):
        pass
    finally:
        api.disconnect()


if __name__ == "__main__":
    main()
