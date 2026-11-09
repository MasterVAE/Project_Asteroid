// Native browser client for the existing Boost.Beast WebSocket protocol.
export class AsteroidAPI extends EventTarget {
  constructor() {
    super();
    this.socket = null;
    this.queue = [];
    this.active = null;
  }

  get connected() { return this.socket?.readyState === WebSocket.OPEN; }

  async connect(address) {
    const url = new URL(address);
    if (!['ws:', 'wss:'].includes(url.protocol)) throw new Error('Нужен адрес ws:// или wss://.');
    if (location.protocol === 'https:' && url.protocol !== 'wss:') {
      throw new Error('Страница HTTPS требует wss://. Настройте TLS-прокси или откройте клиент по HTTP.');
    }
    this.disconnect();
    const socket = new WebSocket(url.href);
    this.socket = socket;
    return new Promise((resolve, reject) => {
      this.rejectConnection = reject;
      this.connectionTimer = setTimeout(() => {
        reject(new Error('Сервер не ответил за 8 секунд. Проверьте адрес и порт.'));
        socket.close();
      }, 8000);
      socket.onopen = () => {
        if (this.socket !== socket) return;
        clearTimeout(this.connectionTimer);
        this.rejectConnection = null;
        resolve();
      };
      socket.onmessage = event => {
        if (this.socket !== socket || typeof event.data !== 'string') return;
        // The server sends a plain-text greeting before JSON responses.
        if (event.data.startsWith('Your ID:')) return;
        let data;
        try { data = JSON.parse(event.data); } catch { return; }
        const request = this.active;
        if (!request || !data || typeof data !== 'object') return;
        const matches = request.body.request_type === 'get_map'
          ? Array.isArray(data.entities)
          : typeof data.type === 'string' || Object.keys(data).length === 0;
        if (!matches) return;
        clearTimeout(request.timer);
        this.active = null;
        request.resolve(data);
        this.pump();
      };
      socket.onerror = () => {
        reject(new Error('Не удалось подключиться. Проверьте адрес, доступность порта и TLS.'));
      };
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

  getMap() { return this.request({ type: 'request', request_type: 'get_map' }); }

  getEntity(id) {
    if (!Number.isSafeInteger(id) || id < 0) return Promise.reject(new Error('Некорректный ID объекта.'));
    return this.request({ type: 'request', request_type: 'get_entity', entity_id: id });
  }

  request(body) {
    if (!this.connected) return Promise.reject(new Error('Нет подключения к серверу.'));
    return new Promise((resolve, reject) => {
      this.queue.push({ body, resolve, reject });
      this.pump();
    });
  }

  pump() {
    if (this.active || !this.connected || !this.queue.length) return;
    const request = this.active = this.queue.shift();
    // Responses have no request IDs, so requests must be serialized.
    request.timer = setTimeout(() => {
      this.failRequests(new Error('Сервер не ответил на запрос за 5 секунд.'));
      this.socket?.close();
    }, 5000);
    try { this.socket.send(JSON.stringify(request.body)); }
    catch (error) {
      this.failRequests(error);
      this.socket?.close();
    }
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
