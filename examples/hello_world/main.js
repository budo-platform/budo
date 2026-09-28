const width = sys.window.getWidth();
const height = sys.window.getHeight();

sys.canvas.clear('#FFFFFF');
sys.canvas.setFillColor('#000000');
sys.canvas.drawText('Hello World!', width / 2 - 80, height / 2, 32);
