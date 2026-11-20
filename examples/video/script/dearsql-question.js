// DearSQL demo: one question, followed through the app with a moving camera over real screens.
//   imshell-video dearsql-question.js render -o dearsql-question.mp4 --set music=../music.wav
const W = 1920, H = 1080, FPS = 30;
const DOCS = args.docs || '/Users/binhbui/code/dearsql/website/public/docs';
const INK = '#0d1117', PAPER = '#f5efe3', AMBER = '#ffb547', MINT = '#5ef2c2', MUTED = '#8b949e';
const AMBER_RGB = [255, 181, 71], MINT_RGB = [94, 242, 194];

const fade = (p, a, b) => interpolate(p, [a, b], [0, 1], 'out');
const lerp = (a, b, t) => a + (b - a) * t;

function backdrop(p) {
  layer('back');
  gradientRect(0, 0, W, H, '#0d1117', '#161b26', 'v');
  // faint dot grid drifting up, so still frames still breathe
  const s = (p * 0.5) % 48;
  for (let y = -48; y < H + 48; y += 48)
    for (let x = 24; x < W; x += 48) circle(x, y - s, 1.6, 'rgba(255,255,255,0.05)');
}

// the step label and caption every screen scene shares
function caption(step, title, p) {
  const a = fade(p, 6, 22), dx = interpolate(p, [6, 26], [-40, 0], 'out');
  text(step, 120 + dx, 64, 28, rgba(...AMBER_RGB, a));
  text(title, 120 + dx, 104, 54, rgba(245, 239, 227, fade(p, 10, 28)));
}

// a camera over a screenshot: (cx, cy) in image pixels goes to the box centre at `zoom`
// (1 = the image width fills the box)
function camera(path, iw, ih, box, from, to, t) {
  const cx = lerp(from[0], to[0], t), cy = lerp(from[1], to[1], t), z = lerp(from[2], to[2], t);
  const s = (box.w / iw) * z;
  clip(box.x, box.y, box.w, box.h, () => {
    rect(box.x, box.y, box.w, box.h, '#ffffff');
    image(path, box.x + box.w / 2 - cx * s, box.y + box.h / 2 - cy * s, iw * s, ih * s);
  });
  return { s, ox: box.x + box.w / 2 - cx * s, oy: box.y + box.h / 2 - cy * s };
}

// a highlight drawn in image coordinates, so it follows the camera
function mark(cam, x, y, w, h, rgb, a) {
  const X = cam.ox + x * cam.s, Y = cam.oy + y * cam.s;
  rect(X, Y, w * cam.s, h * cam.s, rgba(...rgb, 0.12 * a), 6);
  strokeRect(X - 3, Y - 3, w * cam.s + 6, h * cam.s + 6, rgba(...rgb, a), 3, 8);
}

const BOX = { x: 120, y: 190, w: 1680, h: 840 };

function screenScene(p, frames, step, title, file, from, to, marks) {
  backdrop(p);
  caption(step, title, p);
  const a = fade(p, 0, 16), rise = springIn(p, FPS, 40, 0, 0, 26);
  const box = { ...BOX, y: BOX.y + rise };
  shadow(box.x, box.y, box.w, box.h, 18, 50, rgba(0, 0, 0, 0.55 * a));
  const t = interpolate(p, [14, frames - 10], [0, 1], 'inOut');
  const cam = camera(`${DOCS}/${file}`, 1600, 1032, box, from, to, t);
  strokeRect(box.x, box.y, box.w, box.h, 'rgba(255,255,255,0.12)', 2, 18);
  // highlights move with the camera, so they are cut to the box like the screen
  clip(box.x, box.y, box.w, box.h, () => {
    for (const m of marks || []) mark(cam, ...m.r, m.rgb, fade(p, m.at, m.at + 12));
  });
}

// a small screenshot (a dialog or a popup) as a floating card
function card(file, iw, ih, scale, p, title, step, rows) {
  backdrop(p);
  caption(step, title, p);
  const w = iw * scale, h = ih * scale, x = (W - w) / 2;
  const y = 330 + springIn(p, FPS, 50, 0, 0, 26);
  const a = fade(p, 0, 16);
  shadow(x, y, w, h, 16, 60, rgba(...MINT_RGB, 0.25 * a));
  image(`${DOCS}/${file}`, x, y, w, h, 16, 'cover', 0, a);
  for (const r of rows || []) {
    const ra = fade(p, r.at, r.at + 10);
    strokeRect(x + r.r[0] * scale - 4, y + r.r[1] * scale - 4, r.r[2] * scale + 8, r.r[3] * scale + 8,
               rgba(...r.rgb, ra), 3, 8);
  }
}

