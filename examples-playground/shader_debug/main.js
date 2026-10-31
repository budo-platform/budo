// Shader debug demo — tests the GL shader pipeline step by step.
// Draws Skia content, then applies a simple color-tint shader.
// If you see a red-tinted scene, shaders work.
// If you see normal Skia content (no tint), shaders failed to load but Skia works.
// If you see a black screen, the whole pipeline is broken.

let shaderProgram = null;
let shaderError = null;

// Get project directory for diagnostics
const projectDir = sys.gl.getProjectDir ? sys.gl.getProjectDir() : 'N/A';
sys.log('Project directory: ' + projectDir);

try {
    shaderProgram = sys.gl.createProgram('shader.vert', 'shader.frag');
    sys.log('Shader program created: id=' + shaderProgram);
} catch (e) {
    shaderError = '' + e;
    sys.log('Shader creation failed: ' + shaderError);
}

function frame(timestamp) {
    const width = sys.window.getWidth();
    const height = sys.window.getHeight();

    // Scale text based on screen height for readability on Android
    // Aim for ~24 lines of text at minimum
    const baseSize = Math.max(14, Math.floor(height / 35));
    const titleSize = Math.floor(baseSize * 1.5);
    const smallSize = Math.floor(baseSize * 0.9);

    sys.canvas.clear('#FFFFFF');

    // Draw some Skia content
    sys.canvas.setFillColor('#3366CC');
    sys.canvas.drawRect(50, 50, width - 100, height - 100);

    sys.canvas.setFillColor('#FF6600');
    sys.canvas.drawCircle(width / 2, height / 2, Math.min(100, height / 8));

    // Status text
    let y = 30;
    const lineHeight = baseSize + 6;

    sys.canvas.setFillColor('#000000');
    sys.canvas.drawText('Shader Debug Demo', 20, y, titleSize);
    y += lineHeight * 1.5;

    // Show project directory (truncate if too long)
    let dirDisplay = projectDir;
    if (dirDisplay.length > 40) {
        dirDisplay = '...' + dirDisplay.slice(-37);
    }
    sys.canvas.setFillColor('#666666');
    sys.canvas.drawText('Dir: ' + dirDisplay, 20, y, smallSize);
    y += lineHeight;

    sys.canvas.setFillColor('#000000');

    if (shaderProgram > 0) {
        sys.canvas.drawText('Mode: File-based shaders', 20, y, baseSize);
        y += lineHeight;

        sys.canvas.setFillColor('#228B22');
        sys.canvas.drawText('OK: Shader loaded (id=' + shaderProgram + ')', 20, y, baseSize);
        y += lineHeight;
        sys.canvas.drawText('Applying... (red tint expected)', 20, y, baseSize);
        y += lineHeight;

        // Apply the shader
        sys.gl.bindScreen();
        sys.gl.drawFullscreen(shaderProgram);
    } else {
        sys.canvas.drawText('Mode: File-based shaders', 20, y, baseSize);
        y += lineHeight;

        sys.canvas.setFillColor('#CC0000');
        // Show error on multiple lines if needed
        const errMsg = shaderError || 'unknown error';
        sys.canvas.drawText('FAIL: ' + errMsg.slice(0, 50), 20, y, baseSize);
        y += lineHeight;
        if (errMsg.length > 50) {
            sys.canvas.drawText(errMsg.slice(50, 100), 20, y, smallSize);
            y += lineHeight;
            if (errMsg.length > 100) {
                sys.canvas.drawText(errMsg.slice(100, 150), 20, y, smallSize);
                y += lineHeight;
            }
        }

        sys.canvas.setFillColor('#000000');
        sys.canvas.drawText('(Skia drawing should be visible)', 20, y, smallSize);
        y += lineHeight;
    }

    // Instructions at bottom
    sys.canvas.setFillColor('#333333');
    sys.canvas.drawText('File-based shader pipeline diagnostic', 20, height - lineHeight * 2, smallSize);
    sys.canvas.drawText('Screen: ' + width + 'x' + height, 20, height - lineHeight, smallSize);

    sys.animation.requestFrame(frame);
}

sys.animation.requestFrame(frame);
