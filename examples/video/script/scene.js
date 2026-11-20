// a 4 s title card, the same as examples/video/starter but as a script: no compiler.
//   imshell-video scene.js timeline | strip all -n 12 -o strip.png | inspect | verify | render -o out.mp4
video({
  width: 1280, height: 720, fps: 30, frames: 120,
  draw(f) {
    const { width: w, height: h, position: p } = f;
    layer('back');
    rect(0, 0, w, h, '#0e1220');

    // title: springs up while fading in
    const rise = springIn(p, f.fps, h * 0.08, 0, 6, 30);
    const fade = interpolate(p, [6, 20], [0, 1], 'out');
    const titleSize = h * 0.14;
    const [tw, th] = measure('Hello, imshell', titleSize);
    text('Hello, imshell', (w - tw) / 2, h * 0.30 + rise, titleSize, rgba(240, 244, 255, fade));

    // subtitle: slides in after the title
    const slide = interpolate(p, [30, 54], [h * 0.03, 0], 'out');
    const subFade = interpolate(p, [30, 54], [0, 1], 'out');
    const sub = 'a video from a script';
    const [sw] = measure(sub, h * 0.055);
    text(sub, (w - sw) / 2, h * 0.30 + th * 1.1 + slide, h * 0.055, rgba(150, 170, 220, subFade));

    // a ring with a dot going round, and a bar that fills
    const cx = w / 2, cy = h * 0.17, r = h * 0.06, a = p * 0.12;
    const ring_ = interpolate(p, [0, 15], [0, 1], 'out');
    ring(cx, cy, r, rgba(60, 80, 130, ring_), 2);
    circle(cx + r * Math.cos(a), cy + r * Math.sin(a), 7, rgba(90, 160, 255, ring_));
    const bw = w * 0.4, bx = (w - bw) / 2, by = h * 0.78;
    const fill = interpolate(p, [48, 108], [0, 1], 'inOut');
    const barFade = interpolate(p, [40, 54], [0, 1], 'out');
    rect(bx, by, bw, 10, rgba(40, 50, 80, barFade), 5);
    rect(bx, by, bw * fill, 10, rgba(90, 160, 255, barFade), 5);
  },
});
