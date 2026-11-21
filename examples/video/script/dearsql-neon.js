// DearSQL launch teaser, neon: real screenshots in glowing frames, post effects over the finished frame.
//   imshell-video dearsql-neon.js render -o dearsql-neon.mp4 --set music=/path/to/track.wav
const W = 1920, H = 1080, BG = '#06030f';
const PINK = '#ff2d95', CYAN = '#2de2ff', VIOLET = '#9b5cff', ORANGE = '#ff9a3c';
const SHOTS = args.shots || 'shots'; // a folder with results-chart.png, review-indexes.png and tui-sql.png (DearSQL docs shots)

function backdrop(p) {
  layer('back');
  gradientRect(0, 0, W, H, '#0a0420', '#1a0830', 'v');
  // slow grid wash so the empty areas are not flat
  const s = (p * 0.4) % 80;
  for (let x = -80; x < W + 80; x += 80) line(x + s, 0, x + s, H, 'rgba(155,92,255,0.07)', 1.5);
  for (let y = 0; y < H; y += 80) line(0, y, W, y, 'rgba(45,226,255,0.06)', 1.5);
}

function centered(s, y, size, color) { const [w] = measure(s, size); text(s, (W - w) / 2, y, size, color); }

function neonFrame(x, y, w, h, color, a, r = 22) {
  strokeRect(x - 10, y - 10, w + 20, h + 20, rgba(...color, 0.18 * a), 16, r + 8);
  strokeRect(x - 5, y - 5, w + 10, h + 10, rgba(...color, 0.4 * a), 7, r + 4);
  strokeRect(x - 1, y - 1, w + 2, h + 2, rgba(...color, a), 3, r);
}

function shot(name, color, rgb, title, sub, p) {
  backdrop(p);
  const w = 1200, h = Math.round(w * (name === 'tui-sql.png' ? 653 / 1600 : 1928 / 3200)) , x = (W - w) / 2;
  const y = name === 'tui-sql.png' ? 320 : 260;
  const a = interpolate(p, [4, 24], [0, 1], 'out');
  const rise = springIn(p, 30, 60, 0, 0, 30);
  shadow(x, y + rise, w, h, 22, 60, rgba(...rgb, 0.35 * a));
  image(`${SHOTS}/${name}`, x, y + rise, w, h, 22, 'cover', 0, a);
  neonFrame(x, y + rise, w, h, rgb, a);
  const t = springIn(p, 30, 30, 0, 14, 24);
  centered(title, 70 + t, 78, rgba(255, 255, 255, interpolate(p, [10, 30], [0, 1], 'out')));
  centered(sub, 186, 40, rgba(...rgb, interpolate(p, [26, 46], [0, 1], 'out')));
}

const ENGINES = ['SQLite', 'DuckDB', 'PostgreSQL', 'MySQL', 'MongoDB', 'Redis', 'SQL Server', 'Oracle', 'Cassandra', 'S3'];
const COLORS = [[45, 226, 255], [255, 45, 149], [155, 92, 255], [255, 154, 60]];

function hook(f) {
  const p = f.index;
  backdrop(p);
  const s = springIn(p, 30, 0.7, 1, 0, 30);
  const [w] = measure('DearSQL', 190 * s);
  text('DearSQL', (W - w) / 2, 300, 190 * s, rgba(255, 255, 255, interpolate(p, [0, 14], [0, 1], 'out')));
  centered('every database. one native app.', 520, 52, rgba(45, 226, 255, interpolate(p, [20, 40], [0, 1], 'out')));
  // engine pills in two rows
  const sizes = ENGINES.map(e => measure(e, 34)[0] + 56);
  [ENGINES.slice(0, 5), ENGINES.slice(5)].forEach((row, r) => {
    const total = row.reduce((s2, e) => s2 + measure(e, 34)[0] + 56 + 18, -18);
    let x = (W - total) / 2;
    row.forEach((e, i) => {
      const idx = r * 5 + i, c = COLORS[idx % 4];
      const a = interpolate(p, [40 + idx * 4, 56 + idx * 4], [0, 1], 'out');
      const pw = measure(e, 34)[0] + 56;
      neonFrame(x, 650 + r * 100, pw, 64, c, a, 32);
      text(e, x + 28, 662 + r * 100, 34, rgba(255, 255, 255, a));
      x += pw + 18;
    });
  });
}

function end(f) {
  const p = f.index;
  backdrop(p);
  const s = springIn(p, 30, 0.85, 1, 0, 24);
  const [w] = measure('DearSQL', 170 * s);
  text('DearSQL', (W - w) / 2, 380, 170 * s, rgba(255, 255, 255, interpolate(p, [0, 12], [0, 1], 'out')));
  centered('dearsql.dev', 620, 56, rgba(255, 45, 149, interpolate(p, [16, 36], [0, 1], 'out')));
  centered('fast. native. yours.', 720, 40, rgba(45, 226, 255, interpolate(p, [28, 48], [0, 1], 'out')));
}

video({
  width: W, height: H, fps: 30,
  scenes: [
    { name: 'hook', frames: 150, draw: hook },
    { name: 'chart', frames: 110, overlap: 12, draw: f => shot('results-chart.png', CYAN, [45, 226, 255], 'Chart any result', 'bar, line, area, scatter, pie', f.index) },
    { name: 'indexes', frames: 110, overlap: 12, draw: f => shot('review-indexes.png', PINK, [255, 45, 149], 'Review your indexes', 'findings, with the fix to consider', f.index) },
    { name: 'tui', frames: 100, overlap: 12, draw: f => shot('tui-sql.png', VIOLET, [155, 92, 255], 'Or live in the terminal', 'dearsql --tui', f.index) },
    { name: 'end', frames: 90, overlap: 12, draw: end },
  ],
  post: [
    { effect: 'glow', threshold: 0.72, radius: 0.012, intensity: 0.85 },
    { effect: 'glow', threshold: 0.8, radius: 0.04, intensity: 0.5 },
    { effect: 'chromatic', shift: 0.0025 },
    { effect: 'grade', contrast: 1.08, saturation: 1.15 },
    { effect: 'vignette', strength: 0.55, radius: 0.7 },
    { effect: 'grain', amount: 0.035 },
  ],
  audio: args.music ? [{ path: args.music, start: 0, duration: 18, volume: 0.85, fadeIn: 0.8, fadeOut: 2.5 }] : [],
  silentAudio: !args.music,
});
