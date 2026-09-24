// SNES / Super Famicom style pad, drawn in SVG (no logos). on(name) -> pressed.
// Pressed keys sink: shadow off, darker, slightly smaller, plus a ring.

const C = { x: 340, y: 360 };                       // D-pad centre
const FACE = { x: 1110, y: 350 };                   // face-plate centre
const BTN = {                                       // face buttons (SNES colours)
  X: { x: 1100, y: 248, fill: "#2451c6", label: [1180, 172] },
  A: { x: 1232, y: 352, fill: "#d72b2b", label: [1318, 292] },
  B: { x: 1112, y: 452, fill: "#f2c511", label: [1026, 528] },
  Y: { x: 980, y: 348, fill: "#23a043", label: [896, 420] },
};

function Arm({ name, on, x, y, w, h, arrow }) {
  return (
    <g className={`dp ${on(name) ? "on" : ""}`}>
      <rect x={x} y={y} width={w} height={h} rx="10" />
      <path className="arrow" d={arrow} />
    </g>
  );
}

export default function SnesPad({ on, waiting }) {
  const a = 40, l = 115;                            // D-pad half arm width / arm length
  return (
    <svg className={`pad ${waiting ? "waiting" : ""}`} viewBox="0 0 1456 720" aria-hidden="true">
      <defs>
        <linearGradient id="shell" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0" stopColor="#f1e7c6" /><stop offset="1" stopColor="#e2d5a9" />
        </linearGradient>
        <radialGradient id="recess" cx="0.5" cy="0.45" r="0.6">
          <stop offset="0.75" stopColor="#e8dcb2" /><stop offset="1" stopColor="#d6c797" />
        </radialGradient>
        <filter id="lift" x="-30%" y="-30%" width="160%" height="160%">
          <feDropShadow dx="0" dy="6" stdDeviation="5" floodOpacity="0.35" />
        </filter>
      </defs>

      {/* shoulder buttons, peeking out behind the shell */}
      <g className={`sh ${on("L") ? "on" : ""}`}><rect x="170" y="30" width="290" height="80" rx="30" /><text x="315" y="70">L</text></g>
      <g className={`sh ${on("R") ? "on" : ""}`}><rect x="996" y="30" width="290" height="80" rx="30" /><text x="1141" y="70">R</text></g>

      <rect x="70" y="80" width="1316" height="570" rx="285" fill="url(#shell)" stroke="#c9ba8a" strokeWidth="4" />
      <text className="brand" x="520" y="190">BT-SNES</text>

      {/* D-pad in its round recess */}
      <circle cx={C.x} cy={C.y} r="160" fill="url(#recess)" stroke="#cdbd8c" strokeWidth="3" />
      <g filter="url(#lift)">
        <path className="cross" d={`M${C.x - a} ${C.y - l}h${2 * a}v${l - a}h${l - a}v${2 * a}h${a - l}v${l - a}h${-2 * a}v${a - l}h${a - l}v${-2 * a}h${l - a}z`} />
      </g>
      <Arm name="Up" on={on} x={C.x - a} y={C.y - l} w={2 * a} h={l - a} arrow={`M${C.x} ${C.y - l + 18}l-18 30h36z`} />
      <Arm name="Down" on={on} x={C.x - a} y={C.y + a} w={2 * a} h={l - a} arrow={`M${C.x} ${C.y + l - 18}l-18 -30h36z`} />
      <Arm name="Left" on={on} x={C.x - l} y={C.y - a} w={l - a} h={2 * a} arrow={`M${C.x - l + 18} ${C.y}l30 -18v36z`} />
      <Arm name="Right" on={on} x={C.x + a} y={C.y - a} w={l - a} h={2 * a} arrow={`M${C.x + l - 18} ${C.y}l-30 -18v36z`} />
      <circle cx={C.x} cy={C.y} r="20" fill="#3a3a3d" />

      {/* Select / Start */}
      {[["Select", 600, "SELECT"], ["Start", 750, "START"]].map(([n, x, t]) => (
        <g key={n}>
          <g transform={`rotate(-35 ${x} 390)`}>{/* rotation outside: CSS transform on .on would replace it */}
            <g className={`pill ${on(n) ? "on" : ""}`}><rect x={x - 50} y="372" width="100" height="36" rx="18" /></g>
          </g>
          <text className="print" x={x} y="478">{t}</text>
        </g>
      ))}

      {/* face plate with the two diagonal tracks */}
      <circle cx={FACE.x} cy={FACE.y} r="262" fill="#7d7d82" stroke="#6a6a6f" strokeWidth="3" />
      <rect x="930" y="248" width="230" height="104" rx="52" fill="#e3d6ab" transform="rotate(-38 1040 300)" />
      <rect x="1052" y="350" width="230" height="104" rx="52" fill="#e3d6ab" transform="rotate(-38 1172 402)" />
      {Object.entries(BTN).map(([n, b]) => (
        <g key={n}>
          <g className={`fb ${on(n) ? "on" : ""}`}>
            <circle cx={b.x} cy={b.y} r="54" fill={b.fill} />
            <ellipse className="shine" cx={b.x - 14} cy={b.y - 18} rx="20" ry="11" />
          </g>
          <text className="plate" x={b.label[0]} y={b.label[1]}>{n}</text>
        </g>
      ))}
    </svg>
  );
}
