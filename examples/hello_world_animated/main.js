function frame(timestamp) {
    const width = sys.window.getWidth();
    const height = sys.window.getHeight();

    const dx = Math.sin(.9 * timestamp / 500 + Math.PI / 2) * width / 4;
    const dy = Math.sin(1.1 * timestamp / 500) * height / 4;

    sys.canvas.clear('#FFFFFF');
    sys.canvas.setFillColor('#000000');
    sys.canvas.drawText('Hello World', width / 2 - 80 + dx, height / 2 + dy, 32);

    sys.animation.requestFrame(frame);
}

sys.animation.requestFrame(frame);
