"""Unified HTTP/WebSocket API. Importing this module never starts a connection."""

import argparse
import getpass
import http.cookiejar
import json
import os
import threading
import urllib.error
import urllib.parse
import urllib.request

import websocket


class APIError(Exception):
    def __init__(self, error):
        super().__init__(error.get("message", "Ошибка сервера"))
        self.code = error.get("code")
        self.status = error.get("status")


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None  # Never forward credentials to another endpoint.


class AsteroidAPI:
    def __init__(self, server="https://46.173.218.137", api_key=None, timeout=15):
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
        self.api_key = api_key
        self.timeout = timeout
        self.cookies = http.cookiejar.CookieJar()
        self.opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(self.cookies), _NoRedirect())
        self.socket = None
        self._lock = threading.Lock()

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
        with response:
            try:
                body = json.load(response)
            except (ValueError, UnicodeDecodeError) as error:
                raise APIError({"status": response.code, "code": "INVALID_RESPONSE", "message": "Сервер вернул ответ без JSON"}) from error
            if not body.get("ok"):
                raise APIError(body.get("error", {"status": response.code}))
            return body["result"]

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
        self.disconnect()
        headers = [f"Authorization: Bearer {self.api_key}"] if self.api_key else []
        request = urllib.request.Request(self.base + "/ws")
        self.cookies.add_cookie_header(request)
        self.socket = websocket.create_connection(self.ws_url, header=headers,
            cookie=request.get_header("Cookie"), timeout=self.timeout, suppress_origin=True, redirect_limit=0)

    def disconnect(self):
        with self._lock:
            if self.socket is not None:
                self.socket.close()
                self.socket = None

    def request(self, requestType, **fields):
        if not self.connected:
            return self.httpRequest(requestType, **fields)
        with self._lock:
            self.socket.send(json.dumps({**fields, "type": "request", "request_type": requestType}))
            while True:
                message = self.socket.recv()
                if isinstance(message, str) and message.startswith("Your ID:"):
                    continue
                if not message:
                    raise APIError({"code": "DISCONNECTED", "message": "Соединение закрыто"})
                body = json.loads(message)
                if not body.get("ok"):
                    raise APIError(body.get("error", {}))
                return body["result"]

    def getMap(self):
        return self.request("get_map")

    def getEntity(self, entity_id):
        if not isinstance(entity_id, int) or isinstance(entity_id, bool) or entity_id < 0:
            raise ValueError("Некорректный ID объекта")
        return self.request("get_entity", entity_id=entity_id)


def main():
    parser = argparse.ArgumentParser(description="Клиент Project Asteroid")
    parser.add_argument("--server", default="https://46.173.218.137")
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
        print("Команды: map, entity <id>, exit")
        while True:
            command = input("> ").split()
            if not command:
                continue
            if command == ["exit"]:
                break
            if command == ["map"]:
                result = api.getMap()
            elif len(command) == 2 and command[0] == "entity" and command[1].isdigit():
                result = api.getEntity(int(command[1]))
            else:
                print("Команды: map, entity <id>, exit")
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
