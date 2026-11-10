// Match backend/include/protocol.hpp. One WebSocket message = one packet.
// Integers: little-endian. string uses a u16 byte length, long_string a u32.
export const MAX_LONG_STRING_BYTES = 1024 * 1024;
export const MAX_MESSAGE_BYTES = MAX_LONG_STRING_BYTES + 4096;
export const Request = Object.freeze({ Login: 1, Register: 2, Logout: 3, AttachScript: 4, StopScript: 5, InspectScript: 6 });
export const Response = Object.freeze({ Auth: 128, Logout: 129, World: 130, Script: 131, Error: 255 });
export const Status = Object.freeze({ Ok: 0, InvalidInput: 1, NameTaken: 2, WrongCredentials: 3, ServerError: 4 });

const encoder = new TextEncoder();
const decoder = new TextDecoder('utf-8', { fatal: true });

export class Writer {
  constructor() { this.bytes = []; }
  u8(value) { this.bytes.push(value & 255); }
  u16(value) { this.u8(value); this.u8(value >>> 8); }
  u32(value) { for (let shift = 0; shift < 32; shift += 8) this.u8(value >>> shift); }
  string(value) {
    const bytes = encoder.encode(value);
    if (bytes.length > 65535) throw new Error('Строка слишком длинная.');
    this.u16(bytes.length);
    for (const byte of bytes) this.bytes.push(byte);
  }
  longString(value) {
    const bytes = encoder.encode(value);
    if (bytes.length > MAX_LONG_STRING_BYTES) throw new Error('Строка превышает 1 МиБ.');
    this.u32(bytes.length);
    for (const byte of bytes) this.bytes.push(byte);
  }
  finish() { return new Uint8Array(this.bytes); }
}

export class Reader {
  constructor(buffer) { this.view = new DataView(buffer); this.offset = 0; }
  u8() { return this.view.getUint8(this.offset++); }
  u16() { const value = this.view.getUint16(this.offset, true); this.offset += 2; return value; }
  u32() { const value = this.view.getUint32(this.offset, true); this.offset += 4; return value; }
  f32() { const value = this.view.getFloat32(this.offset, true); this.offset += 4; return value; }
  string() {
    return this.readString(this.u16());
  }
  longString() {
    const size = this.u32();
    if (size > MAX_LONG_STRING_BYTES) throw new Error('Строка превышает 1 МиБ.');
    return this.readString(size);
  }
  readString(size) {
    if (size > this.view.byteLength - this.offset) throw new Error('Неполный пакет.');
    const bytes = new Uint8Array(this.view.buffer, this.offset, size);
    this.offset += size;
    return decoder.decode(bytes);
  }
  finish() {
    if (this.offset !== this.view.byteLength) throw new Error('Лишние данные в пакете.');
  }
}

// Requests: [type:u8][username:string][password:string].
function credentials(type, { username, password }) {
  const packet = new Writer();
  packet.u8(type);
  packet.string(username);
  packet.string(password);
  return packet.finish();
}
export function loginRequest(data) { return credentials(Request.Login, data); }
export function registerRequest(data) { return credentials(Request.Register, data); }
export function logoutRequest() { return new Uint8Array([Request.Logout]); }

export function attachScriptRequest(craftId, source) {
  const packet = new Writer();
  packet.u8(Request.AttachScript); packet.u32(craftId); packet.longString(source);
  return packet.finish();
}
export function scriptRequest(craftId, stop = false) {
  const packet = new Writer();
  packet.u8(stop ? Request.StopScript : Request.InspectScript); packet.u32(craftId);
  return packet.finish();
}

// Edit response fields here and in the C++ encoder together.
export function decodeResponse(buffer) {
  const packet = new Reader(buffer);
  const type = packet.u8();
  let response;
  switch (type) {
    case Response.Auth:
      response = { type, status: packet.u8(), userId: packet.u32(), username: packet.string(), message: packet.string() };
      break;
    case Response.World: {
      const count = packet.u32();
      if (count > Math.floor((buffer.byteLength - 5) / 31)) throw new Error('Неверное число судов.');
      const crafts = [];
      for (let i = 0; i < count; i++) {
        const craft = { id: packet.u32(), ownerId: packet.u32(), owner: packet.string(),
          x: packet.f32(), y: packet.f32(), vx: packet.f32(), vy: packet.f32(),
          modules: packet.u32(), scriptState: packet.u8() };
        if (![craft.x, craft.y, craft.vx, craft.vy].every(Number.isFinite)) throw new Error('Неверные координаты.');
        crafts.push(craft);
      }
      response = { type, crafts };
      break;
    }
    case Response.Script:
      response = { type, craftId: packet.u32(), state: packet.u8(), log: packet.string() };
      break;
    case Response.Logout:
      response = { type };
      break;
    case Response.Error:
      response = { type, message: packet.string() };
      break;
    default: throw new Error('Неизвестный ответ сервера.');
  }
  packet.finish();
  return response;
}
