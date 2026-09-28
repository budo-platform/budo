/**
 * Text Demo - Budo
 * 
 * Demonstrates text rendering with various styles, sizes, and effects.
 */

let time = 0;

// Demo text content
const texts = [
    { text: "Typography Demo", size: 48, color: '#2C3E50' },
    { text: "The quick brown fox jumps over the lazy dog", size: 24, color: '#34495E' },
    { text: "ABCDEFGHIJKLMNOPQRSTUVWXYZ", size: 20, color: '#7F8C8D' },
    { text: "abcdefghijklmnopqrstuvwxyz", size: 20, color: '#95A5A6' },
    { text: "0123456789 !@#$%^&*()", size: 20, color: '#BDC3C7' },
];

// Helper for rainbow text effect
function hslToHex(h, s, l) {
    let r, g, b;
    if (s === 0) {
        r = g = b = l;
    } else {
        const hue2rgb = (p, q, t) => {
            if (t < 0) t += 1;
            if (t > 1) t -= 1;
            if (t < 1 / 6) return p + (q - p) * 6 * t;
            if (t < 1 / 2) return q;
            if (t < 2 / 3) return p + (q - p) * (2 / 3 - t) * 6;
            return p;
        };
        const q = l < 0.5 ? l * (1 + s) : l + s - l * s;
        const p = 2 * l - q;
        r = hue2rgb(p, q, h + 1 / 3);
        g = hue2rgb(p, q, h);
        b = hue2rgb(p, q, h - 1 / 3);
    }
    const toHex = x => {
        const hex = Math.round(x * 255).toString(16);
        return hex.length === 1 ? '0' + hex : hex;
    };
    return '#' + toHex(r) + toHex(g) + toHex(b);
}

// Draw text with shadow effect
function drawTextWithShadow(text, x, y, size, color, shadowOffset = 2) {
    // Shadow
    sys.canvas.setFillColor('#00000033');
    sys.canvas.drawText(text, x + shadowOffset, y + shadowOffset, size);
    // Main text
    sys.canvas.setFillColor(color);
    sys.canvas.drawText(text, x, y, size);
}

// Draw rainbow text (each character in different color)
function drawRainbowText(text, x, y, size) {
    let offsetX = 0;
    const charWidth = size * 0.6; // Approximate character width

    for (let i = 0; i < text.length; i++) {
        const hue = (time * 0.5 + i * 0.05) % 1;
        sys.canvas.setFillColor(hslToHex(hue, 0.8, 0.5));
        sys.canvas.drawText(text[i], x + offsetX, y, size);
        offsetX += charWidth;
    }
}

// Draw bouncing text
function drawBouncingText(text, baseY, size) {
    const width = sys.window.getWidth();
    let offsetX = 50;
    const charWidth = size * 0.6;

    for (let i = 0; i < text.length; i++) {
        const bounce = Math.sin(time * 5 + i * 0.3) * 10;
        sys.canvas.setFillColor('#E74C3C');
        sys.canvas.drawText(text[i], offsetX, baseY + bounce, size);
        offsetX += charWidth;
    }
}

// Draw wavy text
function drawWavyText(text, x, baseY, size) {
    const charWidth = size * 0.6;

    for (let i = 0; i < text.length; i++) {
        const wave = Math.sin(time * 3 + i * 0.2) * 15;
        const hue = (i / text.length + time * 0.1) % 1;
        sys.canvas.setFillColor(hslToHex(hue, 0.7, 0.55));
        sys.canvas.drawText(text[i], x + i * charWidth, baseY + wave, size);
    }
}

// Draw typewriter effect
let typewriterIndex = 0;
const typewriterText = "This text appears letter by letter...";
const typewriterSpeed = 0.1; // seconds per character
let lastTypeTime = 0;

