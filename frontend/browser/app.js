import { AsteroidAPI } from './asteroidAPI.js';
import { ShipInterpolation } from './interpolation.js';

const $ = id => document.getElementById(id);
const api = new AsteroidAPI();
const interpolation = new ShipInterpolation();
const canvas = $('map');
const ctx = canvas.getContext('2d');
const camera = { x: 0, y: 0, zoom: 1 };
let width = 0, height = 0, entities = [], selected = null;
let wanted = false, generation = 0, retry = 0;
let registering = false, authVersion = 0, keyViewVersion = 0;
let currentUser = null, myEntities = [], fleetRequest = 0, creatingCraft = false;
const fleetRows = new Map(), pendingEntities = new Map();
let retryTimer, pollTimer, drag;
let frame = null, renderedAt = 0;

function status(text, connected = false) {
  $('status').textContent = text;
  $('status').dataset.connected = String(connected);
}

function retryConnection() {
  if (!wanted || retryTimer) return;
  clearTimeout(pollTimer);
  interpolation.freeze(performance.now());
  draw();
  const delay = Math.min(1000 * 2 ** retry++, 10000);
  status(`Повтор через ${delay / 1000} с`);
  $('message').textContent = 'Нет связи. Последняя полученная карта сохранена на экране.';
  retryTimer = setTimeout(() => { retryTimer = null; connect(); }, delay);
}

async function connect() {
  const current = ++generation;
  status('Подключение…');
  try {
    await api.connect();
    if (current !== generation || !wanted) return;
    retry = 0;
    status('Подключено', true);
    await poll(current);
  } catch (error) {
    if (current !== generation || !wanted) return;
    $('message').textContent = error.message;
    checkConnection();
  }
}

async function poll(current) {
  try {
    const pendingBeforeRequest = new Set(pendingEntities.keys());
    const map = await api.getMap();
    if (current !== generation || !wanted) return;
    entities = map.entities.filter(entity => entity?.position &&
      Number.isFinite(entity.position.x) && Number.isFinite(entity.position.y));
    const received = new Set(entities.map(entity => entity.id));
    for (const [id, entity] of pendingEntities) {
      if (received.has(id) || pendingBeforeRequest.has(id)) pendingEntities.delete(id);
      else entities.push(entity);
    }
    interpolation.update(entities, performance.now());
    if (!hasMap) { hasMap = true; fit(); }
    if (selected) selected = entities.find(entity => selected.id !== undefined
      ? entity.id === selected.id
      : entity.type === selected.type && entity.position.x === selected.position.x && entity.position.y === selected.position.y) ?? null;
    if (!selected) $('selection').hidden = true;
    updateCounts();
    syncFleetSelection();
    if (!$('fleet').hidden) refreshFleet();
    $('message').textContent = `${entities.length ? 'Нажмите на объект, чтобы посмотреть параметры.' : 'Сервер вернул пустую карту.'} Обновлено: ${new Date().toLocaleTimeString('ru-RU')}`;
    draw();
    pollTimer = setTimeout(() => poll(current), 1000);
  } catch (error) {
    if (current !== generation || !wanted) return;
    if (error.code === 'AUTH_REQUIRED') { expireSession(); return; }
    $('message').textContent = error.message;
    retryConnection();
  }
}

let hasMap = false;
function stop() {
  wanted = false;
  generation++;
  clearTimeout(retryTimer);
  clearTimeout(pollTimer);
  retryTimer = null;
  interpolation.freeze(performance.now());
  draw();
  api.disconnect();
  $('disconnect').disabled = true;
  $('craft-create').disabled = true;
}

$('register-mode').onclick = () => {
  registering = !registering;
  $('connect').textContent = registering ? 'Зарегистрироваться' : 'Войти';
  $('register-mode').textContent = registering ? 'Уже есть аккаунт' : 'Создать аккаунт';
  $('password').autocomplete = registering ? 'new-password' : 'current-password';
  $('message').textContent = registering ? 'Логин: латинские буквы, цифры, _ и -. Пароль: от 12 символов.' : 'Введите логин и пароль.';
};

