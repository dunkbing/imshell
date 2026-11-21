// real ImGui windows and widgets, drawn by a script: a 12 s square clip.
//   imshell-video demo-widgets.js render -o demo-widgets.mp4 --set music=/path/to/track.wav
const S = 1080;

function card(name, x, y, w, h, p, start, body) {
  const t = springIn(p, 30, 90, 0, start, 36);
  const a = interpolate(p, [start, start + 16], [0, 1], 'out');
  if (a <= 0) return;
  window(name, x, y + t, w, h, body);
}

video({
  width: S, height: S, fps: 30, frames: 360,
  draw(f) {
    const p = f.position;
    layer('back');
    gradientRect(0, 0, S, S, '#10142a', '#1d1b3a', 'v');
    circle(S * 0.85, S * 0.12, 260, rgba(90, 169, 255, 0.07));
    circle(S * 0.1, S * 0.9, 300, rgba(255, 180, 84, 0.05));

    const [tw] = measure('Real widgets.', 78);
    const ta = interpolate(p, [0, 20], [0, 1], 'out');
    text('Real widgets.', (S - tw) / 2, 70 + springIn(p, 30, 30, 0, 0, 24), 78, rgba(238, 241, 251, ta));
    const [sw] = measure('Drawn by a script.', 38);
    text('Drawn by a script.', (S - sw) / 2, 160, 38, rgba(90, 169, 255, interpolate(p, [20, 40], [0, 1], 'out')));

    card('Deploy', 90, 250, 420, 250, p, 40, () => {
      label('Build and ship');
      separator();
      progress(interpolate(p, [60, 200], [0, 1], 'inOut'));
      button('Cancel');
      sameLine();
      button('Run');
    });
    card('Options', 570, 250, 420, 250, p, 56, () => {
      label('Preferences');
      separator();
      checkbox('Notifications', true);
      checkbox('Dark mode', true);
      checkbox('Auto update', false);
    });
    card('Usage', 90, 560, 420, 250, p, 72, () => {
      label('This month');
      separator();
      progress(interpolate(p, [90, 220], [0, 0.72], 'out'));
      progress(interpolate(p, [100, 230], [0, 0.45], 'out'));
      progress(interpolate(p, [110, 240], [0, 0.9], 'out'));
    });
    card('Status', 570, 560, 420, 250, p, 88, () => {
      label('All systems normal');
      separator();
      label('render  ok');
      label('verify  ok');
      label('inspect 0 errors');
    });
    const [bw] = measure('imshell-video', 34);
    text('imshell-video', (S - bw) / 2, 930, 34, rgba(140, 151, 184, interpolate(p, [150, 175], [0, 1], 'out')));
    const [gw] = measure('github.com/dunkbing/imshell', 26);
    text('github.com/dunkbing/imshell', (S - gw) / 2, 984, 26, rgba(140, 151, 184, interpolate(p, [165, 190], [0, 1], 'out')));
  },
  audio: args.music ? [{ path: args.music, start: 0, duration: 12, volume: 0.8, fadeIn: 0.8, fadeOut: 2 }] : [],
  silentAudio: !args.music,
});
