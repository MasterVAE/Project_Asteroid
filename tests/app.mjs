import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';

const elements = new Map();
const context = new Proxy({}, { get: (target, key) => target[key] ?? (() => {}) });
const element = (id) => {
  if (!elements.has(id)) elements.set(id, {
    id, value: '', textContent: '', hidden: false, disabled: false, dataset: {}, files: [],
    classList: { add() {}, remove() {}, toggle() {} }, setAttribute() {},
    replaceChildren(...children) { this.children = children; },
    getBoundingClientRect: () => ({ width: 900, height: 600, left: 0, top: 0 }),
    getContext: () => context,
  });
  return elements.get(id);
};
globalThis.document = { getElementById: element };
globalThis.window = { devicePixelRatio: 1 };
globalThis.location = { protocol: 'http:', host: 'localhost' };
globalThis.Option = class { constructor(text, value) { this.text = text; this.value = value; } };
globalThis.ResizeObserver = class { observe() {} };
const sockets = [];
globalThis.WebSocket = class {
  static OPEN = 1;
  constructor() { this.readyState = 1; this.sent = []; sockets.push(this); }
  send(packet) { this.sent.push(packet); }
  close() { this.readyState = 3; }
};
const protocolSource = await readFile(new URL('../frontend/browser/protocol.js', import.meta.url), 'utf8');
const protocolUrl = `data:text/javascript;base64,${Buffer.from(protocolSource).toString('base64')}`;
const appSource = await readFile(new URL('../frontend/browser/app.js', import.meta.url), 'utf8');
const { Writer } = await import(protocolUrl);
await import(`data:text/javascript;base64,${Buffer.from(appSource.replace('./protocol.js', protocolUrl)).toString('base64')}`);
const socket = sockets[0];
socket.onopen();
const dispatch = (packet) => socket.onmessage({ data: packet.finish().buffer });
const world = () => {
  const out = new Writer(); out.u8(130); out.u32(2);
  for (const [id, owner, name] of [[1, 10, 'mine'], [2, 20, '<other>']]) {
    out.u32(id); out.u32(owner); out.string(name);
    for (let i = 0; i < 5; i++) out.u32(0);
    out.u8(0);
  }
  return out;
};
element('username').value = 'mine'; element('password').value = 'password123';
element('auth-form').onsubmit({ preventDefault() {} });
assert.equal(socket.sent[0][0], 1);
assert.equal(element('submit').disabled, true);
dispatch(world());
assert.equal(element('submit').disabled, true, 'snapshot must not resolve the pending login');
const auth = new Writer(); auth.u8(128); auth.u8(0); auth.u32(10); auth.string('mine'); auth.string('');
dispatch(auth);
assert.equal(element('game-screen').hidden, false);
element('ship-list').value = '1'; element('ship-list').onchange();
assert.equal(element('script-controls').hidden, false);
assert.match(element('ship-info').textContent, /Владелец: mine/);
element('script-file').files = [{ size: 4, arrayBuffer: async () => new TextEncoder().encode('pass').buffer }];
element('script-file').onchange();
await element('attach-script').onclick();
assert.equal(socket.sent.at(-1)[0], 4);
dispatch(world());
assert.equal(element('logout').disabled, true, 'snapshot must not resolve a pending upload');
const status = new Writer(); status.u8(131); status.u32(1); status.u8(2); status.string('ready'); dispatch(status);
assert.equal(element('script-output').textContent, 'ready');
element('ship-list').value = '2'; element('ship-list').onchange();
assert.equal(element('script-controls').hidden, true);
assert.match(element('ship-info').textContent, /<other>/);
assert.equal(element('ship-info').innerHTML, undefined, 'names are rendered as text');
element('ship-list').value = '1'; element('ship-list').onchange();
element('stop-script').onclick();
assert.equal(socket.sent.at(-1)[0], 5);
console.log('Client interaction tests passed');
