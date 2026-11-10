"""Opaque bytes channel to the ship's C++ handler. No game commands are defined."""
import struct
import sys
import threading

MAX_MESSAGE = 1024 * 1024
_input = sys.stdin.buffer
_output = sys.stdout.buffer
_write_lock = threading.Lock()
_read_lock = threading.Lock()


def _read_exact(size):
    chunks = bytearray()
    while len(chunks) < size:
        chunk = _input.read(size - len(chunks))
        if not chunk:
            raise EOFError('Server channel closed')
        chunks.extend(chunk)
    return bytes(chunks)


def send(payload: bytes):
    payload = bytes(payload)
    if len(payload) > MAX_MESSAGE:
        raise ValueError('Message exceeds 1 MiB')
    with _write_lock:
        _output.write(struct.pack('<I', len(payload)) + payload)
        _output.flush()


def receive() -> bytes:
    with _read_lock:
        size, = struct.unpack('<I', _read_exact(4))
        if size > MAX_MESSAGE:
            raise ValueError('Message exceeds 1 MiB')
        return _read_exact(size)
