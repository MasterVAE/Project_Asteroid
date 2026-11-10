import { Response, Status, loginRequest, registerRequest, logoutRequest, decodeResponse, attachScriptRequest, scriptRequest, MAX_LONG_STRING_BYTES } from './protocol.js';

const $ = (id) => document.getElementById(id);
let socket;
let registration = false;
let pending = false;
let player = null;
let crafts = [];
let worldReceived = false;
let selectedId = null;
let hitTargets = [];
let scriptLoading = false;
const scriptStates = ['Нет скрипта', 'Запускается', 'Работает', 'Остановлен', 'Завершён', 'Ошибка'];
const message = (text) => { $(player ? 'game-message' : 'message').textContent = text; };

function updateButtons() {
  const ready = socket?.readyState === WebSocket.OPEN && !pending;
  $('submit').disabled = !ready;
  $('logout').disabled = !ready;
  $('login-tab').disabled = pending;
  $('register-tab').disabled = pending;
  const own = !!player && crafts.some((craft) => craft.id === selectedId && craft.ownerId === player.userId);
  $('attach-script').disabled = !ready || !own || scriptLoading || !$('script-file').files.length;
  $('stop-script').disabled = !ready || !own;
  $('script-log').disabled = !ready || !own;
  $('submit-label').textContent = pending ? 'Ожидание…' : registration ? 'Создать аккаунт' : 'Войти';
}

function showPlayer(value) {
  player = value;
  delete $('ship-list').dataset.signature;
  selectedId = null;
  $('script-output').textContent = '';
  $('script-file').value = '';
  $('game-message').textContent = '';
  $('auth-screen').hidden = !!player;
  $('game-screen').hidden = !player;
  $('player-name').textContent = player ? player.username : '';
  $('password').value = '';
  $('confirm-password').value = '';
  updateShipInfo();
  if (player) drawField();
}

function setMode(value) {
  registration = value;
  $('auth-title').textContent = value ? 'Создать аккаунт' : 'Вход в систему';
  $('login-tab').classList.toggle('active', !value);
  $('register-tab').classList.toggle('active', value);
  $('login-tab').setAttribute('aria-pressed', String(!value));
  $('register-tab').setAttribute('aria-pressed', String(value));
  $('confirm-group').hidden = !value;
  $('confirm-password').required = value;
  $('password').autocomplete = value ? 'new-password' : 'current-password';
  $('message').textContent = '';
  updateButtons();
}

function send(packet) {
  if (socket?.readyState !== WebSocket.OPEN || pending) return;
  socket.send(packet);
  pending = true;
  message('');
  updateButtons();
}

function connect() {
  crafts = [];
  worldReceived = false;
  updateShipInfo();
  // HTTPS pages automatically use WSS on the same host and port.
  socket = new WebSocket(`${location.protocol === 'https:' ? 'wss:' : 'ws:'}//${location.host}/ws`);
  socket.binaryType = 'arraybuffer';
  socket.onopen = () => {
    $('connection').textContent = 'Сервер на связи';
    $('connection').classList.add('online');
    $('message').textContent = '';
    updateButtons();
  };
  socket.onmessage = ({ data }) => {
    try {
      const response = decodeResponse(data);
      // World snapshots arrive independently of the response to a UI request.
      if (response.type === Response.World) {
        worldReceived = true;
        crafts = response.crafts;
        updateShipInfo(); drawField(); updateButtons();
        return;
      }
      pending = false;
      switch (response.type) {
        case Response.Auth:
          if (response.status === Status.Ok) showPlayer(response);
          else message(response.message);
          break;
        case Response.Logout:
          showPlayer(null);
          break;
        case Response.Script:
          if (response.craftId === selectedId) {
            $('script-output').textContent = response.log || 'Журнал пуст.';
            message(`Скрипт: ${scriptStates[response.state] ?? 'неизвестно'}`);
          }
          break;
        case Response.Error:
          message(response.message);
          break;
      }
    } catch (error) {
      message(`Ошибка протокола: ${error.message}`);
      socket.close();
    }
    updateButtons();
  };
  socket.onclose = () => {
    const wasLoggedIn = !!player;
    pending = false;
    crafts = [];
    worldReceived = false;
    showPlayer(null);
    $('connection').textContent = 'Восстанавливаем связь…';
    $('connection').classList.remove('online');
    $('message').textContent = wasLoggedIn ? 'Соединение потеряно. Войдите снова после подключения.' : 'Сервер недоступен. Повторяем подключение…';
    updateButtons();
    setTimeout(connect, 2000);
  };
  socket.onerror = () => socket.close();
}

$('login-tab').onclick = () => setMode(false);
$('register-tab').onclick = () => setMode(true);
$('auth-form').onsubmit = (event) => {
  event.preventDefault();
  const data = { username: $('username').value, password: $('password').value };
  if (registration && data.password !== $('confirm-password').value) {
    $('message').textContent = 'Пароли не совпадают.';
    return;
  }
  send(registration ? registerRequest(data) : loginRequest(data));
};
$('logout').onclick = () => send(logoutRequest());