function showAccount(profile) {
  currentUser = profile;
  authVersion++;
  keyViewVersion++;
  registering = false;
  $('connect').textContent = 'Войти';
  $('register-mode').textContent = 'Создать аккаунт';
  $('password').autocomplete = 'current-password';
  $('connection').hidden = !!profile;
  $('account').hidden = !profile;
  $('account-name').textContent = profile?.username || '';
  $('disconnect').disabled = !profile;
  creatingCraft = false;
  $('craft-create').disabled = !profile;
  $('craft-create').textContent = 'Создать корабль';
  myEntities = [];
  pendingEntities.clear();
  fleetRows.clear();
  $('fleet-list').replaceChildren();
  $('fleet-empty').hidden = true;
  $('fleet-message').textContent = '';
  closeFleet();
  $('fleet-open').textContent = 'Мои корабли (0)';
  $('keys').hidden = true;
  $('key-secret').value = '';
  $('key-password').value = '';
  $('key-result').hidden = true;
  $('key-list').replaceChildren();
}

function clearMap() {
  entities = [];
  selected = null;
  hasMap = false;
  interpolation.update([], performance.now());
  $('selection').hidden = true;
  $('asteroid-count').textContent = $('craft-count').textContent = '0';
  updateCounts();
  draw();
}

function expireSession() {
  stop();
  showAccount(null);
  clearMap();
  status('Войдите в аккаунт');
  $('message').textContent = 'Сессия завершена. Войдите снова.';
}

async function checkConnection() {
  const current = generation;
  if (!wanted) return;
  try { await api.me(); }
  catch (error) {
    if (current !== generation || !wanted) return;
    if (error.code === 'AUTH_REQUIRED') { expireSession(); return; }
  }
  if (current === generation && wanted) retryConnection();
}

$('connection').addEventListener('submit', async event => {
  event.preventDefault();
  stop();
  const current = generation;
  $('connect').disabled = $('register-mode').disabled = true;
  status(registering ? 'Регистрация…' : 'Вход…');
  try {
    const profile = await api[registering ? 'register' : 'login']($('username').value.trim(), $('password').value);
    if (current !== generation) return;
    $('password').value = '';
    showAccount(profile);
    clearMap();
    wanted = true;
    retry = 0;
    connect();
  } catch (error) {
    if (current !== generation) return;
    status('Ошибка входа');
    $('message').textContent = error.message;
  } finally { $('connect').disabled = $('register-mode').disabled = false; }
});
$('disconnect').onclick = async () => {
  stop();
  try {
    await api.logout();
    showAccount(null);
    clearMap();
    status('Вы вышли');
    $('message').textContent = 'Введите логин и пароль для входа.';
  } catch (error) {
    if (error.code === 'AUTH_REQUIRED') { expireSession(); return; }
    $('disconnect').disabled = false;
    $('message').textContent = `Выход не завершён: ${error.message}`;
  }
};
api.addEventListener('close', checkConnection);

async function loadKeys() {
  const version = authVersion;
  const keys = await api.listApiKeys();
  if (version !== authVersion) return;
  $('key-list').replaceChildren();
  for (const key of keys) {
    const entry = document.createElement('div');
    entry.className = 'key-entry';
    const title = document.createElement('strong');
    title.textContent = key.name;
    const description = document.createElement('p');
    description.textContent = `${key.prefix}… · до ${new Date(key.expires_at * 1000).toLocaleString('ru-RU')} · чтение карты`;
    const revoke = document.createElement('button');
    revoke.type = 'button';
    revoke.textContent = 'Отозвать';
    revoke.onclick = async () => {
      revoke.disabled = true;
      try { await api.revokeApiKey(key.id); if (version === authVersion) await loadKeys(); }
      catch (error) { if (version === authVersion) { $('key-message').textContent = error.message; revoke.disabled = false; } }
    };
    entry.append(title, description, revoke);
    $('key-list').append(entry);
  }
  if (!keys.length) $('key-list').textContent = 'API-ключей пока нет.';
}

