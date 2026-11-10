"""End-to-end HTTP/WS and HTTPS/WSS smoke test. Python standard library only."""
import base64
import hashlib
import http.client
import os
from pathlib import Path
import socket
import sqlite3
import ssl
import struct
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def exact(sock, count):
    result = b""
    while len(result) < count:
        chunk = sock.recv(count - len(result))
        if not chunk:
            raise AssertionError("Unexpected EOF")
        result += chunk
    return result


class WebSocket:
    def __init__(self, port, context=None):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=5)
        if context:
            self.sock = context.wrap_socket(self.sock, server_hostname="localhost")
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall((f"GET /ws HTTP/1.1\r\nHost: localhost:{port}\r\n"
                           "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                           f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n").encode())
        header = b""
        while not header.endswith(b"\r\n\r\n"):
            header += exact(self.sock, 1)
        assert header.startswith(b"HTTP/1.1 101"), header
        accept = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest())
        assert accept in header

    def send(self, payload, opcode=2, final=True):
        size = len(payload)
        length = bytes([128 | size]) if size < 126 else b"\xfe" + struct.pack("!H", size)
        mask = os.urandom(4)
        self.sock.sendall(bytes([(128 if final else 0) | opcode]) + length + mask +
                          bytes(value ^ mask[i % 4] for i, value in enumerate(payload)))

    def receive(self):
        opcode, size = exact(self.sock, 2)
        assert not size & 128, "Server frames must not be masked"
        size &= 127
        if size == 126:
            size = struct.unpack("!H", exact(self.sock, 2))[0]
        if size == 127:
            size = struct.unpack("!Q", exact(self.sock, 8))[0]
        return opcode & 15, exact(self.sock, size)

    def request(self, payload):
        self.send(payload)
        opcode, reply = self.receive()
        assert opcode == 2, (opcode, reply)
        return reply

    def close(self):
        self.send(struct.pack("!H", 1000), opcode=8)
        assert self.receive()[0] == 8
        self.sock.close()


def credentials(kind, username="пилот", password="password123"):
    result = bytes([kind])
    for value in (username, password):
        value = value.encode()
        result += struct.pack("<H", len(value)) + value
    return result


def start_server(directory, secure):
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    env = {**os.environ, "ASTEROID_ADDRESS": "127.0.0.1", "ASTEROID_PORT": str(port),
           "ASTEROID_DB": str(directory / "accounts.sqlite3"),
           "ASTEROID_TLS_CERT": "", "ASTEROID_TLS_KEY": ""}
    if secure:
        env.update(ASTEROID_TLS_CERT=str(directory / "cert.pem"), ASTEROID_TLS_KEY=str(directory / "key.pem"))
    process = subprocess.Popen([str(ROOT / "build/asteroid")], cwd=ROOT, env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    for _ in range(100):
        if process.poll() is not None:
            raise AssertionError(process.communicate())
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=.1):
                return process, port
        except OSError:
            time.sleep(.05)
    process.terminate()
    process.wait(timeout=5)
    raise AssertionError("Server startup timed out")


def check(directory, secure):
    context = ssl.create_default_context(cafile=str(directory / "cert.pem")) if secure else None
    process, port = start_server(directory, secure)
    try:
        for path in ("/", "/style.css", "/app.js", "/protocol.js", "/missing", "/../Makefile"):
            connection = (http.client.HTTPSConnection("localhost", port, context=context, timeout=5)
                          if secure else http.client.HTTPConnection("localhost", port, timeout=5))
            connection.request("GET", path)
            response = connection.getresponse()
            assert response.status == (404 if path in ("/missing", "/../Makefile") else 200), path
            assert response.read()
            connection.close()

        ws = WebSocket(port, context)
        # The HTTPS run reuses the database created by the HTTP run.
        result = ws.request(credentials(1 if secure else 2))
        assert result[:2] == bytes([128, 0]), result
        user_id = struct.unpack_from("<I", result, 2)[0]
        assert user_id > 0
        assert "пилот".encode() in result
        assert ws.request(credentials(1))[0] == 255  # Already signed in.
        assert ws.request(b"\x03") == b"\x81"
        assert ws.request(credentials(2))[:2] == bytes([128, 2])  # Duplicate name.
        assert ws.request(credentials(1, password="wrongpass"))[:2] == bytes([128, 3])
        assert ws.request(credentials(2, username="bad name"))[:2] == bytes([128, 1])
        assert ws.request(credentials(1))[:6] == bytes([128, 0]) + struct.pack("<I", user_id)
        ws.close()

        ws = WebSocket(port, context)
        assert ws.request(b"\x01\x20")[0] == 255  # Truncated packet.
        assert ws.request(b"\xff")[0] == 255
        ws.send(b"ping", opcode=9)
        assert ws.receive() == (10, b"ping")
        packet = credentials(1)
        ws.send(packet[:5], final=False)
        ws.send(packet[5:], opcode=0)
        assert ws.receive()[1][:2] == bytes([128, 0])
        ws.close()
        ws = WebSocket(port, context)
        ws.send(b"text", opcode=1)
        assert ws.receive()[0] == 8
        ws.sock.close()
        with sqlite3.connect(directory / "accounts.sqlite3") as db:
            salt, password_hash = db.execute("SELECT salt, password_hash FROM accounts").fetchone()
            assert len(salt) == 16 and len(password_hash) == 32
            assert password_hash != b"password123"
        print(("HTTPS/WSS" if secure else "HTTP/WS") + ": OK")
    finally:
        process.terminate()
        process.communicate(timeout=5)


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="asteroid-test-") as temp:
        directory = Path(temp)
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                        "-keyout", str(directory / "key.pem"), "-out", str(directory / "cert.pem"),
                        "-days", "1", "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost"],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        check(directory, secure=False)
        check(directory, secure=True)
