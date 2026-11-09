import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import vm from 'node:vm';
import {ShipInterpolation} from '../frontend/browser/interpolation.js';

const app = await fs.readFile(new URL('../frontend/browser/app.js', import.meta.url), 'utf8');
const sdk = await fs.readFile(new URL('../frontend/browser/asteroidAPI.js', import.meta.url), 'utf8');
const html = await fs.readFile(new URL('../frontend/browser/index.html', import.meta.url), 'utf8');
const ids = new Set([...html.matchAll(/id="([^"]+)"/g)].map(match => match[1]));
for (const [, id] of app.matchAll(/\$\('([^']+)'\)/g)) assert(ids.has(id), `Missing element: ${id}`);
const clone = value => JSON.parse(JSON.stringify(value));
const flush = () => new Promise(resolve => setImmediate(resolve));

class Element {
  constructor() {
    this.children = []; this.listeners = {}; this.attributes = {}; this.dataset = {}; this.style = {};
    this.value = ''; this.textContent = ''; this.hidden = false; this.parentNode = null;
  }
  addEventListener(type, listener) {this.listeners[type] = listener;}
  setAttribute(name, value) {this.attributes[name] = value;}
  append(...items) {for (const item of items) this.insertBefore(item, null);}
  insertBefore(item, before) {
    item.remove();
    const index = before ? this.children.indexOf(before) : this.children.length;
    this.children.splice(index, 0, item); item.parentNode = this;
  }
  replaceChildren(...items) {for (const child of [...this.children]) child.remove(); this.append(...items);}
  remove() {
    if (this.parentNode) this.parentNode.children.splice(this.parentNode.children.indexOf(this), 1);
    this.parentNode = null;
  }
  getBoundingClientRect() {return {bottom: 140, top: 600, height: 44};}
  setPointerCapture() {}
}
const ship = (id, owner_id, x) => ({id, owner_id, type: 'craft', position: {x, y: 0}, velocity: {x: 0, y: 0}, modules: []});
const state = {profile: {id: 1, username: 'alice'}, entities: [ship(1, 1, -100), ship(2, 2, 0), ship(3, 0, 100),
  {id: 4, owner_id: 0, type: 'asteroid', position: {x: 0, y: 100}, velocity: {x: 0, y: 0}}],
  frames: new Map(), timers: new Map(), next: 0, now: 0, fills: [], createCalls: 0, mineCalls: 0};
const nodes = new Map([...ids].map(id => [id, new Element()]));
for (const [, id] of html.matchAll(/id="([^"]+)"[^>]*\bhidden\b/g)) nodes.get(id).hidden = true;
const ctx = new Proxy({fill() {state.fills.push(this.fillStyle);}}, {get: (target, key) => target[key] ?? (() => {})});
nodes.get('map').getContext = () => ctx;
class API {
  constructor() {this.connected = false;}
  async me() {return state.profile;}
  async connect() {this.connected = true;}
  disconnect() {this.connected = false;}
  addEventListener() {}
  async logout() {state.profile = null;}
  async getMap() {
    const result = {entities: clone(state.entities)};
    if (state.pauseMap) {
      state.pauseMap = false;
      return new Promise(resolve => {state.resolveMap = () => resolve(result);});
    }
    return result;
  }
  async getEntity(id) {return clone(state.entities.find(entity => entity.id === id));}
  async getMyEntities() {
    state.mineCalls++;
    const result = {entities: clone(state.entities.filter(entity => entity.owner_id === state.profile.id))};
    if (state.pauseMine) {
      state.pauseMine = false;
      return new Promise(resolve => {state.resolveMine = () => resolve(result);});
    }
    return result;
  }
  async createCraft() {
    state.createCalls++;
    if (state.createError) throw new Error('Создание отклонено');
    return new Promise(resolve => {state.resolveCreate = () => {
      const entity = ship(5, state.profile.id, 200);
      state.entities.push(entity); resolve(clone(entity));
    };});
  }
}
const sandbox = {AsteroidAPI: API, ShipInterpolation, document: {
  getElementById: id => nodes.get(id), createElement: () => new Element(), addEventListener() {}, documentElement: {},
  querySelector: selector => ({getBoundingClientRect: () => selector === 'header' ? {bottom: 140} : selector === 'footer' ? {height: 44} : {top: 600}})
}, performance: {now: () => state.now}, innerWidth: 1000, innerHeight: 700, devicePixelRatio: 1,
  addEventListener() {}, requestAnimationFrame: fn => {state.frames.set(++state.next, fn); return state.next;},
  setTimeout: (fn, delay) => {state.timers.set(++state.next, {fn, delay}); return state.next;},
  clearTimeout: id => state.timers.delete(id), ResizeObserver: class {constructor(fn) {this.fn = fn;} observe() {this.fn();}}, console};
const context = vm.createContext(sandbox);
const evaluate = code => vm.runInContext(code, context);
const render = () => {state.fills = []; const callbacks = [...state.frames.values()]; state.frames.clear(); callbacks.forEach(fn => fn(state.now));};
evaluate(app.replace(/^import .*;\n/gm, ''));
await flush();
assert.equal(nodes.get('account').hidden, false);
render();
assert.equal(state.fills.filter(color => color === '#6edbff').length, 1);
assert.equal(state.fills.filter(color => color === '#f5d76e').length, 2);
assert.equal(nodes.get('my-craft-count').textContent, 1);
assert.equal(nodes.get('other-craft-count').textContent, 2);
assert.equal(nodes.get('map-controls').style.bottom, '72px');

nodes.get('fleet-open').onclick();
await flush();
assert(state.mineCalls > 0);
assert.equal(nodes.get('fleet-list').children.length, 1);
const originalRow = nodes.get('fleet-list').children[0];
await evaluate('refreshFleet()');
assert.equal(nodes.get('fleet-list').children[0], originalRow); // Refresh preserves keyboard-focusable nodes.
originalRow.children[0].onclick();
await flush();
assert.equal(evaluate('selected.id'), 1);
assert.equal(evaluate('camera.x'), -100);
assert.equal(originalRow.children[0].attributes['aria-pressed'], 'true');

// A successful mutation must survive an older map response still in flight.
state.pauseMap = true;
const oldPoll = evaluate('poll(generation)');
await flush();
const creating = nodes.get('craft-create').onclick();
await flush();
assert.equal(nodes.get('craft-create').disabled, true);
await nodes.get('craft-create').onclick();
assert.equal(state.createCalls, 1);
state.resolveCreate();
await creating;
await flush();
assert.equal(evaluate('selected.id'), 5);
assert.equal(nodes.get('fleet-list').children.length, 2);
assert.equal(nodes.get('craft-create').disabled, false);
state.resolveMap();
await oldPoll;
await flush();
assert(evaluate('entities.some(entity => entity.id === 5)'));
assert.equal(nodes.get('my-craft-count').textContent, 2);
evaluate('fit()');
render();
assert.equal(state.fills.filter(color => color === '#6edbff').length, 2);

state.createError = true;
await nodes.get('craft-create').onclick();
assert.equal(nodes.get('fleet-message').textContent, 'Создание отклонено');
assert.equal(nodes.get('craft-create').disabled, false);
assert.equal(evaluate('entities.length'), 5);
state.entities = state.entities.filter(entity => entity.id !== 5);
await evaluate('poll(generation)');
await flush();
assert.equal(nodes.get('fleet-list').children.length, 1);
assert.equal(evaluate('selected'), null);

// In-flight fleet replies cannot restore another account's list after logout.
state.pauseMine = true;
const lateFleet = evaluate('refreshFleet()');
await flush();
await nodes.get('disconnect').onclick();
state.resolveMine();
await lateFleet;
assert.equal(nodes.get('fleet').hidden, true);
assert.equal(nodes.get('fleet-list').children.length, 0);
assert.equal(evaluate('entities.length'), 0);
assert.equal(nodes.get('craft-create').disabled, true);

state.profile = {id: 2, username: 'bob'};
evaluate('showAccount({id:2, username:"bob"}); wanted=true;');
await evaluate('connect()');
render();
assert.equal(state.fills.filter(color => color === '#6edbff').length, 1);
assert.equal(state.fills.filter(color => color === '#f5d76e').length, 2);
nodes.get('fleet-open').onclick();
await flush();
assert.equal(evaluate('myEntities[0].id'), 2);
sandbox.innerWidth = 400;
nodes.get('fleet-list').children[0].children[0].onclick();
assert.equal(nodes.get('fleet').hidden, true); // Mobile selection reveals the focused ship.

state.profile = {id: 10, username: 'empty'};
evaluate('showAccount({id:10, username:"empty"}); wanted=true;');
await evaluate('connect()');
nodes.get('fleet-open').onclick();
await flush();
assert.equal(nodes.get('fleet-empty').hidden, false);
assert.equal(nodes.get('fleet-list').children.length, 0);

// Public browser SDK maps the same method names to the wire protocol.
class Events {addEventListener() {} dispatchEvent() {}}
const sdkContext = vm.createContext({EventTarget: Events, URL, location: {href: 'https://game.example/'}});
vm.runInContext(sdk.replaceAll('export class ', 'class ') + '\nglobalThis.Client = AsteroidAPI;', sdkContext);
const client = new sdkContext.Client();
const commands = [];
client.request = command => {commands.push(command); return Promise.resolve({entities: []});};
await client.getMyEntities();
await client.createCraft();
assert.deepEqual(commands, ['get_my_entities', 'create_craft']);
console.log('PASS: owned/foreign colours, fleet menu/filtering/selection, refresh, create/double-click, stale snapshots, logout/account switching and SDK commands');