function hook(f) {
  const p = f.index;
  backdrop(p);
  const q = 'which country buys the most?';
  const typed = Math.floor(interpolate(p, [10, 58], [0, q.length]));
  const size = 76, [w] = measure(q, size), x = (W - w) / 2, y = 430;
  text('>', x - 70, y, size, rgba(...AMBER_RGB, fade(p, 0, 8)));
  text(q.slice(0, typed), x, y, size, PAPER);
  if (typed < q.length || Math.floor(p / 12) % 2 === 0) {
    const [tw] = measure(q.slice(0, typed), size);
    rect(x + tw + 6, y + 8, 36, size - 6, rgba(...AMBER_RGB, 0.9));
  }
  const ua = fade(p, 66, 86);
  rect(x, y + size + 30, w * interpolate(p, [66, 92], [0, 1], 'out'), 4, rgba(...MINT_RGB, ua));
  const sub = 'one question, start to finish, in DearSQL';
  const [sw] = measure(sub, 34);
  text(sub, (W - sw) / 2, y + size + 70, 34, rgba(139, 148, 158, fade(p, 80, 100)));
}

const ENGINES = 'PostgreSQL · MySQL · SQLite · DuckDB · SQL Server · Oracle · MongoDB · Redis · Cassandra · S3 · Excel · CSV · ';

function end(f) {
  const p = f.index;
  backdrop(p);
  const s = springIn(p, FPS, 0.8, 1, 0, 26);
  const [w] = measure('DearSQL', 168 * s);
  text('DearSQL', (W - w) / 2, 330, 168 * s, rgba(245, 239, 227, fade(p, 0, 12)));
  const tag = 'ask it anything. natively.';
  const [tw] = measure(tag, 44);
  text(tag, (W - tw) / 2, 540, 44, rgba(...MINT_RGB, fade(p, 14, 32)));
  // engine ticker, scrolling left
  const band = ENGINES + ENGINES, [bw] = measure(ENGINES, 32), off = (p * 3) % bw;
  clip(0, 680, W, 60, () => text(band, -off, 690, 32, rgba(139, 148, 158, fade(p, 20, 40))));
  const url = 'dearsql.dev';
  const [uw] = measure(url, 50);
  text(url, (W - uw) / 2, 820, 50, rgba(...AMBER_RGB, fade(p, 30, 48)));
}

video({
  width: W, height: H, fps: FPS,
  scenes: [
    { name: 'hook', frames: 120, draw: hook },
    { name: 'find', frames: 84, overlap: 12, draw: f => card('open-quickly.png', 680, 170, 2.2, f.index,
        'Find the table, typos and all', '01  find', [{ r: [32, 62, 616, 22], rgb: AMBER_RGB, at: 30 }]) },
    { name: 'browse', frames: 104, overlap: 12, draw: f => screenScene(f.index, 104, '02  browse',
        'Filter any table with SQL', 'table-grid.png', [800, 516, 1.0], [860, 240, 1.6],
        [{ r: [464, 36, 400, 30], rgb: AMBER_RGB, at: 46 }]) },
    { name: 'explain', frames: 120, overlap: 12, draw: f => screenScene(f.index, 120, '03  explain',
        'See why it is slow', 'sql-explain.png', [800, 160, 1.6], [1000, 640, 1.55],
        [{ r: [462, 490, 1080, 30], rgb: AMBER_RGB, at: 60 }, { r: [610, 722, 880, 24], rgb: MINT_RGB, at: 78 }]) },
    { name: 'chart', frames: 104, overlap: 12, draw: f => screenScene(f.index, 104, '04  answer',
        'Chart the answer', 'results-chart.png', [1010, 730, 1.7], [1000, 560, 1.12],
        [{ r: [1440, 536, 52, 404], rgb: MINT_RGB, at: 70 }]) },
    { name: 'safe', frames: 104, overlap: 12, draw: f => card('delete-preview.png', 760, 200, 2.0, f.index,
        'Know what a delete takes with it', '05  stay safe',
        [{ r: [14, 64, 576, 18], rgb: [255, 99, 99], at: 30 }, { r: [14, 82, 420, 54], rgb: AMBER_RGB, at: 54 }]) },
    { name: 'end', frames: 120, overlap: 14, draw: end },
  ],
  post: [
    { effect: 'grade', contrast: 1.05, saturation: 1.08 },
    { effect: 'vignette', strength: 0.45, radius: 0.75 },
    { effect: 'grain', amount: 0.03 },
  ],
  audio: args.music ? [{ path: args.music, start: 0, duration: 23, volume: 0.8, fadeIn: 0.6, fadeOut: 2.5 }] : [],
  silentAudio: !args.music,
});
