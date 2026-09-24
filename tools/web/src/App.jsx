import { useEffect, useRef, useState } from "react";
import { SUOTA_SERVICE, isSigned, otaUpdate, versionOf } from "./suota.js";
import SnesPad from "./SnesPad.jsx";

// HID button n = report bit n (SNES_BUTTON_* in src/user_hid_hogpd.h)
const BUTTONS = ["A", "B", "Select", "Start", "Up", "Down", "Left", "Right", "X", "Y", "L", "R"];
const IS_SNES = /BT-SNES|Vendor: ffff Product: 0001/i;

/** Buttons of the first gamepad that looks like a BT-SNES (or any gamepad), polled every frame. */
function useGamepad() {
  const [pad, setPad] = useState(null);
  useEffect(() => {
    let raf;
    const tick = () => {
      const pads = [...navigator.getGamepads()].filter(Boolean);
      const p = pads.find((g) => IS_SNES.test(g.id)) ?? pads[0];
      setPad(p ? { id: p.id, snes: IS_SNES.test(p.id), pressed: p.buttons.map((b) => b.pressed) } : null);
      raf = requestAnimationFrame(tick);
    };
    tick();
    return () => cancelAnimationFrame(raf);
  }, []);
  return pad;
}

// Local helper (python tools/suota.py --serve): pad info from Windows + OTA over the Windows link
const HELPER = "http://127.0.0.1:8765";

function useHelper() {
  const [h, setH] = useState({ up: false, pads: [] });
  useEffect(() => {
    let alive = true;
    const poll = async () => {
      try {
        const r = await fetch(`${HELPER}/api/pads`);
        if (alive) setH({ up: r.ok, pads: r.ok ? await r.json() : [] });
      } catch {
        if (alive) setH({ up: false, pads: [] });
      }
    };
    poll();
    const t = setInterval(poll, 4000);
    return () => { alive = false; clearInterval(t); };
  }, []);
  return h;
}

function Chip({ state, label, value }) {
  return (
    <div className={`chip ${state}`}>
      <span className="dot" aria-hidden="true" />
      <span className="chip-label">{label}</span>
      <span className="chip-value">{value}</span>
    </div>
  );
}

function StatusBar({ pad, info, battery, hpad }) {
  const fw = info?.firmware ?? hpad?.version;
  const bat = battery ?? hpad?.battery;
  const live = !!info || !!hpad?.connected;                 // helper values of a sleeping pad are "last known"
  return (
    <div className="status" role="status" aria-live="polite">
      <Chip state={pad ? "good" : "idle"} label="Gamepad" value={pad ? "Connected" : "Not detected"} />
      <Chip state={fw && live ? "good" : "idle"} label="Firmware" value={fw ?? "—"} />
      <Chip state={bat == null || !live ? "idle" : bat < 20 ? "warn" : "good"} label="Battery"
            value={bat == null ? "—" : `${bat} %`} />
    </div>
  );
}

function Controller({ pad }) {
  const on = (n) => !!pad?.pressed[BUTTONS.indexOf(n)];
  const held = BUTTONS.filter(on);
  return (
    <section aria-labelledby="h-ctrl">
      <h2 id="h-ctrl">Controller test</h2>
      <div className={`snes ${pad ? "" : "waiting"}`} role="img"
           aria-label={pad ? `Buttons held: ${held.join(", ") || "none"}` : "No gamepad detected"}>
        <SnesPad on={on} waiting={!pad} />
        {!pad && <div className="overlay">Press any button on the pad</div>}
      </div>
      <p className="held" aria-hidden="true">
        {pad ? <>Held: <b>{held.join(" + ") || "nothing"}</b><span className="muted"> · {pad.snes ? "BT-SNES pad" : pad.id}</span></>
             : <span className="muted">Pair the pad in Windows, then press a button with this tab in front.</span>}
      </p>
    </section>
  );
}

const DIS = { manufacturer: 0x2a29, model: 0x2a24, firmware: 0x2a26, pnp: 0x2a50 };
const hex4 = (v) => "0x" + v.toString(16).padStart(4, "0");

