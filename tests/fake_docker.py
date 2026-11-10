#!/usr/bin/env python3
"""Trusted transport test double: never executes uploaded source."""
import struct
import sys
import time

if sys.argv[1] == 'rm':
    raise SystemExit(0)
required = {'--network=none', '--read-only', '--cap-drop=ALL', '--security-opt=no-new-privileges',
            '--user=65534:65534', '--memory=128m', '--memory-swap=128m', '--cpus=0.25',
            '--pids-limit=16', '--pull=never', '--log-driver=none'}
assert required.issubset(sys.argv), sys.argv

def read_exact(size):
    result = bytearray()
    while len(result) < size:
        chunk = sys.stdin.buffer.read(size - len(result))
        if not chunk:
            raise EOFError
        result.extend(chunk)
    return bytes(result)

def receive():
    size, = struct.unpack('<I', read_exact(4))
    return read_exact(size)

def send(payload):
    sys.stdout.buffer.write(struct.pack('<I', len(payload)) + payload)
    sys.stdout.buffer.flush()

source = receive()
if source == b'fail':
    print('SyntaxError: test failure', file=sys.stderr, flush=True)
    raise SystemExit(1)
send(b'')
if source == b'oversize':
    sys.stdout.buffer.write(struct.pack('<I', 1024 * 1024 + 1))
    sys.stdout.buffer.flush()
    time.sleep(5)
elif source == b'partial':
    sys.stdout.buffer.write(b'\x05\x00')
    sys.stdout.buffer.flush()
elif source == b'final':
    send(b'x' * 100000)
elif source == b'flood':
    for _ in range(200):
        send(b'')
    time.sleep(5)
else:
    while True:
        try:
            send(receive())
        except EOFError:
            break