$('keys-open').onclick = async () => {
  closeFleet();
  const version = authVersion;
  keyViewVersion++;
  $('keys').hidden = false;
  $('key-message').textContent = '';
  try { await loadKeys(); }
  catch (error) {
    if (version !== authVersion) return;
    if (error.code === 'AUTH_REQUIRED') expireSession(); else $('key-message').textContent = error.message;
  }
};
$('keys-close').onclick = () => {
  keyViewVersion++;
  $('keys').hidden = true;
  $('key-secret').value = '';
  $('key-password').value = '';
  $('key-result').hidden = true;
};
$('key-form').addEventListener('submit', async event => {
  event.preventDefault();
  const version = authVersion;
  const view = keyViewVersion;
  $('key-create').disabled = true;
  $('key-secret').value = '';
  $('key-result').hidden = true;
  try {
    const key = await api.createApiKey($('key-name').value.trim(), $('key-password').value, Number($('key-days').value));
    if (version !== authVersion || view !== keyViewVersion) return;
    $('key-secret').value = key.key;
    $('key-result').hidden = false;
    $('key-message').textContent = 'Ключ создан. Храните его как пароль.';
    await loadKeys();
  } catch (error) { if (version === authVersion) $('key-message').textContent = error.message; }
  finally { $('key-password').value = ''; $('key-create').disabled = false; }
});

function isMine(entity) {
  return currentUser !== null && entity.owner_id === currentUser.id;
}

function updateCounts() {
  const ships = entities.filter(entity => entity.type === 'craft');
  const mine = ships.filter(isMine).length;
  $('asteroid-count').textContent = entities.filter(entity => entity.type === 'asteroid').length;
  $('craft-count').textContent = ships.length;
  $('my-craft-count').textContent = mine;
  $('other-craft-count').textContent = ships.length - mine;
  $('fleet-open').textContent = `Мои корабли (${mine})`;
}

function closeFleet() {
  fleetRequest++;
  $('fleet').hidden = true;
  $('fleet-open').setAttribute('aria-expanded', 'false');
}

function openFleet() {
  $('keys-close').onclick();
  $('fleet').hidden = false;
  $('fleet-open').setAttribute('aria-expanded', 'true');
}

function syncFleetSelection() {
  for (const [id, row] of fleetRows) row.button.setAttribute('aria-pressed', String(selected?.id === id));
}

function renderFleet() {
  const ships = myEntities.filter(entity => entity.type === 'craft').sort((a, b) => a.id - b.id);
  const ids = new Set(ships.map(ship => ship.id));
  for (const [id, row] of fleetRows) {
    if (!ids.has(id)) { row.item.remove(); fleetRows.delete(id); }
  }
  ships.forEach((ship, index) => {
    let row = fleetRows.get(ship.id);
    if (!row) {
      const item = document.createElement('li');
      const button = document.createElement('button');
      button.type = 'button';
      button.className = 'fleet-ship';
      const title = document.createElement('strong');
      title.textContent = `Корабль #${ship.id}`;
      const position = document.createElement('span');
      button.append(title, position);
      button.onclick = () => {
        focusShip(ship.id);
        if (innerWidth <= 640) closeFleet();
      };
      item.append(button);
      row = {item, button, position};
      fleetRows.set(ship.id, row);
    }
    row.position.textContent = `X: ${ship.position.x.toFixed(2)} · Y: ${ship.position.y.toFixed(2)}`;
    const list = $('fleet-list');
    if (list.children[index] !== row.item) list.insertBefore(row.item, list.children[index] || null);
  });
  $('fleet-empty').hidden = ships.length !== 0;
  $('fleet-message').textContent = ships.length ? `Кораблей: ${ships.length}. Нажмите на корабль, чтобы найти его на карте.` : '';
  $('fleet-open').textContent = `Мои корабли (${ships.length})`;
  syncFleetSelection();
}

async function refreshFleet() {
  const version = authVersion, request = ++fleetRequest;
  try {
    const result = await api.getMyEntities();
    if (version !== authVersion || request !== fleetRequest || $('fleet').hidden) return;
    myEntities = result.entities.filter(entity => entity?.position && isMine(entity) &&
      Number.isFinite(entity.position.x) && Number.isFinite(entity.position.y));
    renderFleet();
  } catch (error) {
    if (version !== authVersion || request !== fleetRequest || $('fleet').hidden) return;
    if (error.code === 'AUTH_REQUIRED') { expireSession(); return; }
    $('fleet-message').textContent = error.message;
  }
}

