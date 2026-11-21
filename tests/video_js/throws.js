video({ frames: 5, draw(f) { if (f.index === 2) { throw new Error('boom at frame 2'); } } });
