/**
 * Ruler Example
 *
 * Displays a centimeter ruler using sys.window.getDisplayDensity() to convert
 * physical centimeters to pixels.  If density is correctly reported
 * the markings should match a real ruler held against the screen.
 *
 * Standard assumption: 1 inch = 2.54 cm, baseline DPI = 160 on
 * Android (mdpi) and 72 on macOS/desktop (though SDL typically
 * reports density = 1.0 on desktop).
 *
 * Formula used here (Android convention):
 *   1 dp  = density pixels
 *   1 inch = 160 dp  →  pixelsPerInch = 160 * density
 *   1 cm   = pixelsPerInch / 2.54
 */

const RULER_MARGIN = 40;        // px from top
const RULER_HEIGHT = 120;       // px tall
const TICK_CM = 80;         // full-cm tick height
const TICK_HALF = 50;         // half-cm tick height
const TICK_MM = 30;         // mm tick height
const BG_COLOR = '#F5F5F5';
const RULER_COLOR = '#FFFDE7';
const TICK_COLOR = '#333333';
const TEXT_COLOR = '#222222';
const ACCENT = '#E65100';

function animate(timestamp) {
    const W = sys.window.getWidth();
    const H = sys.window.getHeight();
    const density = sys.window.getDisplayDensity();

    // Pixels per centimeter
    const pixelsPerInch = 160 * density;   // Android dp model
    const pxPerCm = pixelsPerInch / 2.54;

    // How many full centimeters fit on screen (with some margin)
    const usableWidth = W - RULER_MARGIN * 2;
    const totalCm = Math.floor(usableWidth / pxPerCm);

    // ── background ──
    sys.canvas.clear(BG_COLOR);

    // ── ruler body ──
    const rulerX = RULER_MARGIN;
    const rulerY = RULER_MARGIN;
    const rulerW = totalCm * pxPerCm;

    sys.canvas.setFillColor(RULER_COLOR);
    sys.canvas.drawRect(rulerX, rulerY, rulerW, RULER_HEIGHT);

    sys.canvas.setStrokeColor(TICK_COLOR);
    sys.canvas.setStrokeWidth(1);
    sys.canvas.drawRect(rulerX, rulerY, rulerW, RULER_HEIGHT);

    // ── ticks and labels ──
    sys.canvas.setStrokeColor(TICK_COLOR);
    for (let mm = 0; mm <= totalCm * 10; mm++) {
        const x = rulerX + mm * (pxPerCm / 10);
        let tickH;
        let sw;

        if (mm % 10 === 0) {
            tickH = TICK_CM;
            sw = 2;
        } else if (mm % 5 === 0) {
            tickH = TICK_HALF;
            sw = 1.5;
        } else {
            tickH = TICK_MM;
            sw = 1;
        }

        sys.canvas.setStrokeWidth(sw);
        sys.canvas.drawLine(x, rulerY + RULER_HEIGHT, x, rulerY + RULER_HEIGHT - tickH);

        // Draw cm number at every full centimeter
        if (mm % 10 === 0) {
            const cm = mm / 10;
            sys.canvas.setFillColor(TEXT_COLOR);
            sys.canvas.drawText(String(cm), x + 4, rulerY + RULER_HEIGHT - TICK_CM + 16, 14);
        }
    }

    // ── info panel below ruler ──
    const infoY = rulerY + RULER_HEIGHT + 40;
    sys.canvas.setFillColor(TEXT_COLOR);
    sys.canvas.drawText('Ruler  —  hold a real ruler against the screen to verify', RULER_MARGIN, infoY, 18);
    sys.canvas.drawText('density = ' + density.toFixed(2), RULER_MARGIN, infoY + 30, 16);
    sys.canvas.drawText('px/inch = ' + pixelsPerInch.toFixed(1), RULER_MARGIN, infoY + 55, 16);
    sys.canvas.drawText('px/cm   = ' + pxPerCm.toFixed(1), RULER_MARGIN, infoY + 80, 16);
    sys.canvas.drawText('screen  = ' + W + ' x ' + H + ' px', RULER_MARGIN, infoY + 105, 16);
    sys.canvas.drawText('ruler length = ' + totalCm + ' cm', RULER_MARGIN, infoY + 130, 16);

    // ── accent line at 0 ──
    sys.canvas.setStrokeColor(ACCENT);
    sys.canvas.setStrokeWidth(3);
    sys.canvas.drawLine(rulerX, rulerY, rulerX, rulerY + RULER_HEIGHT);

    sys.animation.requestFrame(animate);
}

sys.animation.requestFrame(animate);