function updateShipInfo() {
  const signature = crafts.map((craft) => `${craft.id}:${craft.ownerId}:${craft.owner}`).join('|');
  if ($('ship-list').dataset.signature !== signature) {
    $('ship-list').dataset.signature = signature;
    $('ship-list').replaceChildren(new Option('Выберите судно', ''), ...crafts.map((craft) =>
      new Option(`#${craft.id} · ${craft.owner}${craft.ownerId === player?.userId ? ' (ваше)' : ''}`, craft.id)));
  }
  if (!crafts.some((craft) => craft.id === selectedId)) selectedId = null;
  $('ship-list').value = selectedId ?? '';
  const craft = crafts.find((item) => item.id === selectedId);
  $('ship-info').textContent = craft
    ? `Судно #${craft.id}\nВладелец: ${craft.owner} (#${craft.ownerId})\nКоординаты: ${craft.x.toFixed(1)}, ${craft.y.toFixed(1)}\nСкорость: ${craft.vx.toFixed(1)}, ${craft.vy.toFixed(1)}\nМодули: ${craft.modules}\nСкрипт: ${scriptStates[craft.scriptState] ?? 'неизвестно'}`
    : 'Выберите судно на карте или в списке.';
  $('script-controls').hidden = !craft || craft.ownerId !== player?.userId;
  $('world-count').textContent = worldReceived
    ? `СУДА: ${crafts.length} · ОБНОВЛЕНИЕ 10 ГЦ`
    : 'Ожидание данных мира…';
}
function selectShip(id) {
  selectedId = id;
  $('script-output').textContent = '';
  $('script-file').value = '';
  message(''); updateShipInfo(); updateButtons(); drawField();
}
$('ship-list').onchange = () => selectShip(Number($('ship-list').value) || null);
$('script-file').onchange = updateButtons;
$('attach-script').onclick = async () => {
  const file = $('script-file').files[0];
  const id = selectedId;
  const activeSocket = socket;
  if (!file || !id || pending || scriptLoading) return;
  if (file.size === 0 || file.size > MAX_LONG_STRING_BYTES) { message('Размер скрипта: от 1 байта до 1 МиБ.'); return; }
  scriptLoading = true; updateButtons();
  try {
    const source = new TextDecoder('utf-8', { fatal: true }).decode(await file.arrayBuffer());
    if (socket === activeSocket && player && id === selectedId) send(attachScriptRequest(id, source));
  } catch (error) { message(`Не удалось загрузить скрипт: ${error.message}`); }
  finally { scriptLoading = false; updateButtons(); }
};
$('stop-script').onclick = () => { if (selectedId) send(scriptRequest(selectedId, true)); };
$('script-log').onclick = () => { if (selectedId) send(scriptRequest(selectedId)); };
$('game-canvas').onclick = (event) => {
  const rect = event.currentTarget.getBoundingClientRect();
  const x = event.clientX - rect.left, y = event.clientY - rect.top;
  const target = [...hitTargets].sort((a, b) => Math.hypot(a.x - x, a.y - y) - Math.hypot(b.x - x, b.y - y))
    .find((item) => Math.hypot(item.x - x, item.y - y) <= 18);
  selectShip(target?.id ?? null);
};

function drawField() {
  if (!player) return;
  const canvas = $('game-canvas');
  const { width, height } = canvas.getBoundingClientRect();
  if (!width || !height) return;
  const scale = window.devicePixelRatio || 1;
  canvas.width = Math.round(width * scale); canvas.height = Math.round(height * scale);
  const ctx = canvas.getContext('2d'); ctx.scale(scale, scale);
  ctx.strokeStyle = '#22353b66'; ctx.lineWidth = 1; ctx.beginPath();
  for (let x = width / 2 % 64; x < width; x += 64) { ctx.moveTo(x, 0); ctx.lineTo(x, height); }
  for (let y = height / 2 % 64; y < height; y += 64) { ctx.moveTo(0, y); ctx.lineTo(width, y); }
  ctx.stroke();
  for (let i = 0; i < 80; i++) {
    ctx.fillStyle = i % 3 ? '#91aeae60' : '#c8e69690';
    ctx.fillRect((i * 137.53 + 41) % width, (i * 73.19 + 11) % height, 1.5, 1.5);
  }
  let minX = -700, maxX = 700, minY = -400, maxY = 400;
  for (const craft of crafts) {
    minX = Math.min(minX, craft.x - 100); maxX = Math.max(maxX, craft.x + 100);
    minY = Math.min(minY, craft.y - 100); maxY = Math.max(maxY, craft.y + 100);
  }
  const zoom = Math.min((width - 70) / (maxX - minX), (height - 100) / (maxY - minY));
  const cx = (minX + maxX) / 2, cy = (minY + maxY) / 2;
  hitTargets = [];
  for (const craft of crafts) {
    const x = width / 2 + (craft.x - cx) * zoom, y = height / 2 + (craft.y - cy) * zoom;
    hitTargets.push({ id: craft.id, x, y });
    ctx.save(); ctx.translate(x, y);
    if (craft.id === selectedId) {
      ctx.strokeStyle = '#edf0e8'; ctx.lineWidth = 1.5; ctx.beginPath(); ctx.arc(0, 0, 17, 0, Math.PI * 2); ctx.stroke();
    }
    ctx.fillStyle = craft.ownerId === player.userId ? '#c8e696' : '#83c8ec';
    ctx.beginPath(); ctx.moveTo(0, -11); ctx.lineTo(8, 9); ctx.lineTo(0, 5); ctx.lineTo(-8, 9); ctx.closePath(); ctx.fill();
    ctx.font = '11px sans-serif'; ctx.textAlign = 'center'; ctx.fillText(`#${craft.id} ${craft.owner}`, 0, 32, 110);
    ctx.restore();
  }
}
new ResizeObserver(drawField).observe($('game-canvas'));
connect();
