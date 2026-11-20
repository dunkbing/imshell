// "a video is a function": a 19 s intro for imshell-video, made with imshell-video.
//   imshell-video demo-intro.js render -o demo-intro.mp4 --set music=/path/to/track.wav
// numbers on the speed card are measured (M4, 1080p, 24 animated windows); change them if you re-measure.
const W = 1920, H = 1080;
const BG = '#0e1220', PANEL = '#151b30', LINE = '#2a3354', INK = '#eef1fb', MUTED = '#8c97b8', ACCENT = '#5aa9ff', WARM = '#ffb454';

function backdrop() {
  layer('back');
  gradientRect(0, 0, W, H, '#0e1220', '#161c36', 'v');
}

function centered(s, y, size, color) {
  const [w] = measure(s, size);
  text(s, (W - w) / 2, y, size, color);
}

// words of a line fade and rise one after another
function reveal(s, p, y, size, color, start, step = 4) {
  const words = s.split(' ');
  const [total] = measure(s, size);
  const [space] = measure(' ', size);
  let x = (W - total) / 2;
  words.forEach((word, i) => {
    const a = interpolate(p, [start + i * step, start + i * step + 14], [0, 1], 'out');
    const rise = springIn(p, 30, 26, 0, start + i * step, 24);
    const [ww] = measure(word, size);
    text(word, x, y + rise, size, rgba(color[0], color[1], color[2], a));
    x += ww + space;
  });
}

function typed(lines, chars, x, y, size, lh, color) {
  let left = Math.floor(chars);
  lines.forEach((line, i) => {
    if (left <= 0) return;
    const shown = line.slice(0, left);
    left -= line.length;
    const colored = shown.startsWith('$ ') ? [WARM, ACCENT] : null;
    if (colored) {
      text('$', x, y + i * lh, size, WARM);
      text(shown.slice(1), x + measure('$', size)[0], y + i * lh, size, INK);
    } else {
      text(shown, x, y + i * lh, size, color);
    }
  });
}

function panel(x, y, w, h, title) {
  shadow(x, y, w, h, 18, 36, 'rgba(0,0,0,0.6)');
  rect(x, y, w, h, PANEL, 18);
  strokeRect(x, y, w, h, LINE, 1.5, 18);
  [['#ff5f57', 0], ['#febc2e', 1], ['#28c840', 2]].forEach(([c, i]) => circle(x + 26 + i * 24, y + 26, 7, c));
  if (title) text(title, x + 110, y + 14, 22, MUTED);
}

const CODE = [
  'video({',
  '  width: 1280, height: 720,',
  '  fps: 30, frames: 120,',
  '  draw(f) {',
  '    const rise = springIn(',
  '      f.position, f.fps, 80, 0, 6, 30);',
  "    text('Hello', 120, 260 + rise,",
  "         120, '#fff');",
  '  },',
  '});',
];

function hook(f) {
  backdrop();
  const p = f.index;
  reveal('A video is a function', p, 380, 96, [238, 241, 251], 6);
  reveal('frame number in, picture out', p, 520, 52, [90, 169, 255], 36, 5);
  const a = interpolate(p, [60, 80], [0, 1], 'out');
  centered('imshell-video', 880, 34, rgba(140, 151, 184, a));
}

function code(f) {
  backdrop();
  const p = f.index;
  const slide = interpolate(p, [0, 24], [1, 0], 'out');
  const px = 90 - slide * 120, pw = 880, ph = 640, py = 220;
  const alpha = interpolate(p, [0, 18], [0, 1], 'out');
  panel(px, py, pw, ph, 'scene.js');
  const chars = interpolate(p, [14, 104], [0, CODE.join('').length], 'inOut');
  const line = Math.min(CODE.length - 1, (() => { let n = Math.floor(chars), i = 0; while (i < CODE.length - 1 && n > CODE[i].length) { n -= CODE[i].length; i++; } return i; })());
  typed(CODE, chars, px + 40, py + 76, 34, 54, '#c9d3f5');
  if (Math.floor(p / 8) % 2 === 0 && p < 130) rect(px + 40 + 18 * 0.6 * 10, py + 76 + line * 54 + 4, 3, 38, ACCENT);

  // the result, drawn by the same numbers
  const rx = 1020 + slide * 120, ry = 220, rw = 810, rh = 640;
  shadow(rx, ry, rw, rh, 18, 36, 'rgba(0,0,0,0.6)');
  rect(rx, ry, rw, rh, '#0b0f1d', 18);
  strokeRect(rx, ry, rw, rh, LINE, 1.5, 18);
  text('render', rx + 24, ry + 14, 22, MUTED);
  clip(rx, ry + 54, rw, rh - 54, () => {
    const lp = Math.max(0, p - 104);
    const rise = springIn(lp, 30, 80, 0, 0, 30);
    const fade = interpolate(lp, [0, 14], [0, 1], 'out');
    const [tw, th] = measure('Hello', 150);
    text('Hello', rx + (rw - tw) / 2, ry + 54 + (rh - 54 - th) / 2 + rise, 150, rgba(255, 255, 255, fade));
    const bar = interpolate(lp, [10, 44], [0, 1], 'inOut');
    rect(rx + 150, ry + rh - 90, (rw - 300) * bar, 8, ACCENT, 4);
  });
  const cap = interpolate(p, [30, 50], [0, 1], 'out');
  centered('Write it as a script.', 920, 44, rgba(238, 241, 251, cap));
}

