// every call the front end has, with Math.random in use: a frame still has to be a pure function of its number
video({
  width: 320, height: 180, fps: 30,
  scenes: [
    { name: 'a', frames: 20, draw(f) {
        layer('back'); rect(0, 0, 320, 180, '#102030');
        for (let i = 0; i < 8; i++) circle(20 + i * 35, 40 + 30 * Math.random(), 8, rgba(90, 160, 255, 0.8));
        ring(160, 90, 30, '#fff', 2); line(0, 0, 320, 180, '#ff08', 2); strokeRect(10, 10, 100, 40, '#fff', 1, 4);
        const [w] = measure('hello', 20); text('hello', 160 - w / 2, 120, 20, '#fff');
        window('p', 200, 20, 100, 70, () => { label('x'); progress(interpolate(f.position, [0, 20], [0, 1], 'inOut')); button('b'); separator(); checkbox('c', false); });
        rect(0, 150, spring(f.position, f.fps, 0, 200, 0) + springIn(f.position, f.fps, 0, 1, 0, 10), 10, '#0f0'); } },
    { name: 'b', frames: 20, overlap: 5, draw(f) { layer('back'); rect(0, 0, 320, 180, '#203010'); } },
  ],
  post: [{ effect: 'vignette', strength: 0.4 }, { effect: 'grain', amount: 0.02 }, { effect: 'grade', saturation: 1.1 }],
});