function HelperPads({ pads }) {
  if (!pads.length)
    return <p className="note">No BT-SNES pad is paired with this PC. Pair it in Windows Bluetooth settings.</p>;
  return pads.map((p) => (
    <dl key={p.address}>
      <dt>Name</dt><dd>{p.name} <span className="muted mono">{p.address}</span></dd>
      <dt>Status</dt><dd>{p.connected ? <span className="ok">Connected</span>
                                       : <span className="muted">Not connected (asleep or off) - values below are the last known</span>}</dd>
      <dt>Firmware</dt><dd><b>{p.version ?? "—"}</b>{p.version && !p.version_live &&
        <span className="muted"> (as of pairing - Windows doesn't refresh it; the live value appears once the pad is connected)</span>}</dd>
      <dt>Battery</dt><dd>{p.battery == null ? "—" : `${p.battery} %`}</dd>
    </dl>
  ));
}

function Device({ helper, info, setInfo, setBattery, setServer }) {
  const [err, setErr] = useState("");
  const [hint, setHint] = useState(false);
  const [busy, setBusy] = useState(false);
  const dev = useRef(null);

  async function connect() {
    setErr(""); setHint(false); setBusy(true);
    try {
      const d = await navigator.bluetooth.requestDevice({
        filters: [{ namePrefix: "BT-SNES" }],
        optionalServices: [0x180a, 0x180f, SUOTA_SERVICE],
      });
      dev.current = d;
      d.addEventListener("gattserverdisconnected", () => { setInfo(null); setBattery(null); setServer(null); });
      const server = await d.gatt.connect();
      const out = { name: d.name };
      try {
        const dis = await server.getPrimaryService(0x180a);
        for (const [k, u] of Object.entries(DIS)) {
          const v = await (await dis.getCharacteristic(u)).readValue().catch(() => null);
          if (!v) continue;
          out[k] = k === "pnp"
            ? `VID ${hex4(v.getUint16(1, true))} · PID ${hex4(v.getUint16(3, true))} · rev ${hex4(v.getUint16(5, true))}`
            : new TextDecoder().decode(v);
        }
      } catch { out.firmware = "older than 0.3.0"; }
      setInfo(out);
      try {
        const bl = await (await server.getPrimaryService(0x180f)).getCharacteristic(0x2a19);
        setBattery((await bl.readValue()).getUint8(0));
        bl.addEventListener("characteristicvaluechanged", (e) => setBattery(e.target.value.getUint8(0)));
        await bl.startNotifications();
      } catch { /* battery is optional */ }
      setServer(server);
    } catch (e) {
      if (e.name === "NotFoundError") setHint(true);      // chooser cancelled or empty
      else setErr(e.message);
    }
    setBusy(false);
  }

  return (
    <section aria-labelledby="h-dev">
      <h2 id="h-dev">Device info</h2>
      {helper.up ? (
        <>
          <HelperPads pads={helper.pads} />
          <p className="muted small">From Windows, via the local helper. Refreshes every few seconds.</p>
        </>
      ) : !navigator.bluetooth ? (
        <p className="note warn">Web Bluetooth isn't available here. Open this page in Chrome or Edge at http://localhost.</p>
      ) : info ? (
        <>
          <dl>
            <dt>Name</dt><dd>{info.name}</dd>
            <dt>Firmware</dt><dd><b>{info.firmware ?? "—"}</b></dd>
            <dt>Manufacturer</dt><dd>{info.manufacturer ?? "—"}</dd>
            <dt>Model</dt><dd>{info.model ?? "—"}</dd>
            <dt>PnP ID</dt><dd className="mono">{info.pnp ?? "—"}</dd>
          </dl>
          <button onClick={() => dev.current?.gatt.disconnect()}>Disconnect</button>
        </>
      ) : (
        <button className="primary" onClick={connect} disabled={busy}>{busy ? "Connecting…" : "Read version & battery"}</button>
      )}
      {!helper.up && (
        <p className="muted small">
          Tip: run <code>python tools/suota.py --serve</code> (or <code>suota.exe --serve</code>) and this fills in
          by itself for the paired pad, no Bluetooth dialog needed.
        </p>
      )}
      {hint && (
        <p className="note">
          Pad not in the list? Chrome only shows pads that are advertising, and a pad connected to Windows
          as a gamepad isn't. Remove it in Windows Bluetooth settings, press B to wake it, and try again.
        </p>
      )}
      {err && <p className="note error" role="alert">{err}</p>}
    </section>
  );
}

/** OTA through the helper; polls its progress. */
async function helperUpdate(address, body, log, progress) {
  const r = await fetch(`${HELPER}/api/update?address=${encodeURIComponent(address)}`, {
    method: "POST", headers: { "Content-Type": "application/octet-stream" }, body,
  });
  if (!r.ok) throw new Error((await r.json()).error);
  let seen = 0;
  for (;;) {
    await new Promise((res) => setTimeout(res, 400));
    const s = await (await fetch(`${HELPER}/api/update`)).json();
    s.log.slice(seen).forEach(log);
    seen = s.log.length;
    progress(s.progress);
    if (!s.busy) {
      if (s.error) throw new Error(s.error);
      return;
    }
  }
}

function Firmware({ server, helper, hpad }) {
  const [file, setFile] = useState(null);
  const [progress, setProgress] = useState(null);
  const [busy, setBusy] = useState(false);
  const [log, setLog] = useState([]);
  const [result, setResult] = useState(null);
  const add = (m) => setLog((l) => [...l, m]);

  async function pick(e) {
    const f = e.target.files[0];
    if (!f) return;
    const body = new Uint8Array(await f.arrayBuffer());
    setFile({ name: f.name, body, version: versionOf(body), signed: isSigned(body) });
    setProgress(null); setResult(null); setLog([]);
  }

  async function update() {
    setBusy(true); setLog([]); setProgress(0); setResult(null);
    try {
      if (via === "helper") await helperUpdate(hpad.address, file.body, add, setProgress);
      else await otaUpdate(server, file.body, add, setProgress);
      setResult({ ok: true, text: "Update installed. The pad is rebooting into the new firmware." });
    } catch (e) {
      setResult({ ok: false, text: e.message });
    }
    setBusy(false);
  }

  const via = helper.up && hpad ? "helper" : server ? "webbt" : null;
  const blocker = !via ? (helper.up ? "Pair a BT-SNES pad with this PC first." : "Run the helper, or read the device info first (it connects the pad).")
    : !file ? "Choose a signed firmware file."
    : !file.signed ? "This file isn't signed, so the pad would refuse it." : null;

  return (
    <details className="advanced">
      <summary><h2>Firmware update</h2><span className="muted">advanced</span></summary>
      <p className="muted">
        Power the pad on holding <b>Start + Select</b>{helper.up ? "" : ", read the device info above"}, then send a file made with{" "}
        <code>python tools/suota.py --sign BLE_HID_585.bin</code>.
      </p>
      <div className="row">
        <label className="filepick">
          <input type="file" accept=".bin" onChange={pick} disabled={busy} />
          <span>Choose firmware…</span>
        </label>
        {file && (
          <span className="file">
            {file.name} · v{file.version} ·{" "}
            {file.signed ? <span className="ok">signed</span> : <span className="bad">not signed</span>}
          </span>
        )}
      </div>
      <div className="row">
        <button className="danger" onClick={update} disabled={!!blocker || busy}>
          {busy ? `Updating… ${Math.round((progress ?? 0) * 100)} %` : "Install update"}
        </button>
        {blocker && !busy && <span className="muted">{blocker}</span>}
        {!blocker && !busy && <span className="muted">to {via === "helper" ? `${hpad.name} (via Windows)` : "the connected pad"}</span>}
      </div>
      {progress != null && (
        <progress value={progress} max={1} aria-label="Update progress">{Math.round(progress * 100)} %</progress>
      )}
      {result && <p className={`note ${result.ok ? "success" : "error"}`} role="alert">{result.text}</p>}
      {log.length > 0 && <pre aria-label="Update log">{log.join("\n")}</pre>}
    </details>
  );
}

export default function App() {
  const pad = useGamepad();
  const helper = useHelper();
  const hpad = helper.pads.find((p) => p.connected) ?? helper.pads[0];
  const [server, setServer] = useState(null);
  const [info, setInfo] = useState(null);
  const [battery, setBattery] = useState(null);
  return (
    <main>
      <header>
        <h1>BT-SNES Tester</h1>
        <StatusBar pad={pad} info={info} battery={battery} hpad={hpad} />
      </header>
      <Controller pad={pad} />
      <Device helper={helper} info={info} setInfo={setInfo} setBattery={setBattery} setServer={setServer} />
      <Firmware server={server} helper={helper} hpad={hpad} />
    </main>
  );
}
