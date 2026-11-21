// DearSQL, screen-studio style: a real recording of the app (ui_tests, DEARSQL_RECORD + DEARSQL_RECORD_TRACK) inside a rounded,
// shadowed window on a gradient; the camera zooms on clicks and follows the mouse, a drawn cursor with click ripples.
//   cat track.js dearsql-screen.js > run.js && imshell-video run.js render --ffmpeg -o out.mp4 --set footage=showcase.mp4 --set music=music.wav
const W = 1920, H = 1080, FPS = 60, K = FPS / 30, N = TRACK.length, OUTRO = 80 * K; // the recording is 60 fps: scrolling stays as smooth as it ran
const FOOT = args.footage || 'showcase.mp4';
const DISP = [1600, 1000];
const CARD = { x: 240, y: 112, w: 1440, h: 900 };

const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
function smooth(arr, r, passes = 2) {
  let a = arr;
  for (let p = 0; p < passes; p++) {
    a = a.map((_, i) => { let s = 0, c = 0; for (let k = -r; k <= r; k++) { const j = clamp(i + k, 0, arr.length - 1); s += a[j]; c++; } return s / c; });
  }
  return a;
}

// mouse in footage fractions, 0 until it first moves
const MX = TRACK.map(t => t[0] / DISP[0]), MY = TRACK.map(t => t[1] / DISP[1]);
const MOVED = TRACK.map(t => t[0] + t[1] > 0 ? 1 : 0);
const CLICKS = []; TRACK.forEach((t, i) => { if (t[2]) CLICKS.push(i); });
// the camera is in on a click for a while after it (typing, a result arriving), out otherwise
const ACTIVE = TRACK.map((_, i) => CLICKS.some(c => i >= c - 8 * K && i <= c + 80 * K) ? 1 : 0);
const ZOOM = smooth(ACTIVE, 18 * K).map(a => 1 + 0.7 * a);
// focus follows the mouse, held where it was last seen so the camera does not swing back to the corner
let lx = 0.5, ly = 0.5;
const FX = [], FY = [];
TRACK.forEach((_, i) => { if (MOVED[i]) { lx = MX[i]; ly = MY[i]; } FX.push(lx); FY.push(ly); });
const CX = smooth(FX, 22 * K, 3), CY = smooth(FY, 22 * K, 3);
const SX = smooth(MX, 2 * K, 1), SY = smooth(MY, 2 * K, 1);

function view(i) {
  const z = ZOOM[Math.min(i, N - 1)], half = 0.5 / z;
  const cx = clamp(CX[Math.min(i, N - 1)], half, 1 - half), cy = clamp(CY[Math.min(i, N - 1)], half, 1 - half);
  return [cx - half, cy - half, cx + half, cy + half];
}

function background(p) {
  layer('back');
  gradientRect(0, 0, W, H, '#3b2a8c', '#0f4a6e', 'v');
  circle(260 + 30 * Math.sin(p * 0.01), 160, 420, rgba(255, 120, 200, 0.10));
  circle(1650, 900 + 20 * Math.cos(p * 0.012), 480, rgba(80, 220, 255, 0.10));
}

// a mouse arrow: filled by a fan of lines from its tip
const ARROW = [[0, 17], [4.4, 13.2], [7.6, 20.4], [10.4, 19.2], [7.2, 12.2], [12.6, 12]];
function arrow(x, y, s, color, grow) {
  for (let k = 0; k < ARROW.length; k++) {
    const a = k === 0 ? [0, 0] : ARROW[k - 1], b = ARROW[k];
    for (let u = 0; u <= 1; u += 0.1) {
      const px = a[0] + (b[0] - a[0]) * u, py = a[1] + (b[1] - a[1]) * u;
      line(x, y, x + px * s, y + py * s, color, 2.4 + grow);
    }
  }
}

const CAPTIONS = [
  [0.4, 3.2, 'A million rows. Scrolling stays smooth.'],
  [3.4, 6.2, 'Jump to any page, instantly'],
  [6.4, 9.0, 'Straight to the last page'],
  [9.2, 12.4, 'Filter with plain SQL'],
  [12.6, 15.4, 'Every table gets its own tab'],
  [15.6, 19.2, 'Group a million orders in a blink'],
  [19.4, 22.4, 'Then chart the result'],
];

function caption(p) {
  const t = p / FPS;
  CAPTIONS.forEach(([a, b, s]) => {
    if (t < a || t > b) return;
    const al = Math.min(interpolate(t, [a, a + 0.3], [0, 1], 'out'), interpolate(t, [b - 0.3, b], [1, 0], 'in'));
    const [w] = measure(s, 40);
    const x = (W - w) / 2 - 36, y = 28 + (1 - al) * -14;
    shadow(x, y, w + 72, 64, 32, 24, rgba(0, 0, 0, 0.35 * al));
    rect(x, y, w + 72, 64, rgba(255, 255, 255, 0.95 * al), 32);
    text(s, x + 36, y + 11, 40, rgba(30, 24, 70, al));
  });
}

