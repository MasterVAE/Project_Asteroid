import { Response, Status, loginRequest, registerRequest, logoutRequest, decodeResponse } from './protocol.js';

const $ = (id) => document.getElementById(id);
let socket;
let registration = false;
let pending = false;
let player = null;

function updateButtons() {
  const ready = socket?.readyState === WebSocket.OPEN && !pending;
  $('submit').disabled = !ready;
  $('logout').disabled = !ready;
  $('login-tab').disabled = pending;
  $('register-tab').disabled = pending;
  $('submit-label').textContent = pending ? 'Ожидание…' : registration ? 'Создать аккаунт' : 'Войти';
}

function showPlayer(value) {
  player = value;
  $('auth-screen').hidden = !!player;
  $('game-screen').hidden = !player;
  $('player-name').textContent = player ? player.username : '';
  $('password').value = '';
  $('confirm-password').value = '';
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
  $('message').textContent = '';
  updateButtons();
}

function connect() {
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
    pending = false;
    try {
      const response = decodeResponse(data);
      switch (response.type) {
        case Response.Auth:
          if (response.status === Status.Ok) showPlayer(response);
          else $('message').textContent = response.message;
          break;
        case Response.Logout:
          showPlayer(null);
          break;
        case Response.Error:
          $('message').textContent = response.message;
          break;
      }
    } catch (error) {
      $('message').textContent = `Ошибка протокола: ${error.message}`;
      socket.close();
    }
    updateButtons();
  };
  socket.onclose = () => {
    const wasLoggedIn = !!player;
    pending = false;
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

// Visual placeholder only. Future world rendering goes here.
function drawField() {
  if (!player) return;
  const canvas = $('game-canvas');
  const { width, height } = canvas.getBoundingClientRect();
  const scale = window.devicePixelRatio || 1;
  canvas.width = Math.round(width * scale);
  canvas.height = Math.round(height * scale);
  const ctx = canvas.getContext('2d');
  ctx.scale(scale, scale);
  ctx.strokeStyle = '#22353b66';
  ctx.lineWidth = 1;
  ctx.beginPath();
  for (let x = width / 2 % 64; x < width; x += 64) { ctx.moveTo(x, 0); ctx.lineTo(x, height); }
  for (let y = height / 2 % 64; y < height; y += 64) { ctx.moveTo(0, y); ctx.lineTo(width, y); }
  ctx.stroke();
  for (let i = 0; i < 80; i++) {
    ctx.fillStyle = i % 3 ? '#91aeae60' : '#c8e69690';
    ctx.fillRect((i * 137.53 + 41) % width, (i * 73.19 + 11) % height, 1.5, 1.5);
  }
}
new ResizeObserver(drawField).observe($('game-canvas'));
connect();
