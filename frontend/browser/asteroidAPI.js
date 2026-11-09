export class APIError extends Error {
  constructor(error) {
    super(error.message || 'Ошибка сервера.');
    this.code = error.code;
    this.status = error.status;
  }
}

// Same commands, results and errors as frontend/python/asteroidAPI.py.
export class AsteroidAPI extends EventTarget {
  constructor() {
    super();
    this.socket = null;
    this.queue = [];
    this.active = null;
    this.endpoint = new URL('/api', location.href);
  }

  get connected() { return this.socket?.readyState === WebSocket.OPEN; }

  async httpRequest(requestType, fields = {}) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 15000);
    try {
      const response = await fetch(this.endpoint, {
        method: 'POST', credentials: 'same-origin', cache: 'no-store',
        headers: { 'Content-Type': 'application/json', 'X-Asteroid-Client': '1' },
        body: JSON.stringify({ ...fields, type: 'request', request_type: requestType }),
        signal: controller.signal
      });
      const body = await response.json();
      if (!response.ok || !body.ok) throw new APIError(body.error || {status: response.status});
      return body.result;
    } finally { clearTimeout(timer); }
  }

  async authenticate(command, username, password) {
    const profile = await this.httpRequest(command, {username, password});
    this.disconnect();
    return profile;
  }
  register(username, password) { return this.authenticate('register', username, password); }
  login(username, password) { return this.authenticate('login', username, password); }
  me() { return this.httpRequest('me'); }
  async logout() {
    const result = await this.httpRequest('logout');
    this.disconnect();
    return result;
  }
  createApiKey(name, password, expiresDays = 30) {
    return this.httpRequest('create_api_key', {name, password, expires_days: expiresDays});
  }
  listApiKeys() { return this.httpRequest('list_api_keys'); }
  revokeApiKey(keyId) { return this.httpRequest('revoke_api_key', {key_id: keyId}); }

  async connect(address = null) {
    const url = address ? new URL(address) : new URL('/ws', location.href);
    if (!address) url.protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
    if (!['ws:', 'wss:'].includes(url.protocol)) throw new Error('Нужен адрес ws:// или wss://.');
    const httpOrigin = new URL(url);
    httpOrigin.protocol = url.protocol === 'wss:' ? 'https:' : 'http:';
    if (httpOrigin.origin !== location.origin || url.search || url.hash || url.username || url.password)
      throw new Error('Откройте сайт игрового сервера для входа и подключения.');
    this.disconnect();
    const socket = new WebSocket(url.href);
    this.socket = socket;
    return new Promise((resolve, reject) => {
      this.rejectConnection = reject;
      this.connectionTimer = setTimeout(() => {
        reject(new Error('Сервер не ответил за 8 секунд.'));
        socket.close();
      }, 8000);
      socket.onopen = () => {
        if (this.socket !== socket) return;
        clearTimeout(this.connectionTimer);
        this.rejectConnection = null;
        resolve();
      };
      socket.onmessage = event => {
        if (this.socket !== socket || typeof event.data !== 'string' || event.data.startsWith('Your ID:')) return;
        let body;
        try { body = JSON.parse(event.data); } catch { return; }
        if (!this.active || typeof body?.ok !== 'boolean') return;
        const request = this.active;
        this.active = null;
        clearTimeout(request.timer);
        if (body.ok) request.resolve(body.result);
        else request.reject(new APIError(body.error || {}));
        this.pump();
      };
      socket.onerror = () => reject(new Error('Не удалось подключиться к игровому серверу.'));
      socket.onclose = () => {
        if (this.socket !== socket) return;
        clearTimeout(this.connectionTimer);
        this.socket = null;
        this.rejectConnection = null;
        const error = new Error('Соединение с сервером закрыто.');
        reject(error);
        this.failRequests(error);
        this.dispatchEvent(new Event('close'));
      };
    });
  }

  disconnect() {
    clearTimeout(this.connectionTimer);
    const socket = this.socket;
    this.socket = null;
    this.rejectConnection?.(new Error('Подключение отменено.'));
    this.rejectConnection = null;
    this.failRequests(new Error('Нет подключения к серверу.'));
    socket?.close();
  }

  getMap() { return this.request('get_map'); }
  getMyEntities() { return this.request('get_my_entities'); }
  createCraft() { return this.request('create_craft'); }
  flyTo(entityId, x, y) { return this.request('fly_to', {entity_id: entityId, x, y}); }
  mine(craftId, asteroidId) { return this.request('mine', {craft_id: craftId, asteroid_id: asteroidId}); }
  getEntity(id) {
    if (!Number.isSafeInteger(id) || id < 0) return Promise.reject(new Error('Некорректный ID объекта.'));
    return this.request('get_entity', {entity_id: id});
  }

  request(requestType, fields = {}) {
    if (!this.connected) return this.httpRequest(requestType, fields);
    return new Promise((resolve, reject) => {
      this.queue.push({body: {...fields, type: 'request', request_type: requestType}, resolve, reject});
      this.pump();
    });
  }

  pump() {
    if (this.active || !this.connected || !this.queue.length) return;
    const request = this.active = this.queue.shift();
    request.timer = setTimeout(() => {
      this.failRequests(new Error('Сервер не ответил на запрос за 5 секунд.'));
      this.socket?.close();
    }, 5000);
    try { this.socket.send(JSON.stringify(request.body)); }
    catch (error) { this.failRequests(error); this.socket?.close(); }
  }

  failRequests(error) {
    if (this.active) {
      clearTimeout(this.active.timer);
      this.active.reject(error);
      this.active = null;
    }
    this.queue.splice(0).forEach(request => request.reject(error));
  }
}
