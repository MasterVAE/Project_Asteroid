// Match backend/include/protocol.hpp. One WebSocket message = one packet.
// Integers: little-endian. String: [u16 byte length][UTF-8 bytes].
export const Request = Object.freeze({ Login: 1, Register: 2, Logout: 3 });
export const Response = Object.freeze({ Auth: 128, Logout: 129, Error: 255 });
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
    this.bytes.push(...bytes);
  }
  finish() { return new Uint8Array(this.bytes); }
}

export class Reader {
  constructor(buffer) { this.view = new DataView(buffer); this.offset = 0; }
  u8() { return this.view.getUint8(this.offset++); }
  u16() { const value = this.view.getUint16(this.offset, true); this.offset += 2; return value; }
  u32() { const value = this.view.getUint32(this.offset, true); this.offset += 4; return value; }
  string() {
    const size = this.u16();
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

// Edit response fields here and in the C++ encoder together.
export function decodeResponse(buffer) {
  const packet = new Reader(buffer);
  const type = packet.u8();
  let response;
  switch (type) {
    case Response.Auth:
      response = { type, status: packet.u8(), userId: packet.u32(), username: packet.string(), message: packet.string() };
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