// the group-by over 1,000,000 rows: counts the real time from the Run click, stops at what the app reported in its status bar
const RUN_CLICK = CLICKS.find(c => c / FPS > 16.3);
const REPORTED = 0.97888; // "8 rows 978.88 ms", read from the recording's status bar
function stopwatch(p) {
  if (RUN_CLICK === undefined || p < RUN_CLICK) return;
  const t = (p - RUN_CLICK) / FPS;
  if (t > 5.2) return;
  const al = Math.min(interpolate(t, [0, 0.2], [0, 1], 'out'), interpolate(t, [4.7, 5.2], [1, 0], 'in'));
  const done = t >= REPORTED, shown = Math.min(t, REPORTED);
  const x = W - 470, y = 28, w = 430, h = 92;
  shadow(x, y, w, h, 20, 28, rgba(0, 0, 0, 0.4 * al));
  rect(x, y, w, h, rgba(20, 16, 56, 0.92 * al), 20);
  strokeRect(x, y, w, h, rgba(done ? 120 : 255, done ? 255 : 200, done ? 190 : 90, 0.9 * al), 2, 20);
  const ms = (shown * 1000).toFixed(2) + ' ms';
  text(ms, x + 24, y + 12, 52, rgba(255, 255, 255, al));
  text(done ? 'scanned 1,000,000 rows' : 'running...', x + 24, y + 66, 22, rgba(done ? 120 : 255, done ? 255 : 200, done ? 190 : 90, al));
}

video({
  width: W, height: H, fps: FPS, frames: N + OUTRO,
  draw(f) {
    const p = f.index, i = Math.min(p, N - 1);
    background(p);
    const enter = interpolate(p, [0, 22 * K], [0, 1], 'out');
    const dy = (1 - enter) * 40, sc = 0.96 + 0.04 * enter;
    const cw = CARD.w * sc, ch = CARD.h * sc, cx0 = CARD.x + (CARD.w - cw) / 2, cy0 = CARD.y + (CARD.h - ch) / 2 + dy;
    shadow(cx0, cy0, cw, ch, 18, 90, rgba(0, 0, 0, 0.55 * enter));
    const [u0, v0, u1, v1] = view(i);
    image(FOOT, cx0, cy0, cw, ch, 16, 'stretch', Math.min(p, N - 1) / FPS, enter, u0, v0, u1, v1);
    strokeRect(cx0, cy0, cw, ch, rgba(255, 255, 255, 0.18 * enter), 1.5, 16);

    // cursor and click ripples, mapped through the camera
    if (MOVED[i] && p < N) {
      const toX = m => cx0 + ((m - u0) / (u1 - u0)) * cw, toY = m => cy0 + ((m - v0) / (v1 - v0)) * ch;
      CLICKS.forEach(c => {
        const age = i - c;
        if (age < 0 || age > 20 * K) return;
        const px = toX(MX[c]), py = toY(MY[c]);
        const a = 1 - age / (20 * K);
        ring(px, py, 10 + (age / K) * 3.2, rgba(255, 255, 255, 0.85 * a), 3);
        ring(px, py, 6 + (age / K) * 2.0, rgba(120, 220, 255, 0.9 * a), 3);
      });
      const px = toX(SX[i]), py = toY(SY[i]);
      if (px > cx0 && px < cx0 + cw && py > cy0 && py < cy0 + ch) {
        const pressed = TRACK[i][2] ? 1 : 0;
        arrow(px + 2, py + 3, 2.1, rgba(0, 0, 0, 0.35), 2);
        arrow(px, py, 2.0, '#101018', 3.2);
        arrow(px, py, 2.0, '#ffffff', pressed ? -0.4 : 0.2);
      }
    }
    caption(p);
    stopwatch(p);

    // outro
    const o = interpolate(p, [N - 6 * K, N + 16 * K], [0, 1], 'inOut');
    if (o > 0) {
      rect(0, 0, W, H, rgba(15, 12, 40, 0.92 * o));
      const s = 160, [w] = measure('DearSQL', s), rise = springIn(p, FPS, 30, 0, N, 26 * K);
      text('DearSQL', (W - w) / 2, 400 + rise, s, rgba(255, 255, 255, o));
      const a2 = interpolate(p, [N + 10 * K, N + 30 * K], [0, 1], 'out');
      const [w2] = measure('every database. one native app.', 48);
      text('every database. one native app.', (W - w2) / 2, 620, 48, rgba(120, 220, 255, a2));
      const [w3] = measure('dearsql.dev', 40);
      text('dearsql.dev', (W - w3) / 2, 710, 40, rgba(255, 255, 255, a2 * 0.8));
    }
  },
  audio: args.music ? [{ path: args.music, start: 0, duration: (N + OUTRO) / FPS, volume: 0.8, fadeIn: 0.6, fadeOut: 2 }] : [],
  silentAudio: !args.music,
});