function drawTypewriter(x, y, size) {
    if (time - lastTypeTime > typewriterSpeed) {
        typewriterIndex++;
        lastTypeTime = time;
        if (typewriterIndex > typewriterText.length) {
            typewriterIndex = 0;
        }
    }

    const visibleText = typewriterText.substring(0, typewriterIndex);
    sys.canvas.setFillColor('#27AE60');
    sys.canvas.drawText(visibleText, x, y, size);

    // Cursor blink
    if (Math.floor(time * 3) % 2 === 0) {
        const cursorX = x + visibleText.length * size * 0.6;
        sys.canvas.drawText('|', cursorX, y, size);
    }
}

// Draw text sizes showcase
function drawSizeShowcase(x, baseY) {
    const sizes = [12, 16, 20, 24, 32, 40, 48];
    let y = baseY;

    for (const size of sizes) {
        sys.canvas.setFillColor('#2980B9');
        sys.canvas.drawText(`${size}px - Sample Text`, x, y, size);
        y += size + 10;
    }
}

// Main animation loop
function animate(timestamp) {
    time = timestamp / 1000;

    const width = sys.window.getWidth();
    const height = sys.window.getHeight();
    const input = sys.input.get();

    // Clear with gradient-like background
    sys.canvas.clear('#F8F9FA');

    // Title with shadow
    drawTextWithShadow('Text & Typography Demo', 20, 50, 36, '#2C3E50', 3);

    // Separator line
    sys.canvas.setStrokeColor('#DEE2E6');
    sys.canvas.setStrokeWidth(2);
    sys.canvas.drawLine(20, 70, width - 20, 70);

    // Rainbow text
    sys.canvas.setFillColor('#6C757D');
    sys.canvas.drawText('Rainbow effect:', 20, 110, 16);
    drawRainbowText('COLORFUL RAINBOW TEXT', 180, 110, 24);

    // Bouncing text
    sys.canvas.setFillColor('#6C757D');
    sys.canvas.drawText('Bouncing effect:', 20, 170, 16);
    drawBouncingText('BOUNCE!', 160, 32);

    // Wavy text
    sys.canvas.setFillColor('#6C757D');
    sys.canvas.drawText('Wave effect:', 20, 230, 16);
    drawWavyText('Wavy Smooth Text', 180, 230, 28);

    // Typewriter
    sys.canvas.setFillColor('#6C757D');
    sys.canvas.drawText('Typewriter:', 20, 300, 16);
    drawTypewriter(180, 300, 20);

    // Text with different alpha levels
    sys.canvas.setFillColor('#6C757D');
    sys.canvas.drawText('Opacity levels:', 20, 360, 16);
    const alphaLevels = [255, 200, 150, 100, 50];
    for (let i = 0; i < alphaLevels.length; i++) {
        sys.canvas.setFillColor('#3498DB');
        sys.canvas.setAlpha(alphaLevels[i]);
        sys.canvas.drawText('TEXT', 180 + i * 70, 360, 24);
    }
    sys.canvas.setAlpha(255);

    // Size showcase on the right
    sys.canvas.setFillColor('#6C757D');
    sys.canvas.drawText('Font sizes:', width - 350, 110, 16);
    sys.canvas.setStrokeColor('#DEE2E6');
    sys.canvas.setStrokeWidth(1);
    sys.canvas.drawLine(width - 350, 130, width - 30, 130);

    let sizeY = 160;
    const sizes = [12, 18, 24, 32, 48];
    for (const size of sizes) {
        sys.canvas.setFillColor('#495057');
        sys.canvas.drawText(`${size}px`, width - 350, sizeY, size);
        sizeY += size + 15;
    }

    // Instructions
    sys.canvas.setFillColor('#ADB5BD');
    sys.canvas.drawText('Press ESC to exit', 20, height - 30, 14);

    // Mouse position text display
    sys.canvas.setFillColor('#6C757D');
    sys.canvas.drawText(`Mouse: (${input.mouse.x}, ${input.mouse.y})`, width - 180, height - 30, 14);

    sys.animation.requestFrame(animate);
}

sys.log('Starting text demo...');
sys.animation.requestFrame(animate);