function includeEntity(entity) {
  const existing = entities.find(candidate => candidate.id === entity.id);
  if (existing) return existing;
  // An older map response may still be in flight when a ship is created.
  pendingEntities.set(entity.id, entity);
  entities.push(entity);
  updateCounts();
  return entity;
}

function focusShip(id) {
  let entity = entities.find(candidate => candidate.id === id);
  if (!entity) {
    const own = myEntities.find(candidate => candidate.id === id);
    if (!own) return;
    entity = includeEntity(own);
  }
  const pose = interpolation.sample(entity, performance.now());
  camera.x = pose.position.x;
  camera.y = pose.position.y;
  camera.zoom = Math.max(camera.zoom, 1);
  selectEntity(entity);
}

$('fleet-open').onclick = () => {
  if (!$('fleet').hidden) { closeFleet(); return; }
  openFleet();
  $('fleet-message').textContent = 'Загрузка…';
  refreshFleet();
};
$('fleet-close').onclick = closeFleet;
$('craft-create').onclick = async () => {
  if (!currentUser || !wanted || creatingCraft) return;
  const version = authVersion;
  creatingCraft = true;
  $('craft-create').disabled = true;
  $('craft-create').textContent = 'Создание…';
  openFleet();
  $('fleet-message').textContent = 'Создание корабля…';
  try {
    const ship = await api.createCraft();
    if (version !== authVersion) return;
    includeEntity(ship);
    focusShip(ship.id);
    if (!$('fleet').hidden) await refreshFleet();
  } catch (error) {
    if (version !== authVersion) return;
    if (error.code === 'AUTH_REQUIRED') { expireSession(); return; }
    $('fleet-message').textContent = error.message;
    $('message').textContent = error.message;
  } finally {
    if (version === authVersion) {
      creatingCraft = false;
      $('craft-create').disabled = !wanted;
      $('craft-create').textContent = 'Создать корабль';
    }
  }
};


function project(position) {
  return { x: width / 2 + (position.x - camera.x) * camera.zoom,
    y: height / 2 - (position.y - camera.y) * camera.zoom };
}

function fit() {
  if (!entities.length) { camera.x = camera.y = 0; camera.zoom = 1; draw(); return; }
  const positions = entities.map(entity => interpolation.sample(entity, performance.now()).position);
  const xs = positions.map(position => position.x), ys = positions.map(position => position.y);
  const minX = Math.min(...xs), maxX = Math.max(...xs), minY = Math.min(...ys), maxY = Math.max(...ys);
  const top = document.querySelector('header').getBoundingClientRect().bottom + 24;
  const bottom = document.querySelector('.toolbar').getBoundingClientRect().top - 24;
  camera.zoom = Math.max(.02, Math.min(20, (width - 64) / Math.max(100, maxX - minX),
    Math.max(80, bottom - top) / Math.max(100, maxY - minY)));
  camera.x = (minX + maxX) / 2;
  camera.y = (minY + maxY) / 2 + ((top + bottom) / 2 - height / 2) / camera.zoom;
  draw();
}

function zoom(factor, x = width / 2, y = height / 2) {
  const previous = camera.zoom;
  camera.zoom = Math.max(.02, Math.min(20, previous * factor));
  camera.x += (x - width / 2) * (1 / previous - 1 / camera.zoom);
  camera.y -= (y - height / 2) * (1 / previous - 1 / camera.zoom);
  draw();
}

function draw() {
  if (frame === null) frame = requestAnimationFrame(render);
}

