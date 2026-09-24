// npm test - checks the SUOTA image builder (must match tools/suota.py build_payload)
import { test } from "node:test";
import assert from "node:assert/strict";
import { buildPayload, crc32, isSigned, versionOf } from "./suota.js";

const enc = new TextEncoder();

test("crc32 check value", () => assert.equal(crc32(enc.encode("123456789")), 0xcbf43926));

test("payload header, version, XOR", () => {
  const body = new Uint8Array([...new Uint8Array(100), ...enc.encode("BLE_HID_VERSION=1.2.3\0"), ...new Uint8Array(50).fill(0x5a)]);
  const p = buildPayload(body, 1234);
  const dv = new DataView(p.buffer);
  assert.equal(p.length, 64 + body.length + 1);
  assert.deepEqual([...p.subarray(0, 4)], [0x70, 0x51, 0xff, 0]);
  assert.equal(dv.getUint32(4, true), body.length);
  assert.equal(dv.getUint32(8, true), crc32(body));
  assert.equal(new TextDecoder().decode(p.subarray(12, 17)), "1.2.3");
  assert.equal(dv.getUint32(28, true), 1234);
  assert.equal(versionOf(body), "1.2.3");
  assert.equal(p.reduce((x, b) => x ^ b, 0), 0);
});

test("signed detection", () => {
  const fw = new Uint8Array(200);
  assert.equal(isSigned(fw), false);
  assert.equal(isSigned(new Uint8Array([...fw, ...enc.encode("BTSNSIG1"), ...new Uint8Array(64)])), true);
});
