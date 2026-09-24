// SUOTA over Web Bluetooth - same protocol and image format as tools/suota.py.
// Constants come from tools/suota_protocol.json, shared with suota.py (single source).
import P from "../../suota_protocol.json" with { type: "json" };

export const SUOTA_SERVICE = P.service;
export const HELPER_PORT = P.helper_port;
const { mem_dev: MEM_DEV, gpio_map: GPIO_MAP, patch_len: PATCH_LEN, patch_data: PATCH_DATA, status: STATUS } = P.chars;
const [IMG_SPI_FLASH, IMG_END, REBOOT] = ["img_spi_flash", "img_end", "reboot"].map((k) => parseInt(P.commands[k], 16));
const SPI_GPIO_MAP = parseInt(P.spi_gpio_map, 16);
const CHUNK = 20;                   // Web Bluetooth doesn't expose the MTU; 20 always fits
const BLOCK = Math.floor(P.max_block / CHUNK) * CHUNK;
const ST_CMP_OK = P.status_ok, ST_IMG_STARTED = P.status_img_started;
const STATUS_TEXT = P.status_text;  // keys are decimal status codes

const enc = new TextEncoder();
const VERSION_TAG = enc.encode(P.version_tag);
const SIG_MAGIC = enc.encode(P.sig_magic);
const SIG_TRAILER = SIG_MAGIC.length + 64;    // magic + ECDSA r || s

function indexOf(hay, needle, from = 0) {
  outer: for (let i = from; i <= hay.length - needle.length; i++) {
    for (let j = 0; j < needle.length; j++) if (hay[i + j] !== needle[j]) continue outer;
    return i;
  }
  return -1;
}

const CRC_TABLE = Array.from({ length: 256 }, (_, n) => {
  let c = n;
  for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  return c >>> 0;
});
export function crc32(buf) {
  let c = 0xffffffff;
  for (const b of buf) c = CRC_TABLE[(c ^ b) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

/** Firmware version from the "BLE_HID_VERSION=x.y.z" string the firmware carries. */
export function versionOf(body) {
  const i = indexOf(body, VERSION_TAG);
  if (i < 0) return "unknown";
  const s = i + VERSION_TAG.length;
  return new TextDecoder().decode(body.subarray(s, body.indexOf(0, s))).slice(0, 15);
}

export function isSigned(body) {
  return body.length > SIG_TRAILER && indexOf(body.subarray(body.length - SIG_TRAILER, body.length - 64), SIG_MAGIC) === 0;
}

/** SUOTA payload: 64-byte header + body + XOR byte (the pad checks crc_calc == 0). */
export function buildPayload(body, timestamp = Math.floor(Date.now() / 1000)) {
  const out = new Uint8Array(64 + body.length + 1);
  const dv = new DataView(out.buffer);
  out.set([0x70, 0x51, 0xff, 0x00]);                      // signature, validflag, imageid
  dv.setUint32(4, body.length, true);
  dv.setUint32(8, crc32(body), true);
  out.set(enc.encode(versionOf(body)), 12);               // version[16], zero padded
  dv.setUint32(28, timestamp, true);                      // encryption + reserved stay 0
  out.set(body, 64);
  let x = 0;
  for (let i = 0; i < out.length - 1; i++) x ^= out[i];
  out[out.length - 1] = x;
  return out;
}

const u32 = (v) => { const b = new Uint8Array(4); new DataView(b.buffer).setUint32(0, v >>> 0, true); return b; };
const u16 = (v) => { const b = new Uint8Array(2); new DataView(b.buffer).setUint16(0, v, true); return b; };

/** Send a signed firmware body to a connected pad. log(msg), progress(0..1). */
export async function otaUpdate(server, body, log, progress) {
  if (!isSigned(body)) throw new Error("file is not signed - run: python tools/suota.py --sign FILE.bin");
  const payload = buildPayload(body);
  let svc;
  try {
    svc = await server.getPrimaryService(SUOTA_SERVICE);
  } catch {
    throw new Error("no OTA service: the pad needs firmware v0.4.0+ (re-pair it in Windows after updating over J-Link)");
  }
  const ch = {};
  for (const [k, u] of Object.entries({ MEM_DEV, GPIO_MAP, PATCH_LEN, PATCH_DATA, STATUS }))
    ch[k] = await svc.getCharacteristic(u);

  const queue = [], waiters = [];
  ch.STATUS.addEventListener("characteristicvaluechanged", (e) => {
    const s = e.target.value.getUint8(0);
    waiters.length ? waiters.shift()(s) : queue.push(s);
  });
  const next = (timeout) => new Promise((res, rej) => {
    if (queue.length) return res(queue.shift());
    const t = setTimeout(() => rej(new Error("no answer from the pad")), timeout);
    waiters.push((s) => { clearTimeout(t); res(s); });
  });
  const expect = async (ok, what, timeout = 15000) => {
    const s = await next(timeout).catch((e) => { throw new Error(`${what}: ${e.message}`); });
    if (s !== ok) throw new Error(`${what}: ${STATUS_TEXT[s] ?? "status 0x" + s.toString(16)}`);
  };

  try {
    await ch.STATUS.startNotifications();
    await ch.MEM_DEV.writeValueWithResponse(u32(IMG_SPI_FLASH));
  } catch (e) {
    throw new Error(`pad refused the update (${e.message}). Is it in OTA mode (hold L + R + Start + Select for 3 s) and paired with this PC?`);
  }
  await expect(ST_IMG_STARTED, "start");
  await ch.GPIO_MAP.writeValueWithResponse(u32(SPI_GPIO_MAP));

  log(`sending ${payload.length} bytes`);
  let curLen = -1;
  for (let off = 0; off < payload.length; off += BLOCK) {
    const blk = payload.subarray(off, off + BLOCK);
    if (blk.length !== curLen) {
      curLen = blk.length;
      await ch.PATCH_LEN.writeValueWithResponse(u16(curLen));
    }
    for (let i = 0; i < blk.length; i += CHUNK)
      await ch.PATCH_DATA.writeValueWithoutResponse(blk.subarray(i, i + CHUNK));
    await expect(ST_CMP_OK, `block at ${off}`);
    progress((off + blk.length) / payload.length);
  }

  await ch.MEM_DEV.writeValueWithResponse(u32(IMG_END));
  await expect(ST_CMP_OK, "CRC check");
  log("checking signature on the pad ...");
  await expect(ST_CMP_OK, "signature check", 60000);
  log("signature OK - rebooting the pad into the new firmware");
  await ch.MEM_DEV.writeValueWithResponse(u32(REBOOT)).catch(() => {}); // it disconnects to reboot
}