function render(now) {
  frame = null;
  renderedAt = now;
  const dpr = Math.min(devicePixelRatio || 1, 2);
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.fillStyle = '#080e19';
  ctx.fillRect(0, 0, width, height);
  const step = 10 ** Math.ceil(Math.log10(70 / camera.zoom));
  const spacing = step * camera.zoom;
  const origin = project({ x: 0, y: 0 });
  ctx.strokeStyle = '#182638'; ctx.lineWidth = 1;
  ctx.beginPath();
  for (let x = ((origin.x % spacing) + spacing) % spacing; x < width; x += spacing) {
    ctx.moveTo(x, 0); ctx.lineTo(x, height);
  }
  for (let y = ((origin.y % spacing) + spacing) % spacing; y < height; y += spacing) {
    ctx.moveTo(0, y); ctx.lineTo(width, y);
  }
  ctx.stroke();
  ctx.strokeStyle = '#2d425d';
  ctx.beginPath(); ctx.moveTo(origin.x, 0); ctx.lineTo(origin.x, height);
  ctx.moveTo(0, origin.y); ctx.lineTo(width, origin.y); ctx.stroke();

  for (const entity of entities) {
    const pose = interpolation.sample(entity, now);
    const p = project(pose.position);
    if (p.x < -25 || p.y < -25 || p.x > width + 25 || p.y > height + 25) continue;
    ctx.save(); ctx.translate(p.x, p.y);
    if (selected === entity) {
      ctx.strokeStyle = '#fff'; ctx.lineWidth = 1;
      ctx.beginPath(); ctx.arc(0, 0, 16, 0, Math.PI * 2); ctx.stroke();
    }
    ctx.beginPath();
    if (entity.type === 'craft') {
      const velocity = pose.velocity;
      if (velocity && Math.hypot(velocity.x, velocity.y) > 0) ctx.rotate(Math.atan2(velocity.x, velocity.y));
      ctx.moveTo(0, -11); ctx.lineTo(8, 8); ctx.lineTo(0, 4); ctx.lineTo(-8, 8);
      ctx.fillStyle = isMine(entity) ? '#6edbff' : ' #f56ef5';
      ctx.strokeStyle = isMine(entity) ? '#c1f1ff' : '#f7baed';
    } else {
      const seed = Number(entity.id) || 0;
      for (let i = 0; i < 8; i++) {
        const angle = i * Math.PI / 4;
        const radius = 6 + ((seed + i * 7) % 4);
        const x = Math.cos(angle) * radius, y = Math.sin(angle) * radius;
        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      }
      ctx.fillStyle = entity.type === 'asteroid' ? '#997c59' : '#748092'; ctx.strokeStyle = '#d7ad77';
    }
    ctx.closePath(); ctx.fill(); ctx.stroke(); ctx.restore();
  }
  $('scale').textContent = `${Math.round(camera.zoom * 100)}% · сетка ${step} ед.`;
  if (interpolation.isAnimating(now)) draw();
}

async function selectAt(x, y) {
  let nearest = null, distance = 20;
  for (const entity of entities) {
    const p = project(interpolation.sample(entity, renderedAt).position), d = Math.hypot(x - p.x, y - p.y);
    if (d < distance) { nearest = entity; distance = d; }
  }
  await selectEntity(nearest);
}

async function selectEntity(nearest) {
  selected = nearest;
  syncFleetSelection();
  draw();
  const panel = $('selection');
  panel.hidden = !nearest;
  if (!nearest) return;
  const current = generation;
  const id = nearest.id;
  function show(entity, details = '') {
    panel.replaceChildren();
    const title = document.createElement('h2');
    title.textContent = `${entity.type === 'craft' ? 'Корабль' : 'Астероид'}${id === undefined ? '' : ` #${id}`}`;
    panel.append(title);
    const lines = [`X: ${entity.position.x.toFixed(2)} · Y: ${entity.position.y.toFixed(2)}`,
      `Скорость: ${Number(entity.velocity?.x ?? 0).toFixed(2)}, ${Number(entity.velocity?.y ?? 0).toFixed(2)}`];
    if (entity.type === 'craft') lines.push(isMine(entity) ? 'Ваш корабль' : `Чужой корабль · владелец #${entity.owner_id}`);
    if (Array.isArray(entity.resources)) for (const resource of entity.resources) lines.push(`${resource.name}: ${resource.amount}`);
    if (Array.isArray(entity.modules)) lines.push(`Модули: ${entity.modules.map(module => ['Добыча', 'Производство', 'Хранилище'][module.type] ?? module.type).join(', ') || 'нет'}`);
    if (details) lines.push(details);
    for (const line of lines) { const p = document.createElement('p'); p.textContent = line; panel.append(p); }
  }
  show(nearest);
  if (!api.connected || !Number.isSafeInteger(id)) return;
  try {
    const entity = await api.getEntity(id);
    if (current === generation && selected?.id === id && entity.position) show(entity);
  } catch (error) {
    if (current === generation && selected?.id === id) show(nearest, error.message);
  }
}

