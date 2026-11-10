import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';

// Load the browser ES module without changing the project's package settings.
const source = await readFile(new URL('../frontend/browser/protocol.js', import.meta.url), 'utf8');
const { Writer, Reader, MAX_LONG_STRING_BYTES, MAX_MESSAGE_BYTES, loginRequest, decodeResponse, attachScriptRequest, scriptRequest } =
  await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
const encoder = new TextEncoder();

for (const value of ['', 'a\0b', 'x'.repeat(65535), 'x'.repeat(65536),
  'x'.repeat(MAX_LONG_STRING_BYTES), 'я'.repeat(MAX_LONG_STRING_BYTES / 2)]) {
  const writer = new Writer();
  writer.u8(42);
  writer.longString(value);
  writer.string('tail');
  const bytes = writer.finish();
  const size = encoder.encode(value).length;
  assert.equal(bytes.length, 1 + 4 + size + 2 + 4);
  assert.ok(bytes.length <= MAX_MESSAGE_BYTES);
  for (let i = 0; i < 4; i++) assert.equal(bytes[1 + i], (size >>> (8 * i)) & 255);
  const reader = new Reader(bytes.buffer);
  assert.equal(reader.u8(), 42);
  assert.equal(reader.longString(), value);
  assert.equal(reader.string(), 'tail');
  reader.finish();
}

const writer = new Writer();
writer.u8(42);
for (const value of ['x'.repeat(MAX_LONG_STRING_BYTES + 1), 'я'.repeat(MAX_LONG_STRING_BYTES / 2 + 1)]) {
  assert.throws(() => writer.longString(value), /1 МиБ/);
  assert.deepEqual(writer.finish(), new Uint8Array([42]));
}
assert.throws(() => writer.string('x'.repeat(65536)), /слишком длинная/);
for (const size of [MAX_LONG_STRING_BYTES + 1, 0xffffffff]) {
  const header = new Writer();
  header.u32(size);
  assert.throws(() => new Reader(header.finish().buffer).longString(), /1 МиБ/);
}
const truncated = new Uint8Array([3, 0, 0, 0, 97, 98, 99]);
for (let size = 0; size < truncated.length; size++) {
  assert.throws(() => new Reader(truncated.slice(0, size).buffer).longString());
}
const trailing = new Reader(new ArrayBuffer(5));
assert.equal(trailing.longString(), '');
assert.throws(() => trailing.finish(), /Лишние данные/);
assert.deepEqual(loginRequest({ username: 'a', password: 'b' }), new Uint8Array([1, 1, 0, 97, 1, 0, 98]));
assert.deepEqual(decodeResponse(new Uint8Array([255, 3, 0, 97, 98, 99]).buffer), { type: 255, message: 'abc' });
console.log('JavaScript protocol tests passed');

const upload = attachScriptRequest(7, 'print("hello")');
assert.equal(upload[0], 4);
const uploadReader = new Reader(upload.buffer);
assert.equal(uploadReader.u8(), 4); assert.equal(uploadReader.u32(), 7);
assert.equal(uploadReader.longString(), 'print("hello")'); uploadReader.finish();
assert.deepEqual(scriptRequest(7, true), new Uint8Array([5, 7, 0, 0, 0]));
assert.deepEqual(scriptRequest(7), new Uint8Array([6, 7, 0, 0, 0]));
const snapshot = new Writer();
snapshot.u8(130); snapshot.u32(1); snapshot.u32(7); snapshot.u32(12); snapshot.string('капитан');
for (const value of [1.5, -2.5, 0, 42]) {
  const bytes = new ArrayBuffer(4); new DataView(bytes).setFloat32(0, value, true);
  snapshot.u32(new DataView(bytes).getUint32(0, true));
}
snapshot.u32(3); snapshot.u8(2);
assert.deepEqual(decodeResponse(snapshot.finish().buffer), { type: 130, crafts: [
  { id: 7, ownerId: 12, owner: 'капитан', x: 1.5, y: -2.5, vx: 0, vy: 42, modules: 3, scriptState: 2 }
] });
assert.throws(() => decodeResponse(new Uint8Array([130, 255, 255, 255, 255]).buffer));
assert.throws(() => decodeResponse(snapshot.finish().slice(0, -1).buffer));
const log = new Writer(); log.u8(131); log.u32(7); log.u8(5); log.string('SyntaxError');
assert.deepEqual(decodeResponse(log.finish().buffer), { type: 131, craftId: 7, state: 5, log: 'SyntaxError' });
console.log('World and script protocol tests passed');
