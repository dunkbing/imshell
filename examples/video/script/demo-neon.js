// neon with post effects: bright shapes on near-black, then glow + chromatic + grain + vignette over the finished frame.
//   imshell-video demo-neon.js render -o demo-neon.mp4 --set music=/path/to/track.wav
const W = 1920, H = 1080, BG = '#06030f', PINK = '#ff2d95', CYAN = '#2de2ff', ORANGE = '#ff9a3c', VIOLET = '#9b5cff';
const HORIZON = 640;

function sky() {
  layer('back');
  gradientRect(0, 0, W, HORIZON, '#0a0420', '#2a0a3c', 'v');
  rect(0, HORIZON, W, H - HORIZON, BG);
}

function sun(p) {
  const cx = W / 2, cy = 470;
  for (let i = 0; i < 24; i++) {
    const t = i / 23;
    circle(cx, cy, 200 - i * 8, rgba(255, Math.round(60 + 140 * t), Math.round(150 - 110 * t), 1));
  }
  // slits that widen toward the bottom and drift down
  for (let k = 0; k < 7; k++) {
    const y = cy + 20 + k * 26 + ((p * 0.3) % 26) * 0;
    rect(cx - 210, y, 420, 3 + k * 3, '#14062a');
  }
  rect(0, HORIZON, W, 8, '#14062a');
}

function grid(p) {
  const scroll = (p * 0.025) % 1;
  for (let k = 0; k < 14; k++) {
    const t = (k + scroll) / 14;
    const y = HORIZON + t * t * (H - HORIZON);
    line(0, y, W, y, rgba(45, 226, 255, 0.35 + 0.65 * t), 2 + t * 2);
  }
  for (let i = -12; i <= 12; i++) {
    line(W / 2 + i * 14, HORIZON, W / 2 + i * 230, H, rgba(255, 45, 149, 0.8), 2.5);
  }
}

function neonText(s, y, size, color, p, flick) {
  const [w] = measure(s, size);
  const a = flick ? 0.82 + 0.18 * Math.sin(p * 0.9) * Math.sin(p * 0.37) : 1;
  text(s, (W - w) / 2, y, size, color + (a < 1 ? Math.round(a * 255).toString(16).padStart(2, '0') : ''));
}

function horizonScene(f) {
  sky(); sun(f.index); grid(f.index);
  const a = interpolate(f.index, [8, 40], [0, 1], 'out');
  const rise = springIn(f.index, 30, 40, 0, 8, 30);
  const [w] = measure('IMSHELL VIDEO', 120);
  text('IMSHELL VIDEO', (W - w) / 2, 150 + rise, 120, rgba(255, 255, 255, a));
  const [w2] = measure('neon from a script', 44);
  text('neon from a script', (W - w2) / 2, 290, 44, rgba(45, 226, 255, interpolate(f.index, [40, 64], [0, 1], 'out')));
}

// rings and a cube-ish frame: the shapes glow best when thin and saturated
function shapesScene(f) {
  layer('back'); rect(0, 0, W, H, BG);
  const p = f.index;
  for (let i = 0; i < 6; i++) {
    const r = 80 + i * 70 + 24 * Math.sin(p * 0.08 + i);
    ring(W / 2, H / 2, r, [PINK, CYAN, VIOLET, ORANGE, PINK, CYAN][i], 5);
  }
  for (let i = 0; i < 12; i++) {
    const ang = p * 0.02 + i * Math.PI / 6;
    line(W / 2, H / 2, W / 2 + Math.cos(ang) * 520, H / 2 + Math.sin(ang) * 520, rgba(155, 92, 255, 0.55), 3);
  }
  const s = 230;
  const q = interpolate(p, [0, 30], [0, 1], 'out');
  strokeRect(W / 2 - s * q, H / 2 - s * q, 2 * s * q, 2 * s * q, '#ffffff', 5, 18);
  neonText('GLOW', H / 2 - 60, 120, '#ffffff', p, false);
  neonText('threshold, radius, intensity', 960, 40, CYAN, p, false);
}

function stackScene(f) {
  layer('back'); rect(0, 0, W, H, BG);
  const p = f.index;
  const items = [['glow', PINK, 'bloom on what is bright'], ['chromatic', CYAN, 'colour fringes at the edges'], ['grain', ORANGE, 'film noise that moves each frame'], ['vignette', VIOLET, 'darker corners']];
  items.forEach(([name, color, note], i) => {
    const start = 6 + i * 22;
    const a = interpolate(p, [start, start + 14], [0, 1], 'out');
    const x = 300 + interpolate(p, [start, start + 18], [80, 0], 'out');
    const y = 230 + i * 170;
    strokeRect(x - 30, y - 20, 1320, 130, rgba(255, 255, 255, a), 4, 22);
    text(name, x + 20, y + 10, 72, rgba(255, 255, 255, a));
    text(note, x + 620, y + 34, 40, rgba(255, 255, 255, a * 0.85));
  });
  neonText('four post effects, one line each', 100, 54, CYAN, p, false);
}

function endScene(f) {
  sky(); sun(f.index); grid(f.index);
  const a = interpolate(f.index, [0, 20], [0, 1], 'out');
  const [w] = measure('github.com/dunkbing/imshell', 52);
  text('github.com/dunkbing/imshell', (W - w) / 2, 220, 52, rgba(255, 255, 255, a));
}

video({
  width: W, height: H, fps: 30,
  scenes: [
    { name: 'horizon', frames: 130, draw: horizonScene },
    { name: 'shapes', frames: 110, overlap: 12, draw: shapesScene },
    { name: 'stack', frames: 110, overlap: 12, draw: stackScene },
    { name: 'end', frames: 70, overlap: 12, draw: endScene },
  ],
  post: [
    { effect: 'glow', threshold: 0.55, radius: 0.012, intensity: 0.9 },
    { effect: 'glow', threshold: 0.7, radius: 0.04, intensity: 0.55 },
    { effect: 'chromatic', shift: 0.003 },
    { effect: 'grade', contrast: 1.12, saturation: 1.2 },
    { effect: 'vignette', strength: 0.6, radius: 0.7 },
    { effect: 'grain', amount: 0.04 },
  ],
  audio: args.music ? [{ path: args.music, start: 0, duration: 14.5, volume: 0.85, fadeIn: 0.8, fadeOut: 2 }] : [],
  silentAudio: !args.music,
});