canvas.addEventListener('pointerdown', event => {
  if (event.button !== 0 || drag) return;
  canvas.setPointerCapture(event.pointerId);
  drag = { id: event.pointerId, x: event.clientX, y: event.clientY, startX: event.clientX, startY: event.clientY, moved: false };
});
canvas.addEventListener('pointermove', event => {
  if (!drag || event.pointerId !== drag.id) return;
  if (Math.hypot(event.clientX - drag.startX, event.clientY - drag.startY) > 4) drag.moved = true;
  camera.x -= (event.clientX - drag.x) / camera.zoom;
  camera.y += (event.clientY - drag.y) / camera.zoom;
  drag.x = event.clientX; drag.y = event.clientY; draw();
});
canvas.addEventListener('pointerup', event => {
  if (!drag || event.pointerId !== drag.id) return;
  const click = !drag.moved;
  drag = null;
  if (click) selectAt(event.clientX, event.clientY);
});
canvas.addEventListener('pointercancel', () => { drag = null; });
canvas.addEventListener('lostpointercapture', () => { drag = null; });
canvas.addEventListener('wheel', event => {
  event.preventDefault(); zoom(Math.exp(-Math.max(-100, Math.min(100, event.deltaY)) * .002), event.clientX, event.clientY);
}, { passive: false });
canvas.addEventListener('keydown', event => {
  if (['+', '=', '-', 'f', 'F', 'ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown'].includes(event.key)) event.preventDefault();
  if (event.key === '+' || event.key === '=') zoom(1.25);
  if (event.key === '-') zoom(.8);
  if (event.key.toLowerCase() === 'f') fit();
  if (event.key === 'ArrowLeft') camera.x -= 40 / camera.zoom;
  if (event.key === 'ArrowRight') camera.x += 40 / camera.zoom;
  if (event.key === 'ArrowUp') camera.y += 40 / camera.zoom;
  if (event.key === 'ArrowDown') camera.y -= 40 / camera.zoom;
  draw();
});
$('fit').onclick = fit;
$('zoom-in').onclick = () => zoom(1.25);
$('zoom-out').onclick = () => zoom(.8);
$('fullscreen').onclick = async () => {
  try {
    if (document.fullscreenElement) await document.exitFullscreen();
    else await document.documentElement.requestFullscreen();
  } catch { $('message').textContent = 'Полноэкранный режим недоступен в этом браузере.'; }
};
document.addEventListener('fullscreenchange', () => {
  $('fullscreen').textContent = document.fullscreenElement ? 'Выйти из экрана' : 'Полный экран';
});
function resize() {
  width = innerWidth; height = innerHeight;
  const dpr = Math.min(devicePixelRatio || 1, 2);
  canvas.width = Math.round(width * dpr); canvas.height = Math.round(height * dpr); draw();
}
addEventListener('resize', resize);
const footer = document.querySelector('footer');
const footerObserver = new ResizeObserver(() => {
  $('map-controls').style.bottom = `${footer.getBoundingClientRect().height + 28}px`;
});
footerObserver.observe(footer);
resize();
async function restoreSession() {
  const current = generation;
  try {
    const profile = await api.me();
    if (current !== generation) return;
    showAccount(profile);
    wanted = true;
    connect();
  } catch (error) {
    if (current !== generation) return;
    status('Войдите в аккаунт');
    $('message').textContent = error.code === 'AUTH_REQUIRED' ? 'Войдите или создайте аккаунт.' : error.message;
  }
}
restoreSession();