function check(f) {
  backdrop();
  const p = f.index;
  const lines = [
    '$ imshell-video scene.js strip all -n 12',
    '$ imshell-video scene.js inspect all',
    '0 errors, 0 warnings, 0 notes',
    '$ imshell-video scene.js verify',
    'playing and seeking agree',
    '$ imshell-video scene.js render -o out.mp4',
  ];
  const slide = interpolate(p, [0, 22], [1, 0], 'out');
  panel(90 - slide * 100, 200, 1000, 560, 'terminal');
  const total = lines.join('').length;
  const chars = interpolate(p, [12, 96], [0, total], 'linear');
  let left = Math.floor(chars);
  lines.forEach((line, i) => {
    if (left <= 0) return;
    const shown = line.slice(0, left);
    left -= line.length;
    const prompt = line.startsWith('$ ');
    if (prompt) {
      text('$', 130 - slide * 100, 276 + i * 66, 30, WARM);
      text(shown.slice(1), 130 - slide * 100 + measure('$', 30)[0], 276 + i * 66, 30, INK);
    } else {
      text(shown, 130 - slide * 100, 276 + i * 66, 30, '#7be0a5');
    }
  });

  // the contact sheet it just made
  const cols = 4, rows = 3, cw = 150, ch = 84, gx = 1140;
  for (let i = 0; i < cols * rows; i++) {
    const a = interpolate(p, [20 + i * 4, 32 + i * 4], [0, 1], 'out');
    const x = gx + (i % cols) * (cw + 12) + slide * 140, y = 232 + Math.floor(i / cols) * (ch + 12);
    rect(x, y, cw, ch, rgba(21, 27, 48, a), 8);
    strokeRect(x, y, cw, ch, rgba(42, 51, 84, a), 1.2, 8);
    const px = x + 16 + (i / (cols * rows - 1)) * (cw - 48);
    circle(px, y + ch / 2, 10, rgba(90, 169, 255, a));
    text(String(i * 10), x + 8, y + 4, 16, rgba(140, 151, 184, a));
  }
  text('strip.png', gx + slide * 140, 232 + rows * (ch + 12), 22, MUTED);
  const cap = interpolate(p, [30, 50], [0, 1], 'out');
  centered('Check it without watching.', 880, 44, rgba(238, 241, 251, cap));
}

function speed(f) {
  backdrop();
  const p = f.index;
  const stats = [[221, ' fps', 'rendering to mp4', ACCENT], [7.4, 'x', 'faster than real time', WARM], [3, ' MB', 'one binary, no compiler', '#7be0a5']];
  stats.forEach(([value, unit, label, color], i) => {
    const start = 8 + i * 14;
    const t = interpolate(p, [start, start + 40], [0, 1], 'out');
    const a = interpolate(p, [start, start + 14], [0, 1], 'out');
    const x = 150 + i * 580, y = 330;
    const shown = value % 1 === 0 ? String(Math.round(value * t)) : (value * t).toFixed(1);
    rect(x, y - 30, 520, 380, rgba(21, 27, 48, a), 24);
    strokeRect(x, y - 30, 520, 380, rgba(42, 51, 84, a), 1.5, 24);
    const num = shown + unit;
    text(num, x + 40, y + 40, 112, rgba(color === ACCENT ? 90 : color === WARM ? 255 : 123, color === ACCENT ? 169 : color === WARM ? 180 : 224, color === ACCENT ? 255 : color === WARM ? 84 : 165, a));
    text(label, x + 44, y + 220, 34, rgba(140, 151, 184, a));
  });
  const cap = interpolate(p, [20, 40], [0, 1], 'out');
  centered('Fast, and nothing to install first.', 80, 52, rgba(238, 241, 251, cap));
  const note = interpolate(p, [60, 80], [0, 1], 'out');
  centered('1080p, 24 animated windows, Apple M4. Drawing and encoding are native.', 860, 28, rgba(140, 151, 184, note));
}

function end(f) {
  backdrop();
  const p = f.index;
  const s = springIn(p, 30, 0.85, 1, 0, 30);
  const a = interpolate(p, [0, 16], [0, 1], 'out');
  const size = 120 * s;
  const [w] = measure('imshell-video', size);
  text('imshell-video', (W - w) / 2, 380, size, rgba(238, 241, 251, a));
  reveal('A video is a script. A script is a file.', p, 560, 40, [90, 169, 255], 20, 3);
  const b = interpolate(p, [44, 64], [0, 1], 'out');
  centered('github.com/dunkbing/imshell', 760, 34, rgba(140, 151, 184, b));
}

const MUSIC = args.music;
video({
  width: W, height: H, fps: 30,
  scenes: [
    { name: 'hook', frames: 100, draw: hook },
    { name: 'code', frames: 170, overlap: 10, draw: code },
    { name: 'check', frames: 120, overlap: 10, draw: check },
    { name: 'speed', frames: 120, overlap: 10, draw: speed },
    { name: 'end', frames: 100, overlap: 10, draw: end },
  ],
  audio: MUSIC ? [{ path: MUSIC, start: 0, duration: 20.5, volume: 0.85, fadeIn: 1, fadeOut: 2.5 }] : [],
  silentAudio: !MUSIC,
});
