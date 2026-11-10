"""End-to-end WS/WSS checks; Docker is mocked, uploaded code is never run here."""
import base64
import contextlib
import os
from pathlib import Path
import socket
import ssl
import struct
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def exact(sock, size):
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise EOFError('WebSocket closed')
        data.extend(chunk)
    return bytes(data)


def string(value):
    value = value.encode()
    return struct.pack('<H', len(value)) + value


def source_packet(ship, source):
    return b'\x04' + struct.pack('<II', ship, len(source)) + source


class Client:
    def __init__(self, port, secure=False):
        self.sock = socket.create_connection(('127.0.0.1', port), timeout=5)
        if secure:
            self.sock = ssl._create_unverified_context().wrap_socket(self.sock, server_hostname='localhost')
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall(f'GET /ws HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: {key}\r\n\r\n'.encode())
        response = b''
        while not response.endswith(b'\r\n\r\n'):
            response += exact(self.sock, 1)
        assert response.startswith(b'HTTP/1.1 101'), response

    def close(self):
        self.sock.close()

    def frame(self, data, opcode=2, final=True):
        header = bytes([(128 if final else 0) | opcode])
        size = len(data)
        if size < 126:
            header += bytes([128 | size])
        elif size < 65536:
            header += b'\xfe' + struct.pack('!H', size)
        else:
            header += b'\xff' + struct.pack('!Q', size)
        mask = os.urandom(4)
        self.sock.sendall(header + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))

    def receive(self):
        data = bytearray()
        while True:
            a, b = exact(self.sock, 2)
            length = b & 127
            if length == 126:
                length, = struct.unpack('!H', exact(self.sock, 2))
            elif length == 127:
                length, = struct.unpack('!Q', exact(self.sock, 8))
            body = exact(self.sock, length)
            if (a & 15) == 9:
                self.frame(body, opcode=10)
                continue
            assert (a & 15) in (0, 2), (a, body)
            data.extend(body)
            if a & 128:
                return bytes(data)

    def wait(self, kind, predicate=lambda packet: True):
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            packet = self.receive()
            if packet[0] == kind and predicate(packet):
                return packet
        raise AssertionError(f'Timed out waiting for {kind}')

    def request(self, packet, kind):
        self.frame(packet)
        return self.wait(kind)


def ships(packet):
    count, = struct.unpack_from('<I', packet, 1)
    offset, result = 5, []
    for _ in range(count):
        ship, owner, length = struct.unpack_from('<IIH', packet, offset)
        offset += 10
        name = packet[offset:offset + length].decode()
        offset += length
        x, y, vx, vy, modules, state = struct.unpack_from('<ffffIB', packet, offset)
        offset += 21
        result.append(dict(id=ship, owner=owner, name=name, state=state))
    assert offset == len(packet)
    return result


@contextlib.contextmanager
def server(directory, secure=False):
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        port = probe.getsockname()[1]
    env = dict(os.environ, ASTEROID_PORT=str(port), ASTEROID_DB=str(directory / 'accounts.db'),
               ASTEROID_TLS_CERT='', ASTEROID_TLS_KEY='', ASTEROID_DOCKER=str(ROOT / 'tests/fake_docker.py'))
    if secure:
        cert, key = directory / 'cert.pem', directory / 'key.pem'
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
                        '-subj', '/CN=localhost', '-keyout', str(key), '-out', str(cert)],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        env.update(ASTEROID_TLS_CERT=str(cert), ASTEROID_TLS_KEY=str(key))
    process = subprocess.Popen([str(ROOT / 'build/asteroid')], cwd=ROOT, env=env,
                               stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        for _ in range(200):
            if process.poll() is not None:
                raise RuntimeError(process.stderr.read().decode())
            try:
                probe = socket.create_connection(('127.0.0.1', port), timeout=.1)
                probe.close()
                break
            except OSError:
                time.sleep(.02)
        else:
            raise RuntimeError('Server startup timed out')
        yield port
    finally:
        process.terminate()
        process.wait(timeout=5)
        process.stderr.close()


def main():
    with tempfile.TemporaryDirectory(prefix='asteroid-smoke-') as tmp:
        directory = Path(tmp)
        with server(directory) as port, contextlib.ExitStack() as stack:
            clients = [Client(port) for _ in range(3)]
            for client in clients:
                stack.callback(client.close)
            a, b, guest = clients
            for client, name in [(a, 'alpha'), (b, 'beta')]:
                reply = client.request(b'\x02' + string(name) + string('password123'), 128)
                assert reply[1] == 0
            # A guest receives unsolicited snapshots with all registered ships.
            world = ships(guest.wait(130, lambda p: struct.unpack_from('<I', p, 1)[0] == 2))
            own = next(ship['id'] for ship in world if ship['name'] == 'alpha')
            assert {s['name'] for s in world} == {'alpha', 'beta'}
            guest.request(source_packet(own, b'echo'), 255)
            b.request(source_packet(own, b'echo'), 255)
            b.request(b'\x05' + struct.pack('<I', own), 255)
            b.request(b'\x06' + struct.pack('<I', own), 255)
            a.request(source_packet(own, b''), 255)
            a.request(b'\x04' + struct.pack('<II', own, 1024 * 1024 + 1), 255)
            a.request(b'\x04' + struct.pack('<II', own, 10) + b'abc', 255)
            # Full-sized source crosses the WebSocket 64-bit length boundary and is fragmented.
            upload = source_packet(own, b'x' * (1024 * 1024))
            a.frame(upload[:600000], final=False)
            a.frame(upload[600000:], opcode=0)
            a.wait(131)
            a.wait(130, lambda p: any(s['id'] == own and s['state'] == 2 for s in ships(p)))
            a.request(b'\x06' + struct.pack('<I', own), 131)
            a.request(b'\x05' + struct.pack('<I', own), 131)
            a.wait(130, lambda p: any(s['id'] == own and s['state'] == 3 for s in ships(p)))
            a.request(b'\x03', 129)
            assert len(ships(guest.wait(130))) == 2
            reply = a.request(b'\x01' + string('alpha') + string('password123'), 128)
            assert reply[1] == 0
            # Script syntax/startup errors are observable without breaking world broadcasts.
            a.request(source_packet(own, b'fail'), 131)
            a.wait(130, lambda p: any(s['id'] == own and s['state'] == 5 for s in ships(p)))
            reply = a.request(b'\x06' + struct.pack('<I', own), 131)
            assert b'SyntaxError' in reply
        with server(directory, secure=True) as port:
            client = Client(port, secure=True)
            try:
                reply = client.request(b'\x01' + string('alpha') + string('password123'), 128)
                assert reply[1] == 0
                assert len(ships(client.wait(130))) == 2
            finally:
                client.close()
    print('WS/WSS tests passed: broadcasts, two players, ownership, 1 MiB upload, lifecycle, restart')


if __name__ == '__main__':
    main()
