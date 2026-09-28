/**
 * Shapes Demo - Budo
 * 
 * Demonstrates all shape primitives, paths, and drawing styles.
 */

let time = 0;

// Color palette
const PALETTE = [
    '#FF6B6B', '#4ECDC4', '#45B7D1', '#96CEB4',
    '#FFEAA7', '#DDA0DD', '#98D8C8', '#F7DC6F',
    '#74B9FF', '#A29BFE', '#FD79A8', '#00B894'
];

// Draw a regular polygon
function drawPolygon(cx, cy, radius, sides, rotation = 0) {
    const path = sys.path.create();

    for (let i = 0; i < sides; i++) {
        const angle = (i / sides) * Math.PI * 2 + rotation;
        const x = cx + Math.cos(angle) * radius;
        const y = cy + Math.sin(angle) * radius;

        if (i === 0) {
            sys.path.moveTo(path, x, y);
        } else {
            sys.path.lineTo(path, x, y);
        }
    }
    sys.path.close(path);
    sys.canvas.drawPath(path);
}

// Draw a star shape
function drawStar(cx, cy, outerRadius, innerRadius, points, rotation = 0) {
    const path = sys.path.create();
    const step = Math.PI / points;

    for (let i = 0; i < points * 2; i++) {
        const radius = i % 2 === 0 ? outerRadius : innerRadius;
        const angle = i * step + rotation;
        const x = cx + Math.cos(angle) * radius;
        const y = cy + Math.sin(angle) * radius;

        if (i === 0) {
            sys.path.moveTo(path, x, y);
        } else {
            sys.path.lineTo(path, x, y);
        }
    }
    sys.path.close(path);
    sys.canvas.drawPath(path);
}

// Draw a heart shape
function drawHeart(cx, cy, size) {
    const path = sys.path.create();

    // Start at bottom point
    sys.path.moveTo(path, cx, cy + size * 0.8);

    // Left curve
    sys.path.cubicTo(path,
        cx - size * 1.2, cy + size * 0.3,  // control point 1
        cx - size * 0.7, cy - size * 0.5,   // control point 2
        cx, cy - size * 0.2                  // end point
    );

    // Right curve
    sys.path.cubicTo(path,
        cx + size * 0.7, cy - size * 0.5,   // control point 1
        cx + size * 1.2, cy + size * 0.3,   // control point 2
        cx, cy + size * 0.8                  // end point
    );

    sys.path.close(path);
    sys.canvas.drawPath(path);
}

// Draw a gear/cog shape
function drawGear(cx, cy, outerRadius, innerRadius, teeth, rotation = 0) {
    const path = sys.path.create();
    const toothAngle = (Math.PI * 2) / teeth;
    const toothDepth = (outerRadius - innerRadius) / 2;

    for (let i = 0; i < teeth; i++) {
        const angle = i * toothAngle + rotation;
        const nextAngle = (i + 1) * toothAngle + rotation;

        // Outer point
        const x1 = cx + Math.cos(angle) * outerRadius;
        const y1 = cy + Math.sin(angle) * outerRadius;

        // Top of tooth
        const midAngle = angle + toothAngle * 0.3;
        const x2 = cx + Math.cos(midAngle) * outerRadius;
        const y2 = cy + Math.sin(midAngle) * outerRadius;

        // Valley
        const valleyAngle = angle + toothAngle * 0.5;
        const x3 = cx + Math.cos(valleyAngle) * innerRadius;
        const y3 = cy + Math.sin(valleyAngle) * innerRadius;

        // Other side of tooth
        const midAngle2 = angle + toothAngle * 0.7;
        const x4 = cx + Math.cos(midAngle2) * outerRadius;
        const y4 = cy + Math.sin(midAngle2) * outerRadius;

        if (i === 0) {
            sys.path.moveTo(path, x1, y1);
        }

        sys.path.lineTo(path, x2, y2);
        sys.path.lineTo(path, x3, y3);
        sys.path.lineTo(path, x4, y4);
    }

    sys.path.close(path);
    sys.canvas.drawPath(path);
}

// Draw a spiral
function drawSpiral(cx, cy, startRadius, endRadius, turns, rotation = 0) {
    const path = sys.path.create();
    const points = turns * 36; // 36 points per turn

    for (let i = 0; i <= points; i++) {
        const t = i / points;
        const angle = t * turns * Math.PI * 2 + rotation;
        const radius = startRadius + (endRadius - startRadius) * t;
        const x = cx + Math.cos(angle) * radius;
        const y = cy + Math.sin(angle) * radius;

        if (i === 0) {
            sys.path.moveTo(path, x, y);
        } else {
            sys.path.lineTo(path, x, y);
        }
    }

    sys.canvas.drawPath(path);
}

// Draw bezier curve demo
function drawBezierDemo(x, y, width, height) {
    // Control points that animate
    const cp1x = x + width * 0.25 + Math.sin(time * 2) * 30;
    const cp1y = y + Math.cos(time * 1.5) * 40;
    const cp2x = x + width * 0.75 + Math.cos(time * 2.5) * 30;
    const cp2y = y + height + Math.sin(time * 1.8) * 40;

    // Draw control lines
    sys.canvas.setStrokeColor('#88888844');
    sys.canvas.setStrokeWidth(1);
    sys.canvas.drawLine(x, y + height / 2, cp1x, cp1y);
    sys.canvas.drawLine(cp1x, cp1y, cp2x, cp2y);
    sys.canvas.drawLine(cp2x, cp2y, x + width, y + height / 2);

    // Draw curve
    const path = sys.path.create();
    sys.path.moveTo(path, x, y + height / 2);
    sys.path.cubicTo(path, cp1x, cp1y, cp2x, cp2y, x + width, y + height / 2);

    sys.canvas.setStrokeColor(PALETTE[0]);
    sys.canvas.setStrokeWidth(3);
    sys.canvas.drawPath(path);

    // Draw control points
    sys.canvas.setFillColor(PALETTE[4]);
    sys.canvas.drawCircle(cp1x, cp1y, 6);
    sys.canvas.drawCircle(cp2x, cp2y, 6);

    // End points
    sys.canvas.setFillColor(PALETTE[2]);
    sys.canvas.drawCircle(x, y + height / 2, 6);
    sys.canvas.drawCircle(x + width, y + height / 2, 6);
}

// Draw a section header
function drawSectionHeader(text, x, y, width) {
    sys.canvas.setFillColor('#333');
    sys.canvas.drawText(text, x, y, 18);

    sys.canvas.setStrokeColor('#ddd');
    sys.canvas.setStrokeWidth(1);
    sys.canvas.drawLine(x, y + 8, x + width, y + 8);
}

// Main animation loop
function animate(timestamp) {
    time = timestamp / 1000;

    const width = sys.window.getWidth();
    const height = sys.window.getHeight();
    const input = sys.input.get();

    // Clear
    sys.canvas.clear('#F5F5F5');

    // Title
    sys.canvas.setFillColor('#2C3E50');
    sys.canvas.drawText('Shapes & Paths Gallery', 20, 40, 28);

    // =====================
    // Basic Shapes Section
    // =====================
    drawSectionHeader('Basic Shapes', 20, 80, 350);

    const shapeY = 130;
    const shapeSpacing = 90;
    let shapeX = 60;

    // Rectangle
    sys.canvas.setFillColor(PALETTE[0]);
    sys.canvas.drawRect(shapeX - 30, shapeY - 25, 60, 50);
    sys.canvas.setFillColor('#333');
    sys.canvas.drawText('Rect', shapeX - 15, shapeY + 50, 12);
    shapeX += shapeSpacing;

    // Rounded Rectangle
    sys.canvas.setFillColor(PALETTE[1]);
    sys.canvas.drawRoundRect(shapeX - 30, shapeY - 25, 60, 50, 10, 10);
    sys.canvas.setFillColor('#333');
    sys.canvas.drawText('RoundRect', shapeX - 30, shapeY + 50, 12);
    shapeX += shapeSpacing;

    // Circle
    sys.canvas.setFillColor(PALETTE[2]);
    sys.canvas.drawCircle(shapeX, shapeY, 30);
    sys.canvas.setFillColor('#333');
    sys.canvas.drawText('Circle', shapeX - 18, shapeY + 50, 12);
    shapeX += shapeSpacing;

    // Oval
    sys.canvas.setFillColor(PALETTE[3]);
    sys.canvas.drawOval(shapeX - 35, shapeY - 20, 70, 40);
    sys.canvas.setFillColor('#333');
    sys.canvas.drawText('Oval', shapeX - 12, shapeY + 50, 12);

    // =====================
    // Polygons Section
    // =====================
    drawSectionHeader('Polygons (Animated)', 20, 200, 350);

    const polyY = 270;
    const polySpacing = 85;
    let polyX = 60;

    // Triangle
    sys.canvas.setFillColor(PALETTE[4]);
    drawPolygon(polyX, polyY, 30, 3, time * 0.5);
    sys.canvas.setFillColor('#333');
    sys.canvas.drawText('Triangle', polyX - 22, polyY + 50, 12);
    polyX += polySpacing;

    // Pentagon
    sys.canvas.setFillColor(PALETTE[5]);
    drawPolygon(polyX, polyY, 30, 5, time * 0.4);
    sys.canvas.setFillColor('#333');
    sys.canvas.drawText('Pentagon', polyX - 26, polyY + 50, 12);
    polyX += polySpacing;

    // Hexagon
    sys.canvas.setFillColor(PALETTE[6]);
    drawPolygon(polyX, polyY, 30, 6, time * 0.3);
    sys.canvas.setFillColor('#333');
    sys.canvas.drawText('Hexagon', polyX - 24, polyY + 50, 12);
    polyX += polySpacing;

    // Octagon
    sys.canvas.setFillColor(PALETTE[7]);
    drawPolygon(polyX, polyY, 30, 8, time * 0.2);
    sys.canvas.setFillColor('#333');
    sys.canvas.drawText('Octagon', polyX - 24, polyY + 50, 12);

    // =====================
    // Stars Section
    // =====================
    drawSectionHeader('Stars', 20, 350, 350);

    const starY = 420;
    const starSpacing = 90;
    let starX = 60;

    // 4-point star
    sys.canvas.setFillColor(PALETTE[8]);
    drawStar(starX, starY, 35, 15, 4, time);
    sys.canvas.setFillColor('#333');
    sys.canvas.drawText('4-Point', starX - 20, starY + 55, 12);
    starX += starSpacing;

    // 5-point star
    sys.canvas.setFillColor(PALETTE[9]);
    drawStar(starX, starY, 35, 15, 5, -time * 0.5);
    sys.canvas.setFillColor('#333');
    sys.canvas.drawText('5-Point', starX - 20, starY + 55, 12);
    starX += starSpacing;

    // 6-point star
    sys.canvas.setFillColor(PALETTE[10]);
    drawStar(starX, starY, 35, 18, 6, time * 0.3);
    sys.canvas.setFillColor('#333');
    sys.canvas.drawText('6-Point', starX - 20, starY + 55, 12);
    starX += starSpacing;

    // 8-point star
    sys.canvas.setFillColor(PALETTE[11]);
    drawStar(starX, starY, 35, 20, 8, -time * 0.2);
    sys.canvas.setFillColor('#333');
    sys.canvas.drawText('8-Point', starX - 20, starY + 55, 12);

    // =====================
    // Special Shapes Section
    // =====================
    drawSectionHeader('Special Shapes', 400, 80, 350);

    // Heart (pulsing)
    const heartScale = 1 + Math.sin(time * 3) * 0.1;
    sys.canvas.save();
    sys.canvas.translate(480, 150);
    sys.canvas.scale(heartScale, heartScale);
    sys.canvas.setFillColor('#E74C3C');
    drawHeart(0, 0, 30);
    sys.canvas.restore();
    sys.canvas.setFillColor('#333');
    sys.canvas.drawText('Heart', 457, 205, 12);

    // Gear
    sys.canvas.setFillColor(PALETTE[7]);
    drawGear(600, 150, 35, 25, 8, time);
    sys.canvas.setFillColor('#333');
    sys.canvas.drawText('Gear', 582, 205, 12);

    // Arc
    sys.canvas.setFillColor(PALETTE[1]);
    sys.canvas.drawArc(670, 115, 70, 70, 0, 270 + Math.sin(time) * 45, true);
    sys.canvas.setFillColor('#333');
    sys.canvas.drawText('Arc', 693, 205, 12);

    // =====================
    // Curves Section
    // =====================
    drawSectionHeader('Bezier Curves (Interactive)', 400, 230, 350);

    drawBezierDemo(420, 260, 300, 80);

    // =====================
    // Stroke Styles Section
    // =====================
    drawSectionHeader('Stroke Styles', 400, 370, 350);

    const strokeY = 410;

    // Different stroke widths
    sys.canvas.setStrokeColor(PALETTE[0]);
    for (let i = 0; i < 5; i++) {
        sys.canvas.setStrokeWidth(1 + i * 2);
        sys.canvas.drawLine(420, strokeY + i * 20, 520, strokeY + i * 20);
    }
    sys.canvas.setFillColor('#333');
    sys.canvas.drawText('Widths', 450, strokeY + 110, 12);

    // Stroke caps
    sys.canvas.setStrokeWidth(8);
    sys.canvas.setStrokeColor(PALETTE[2]);

    sys.canvas.setStrokeCap('butt');
    sys.canvas.drawLine(550, strokeY, 620, strokeY);
    sys.canvas.setFillColor('#888');
    sys.canvas.drawText('butt', 630, strokeY + 5, 10);

    sys.canvas.setStrokeCap('round');
    sys.canvas.drawLine(550, strokeY + 25, 620, strokeY + 25);
    sys.canvas.setFillColor('#888');
    sys.canvas.drawText('round', 630, strokeY + 30, 10);

    sys.canvas.setStrokeCap('square');
    sys.canvas.drawLine(550, strokeY + 50, 620, strokeY + 50);
    sys.canvas.setFillColor('#888');
    sys.canvas.drawText('square', 630, strokeY + 55, 10);

    sys.canvas.setFillColor('#333');
    sys.canvas.drawText('Line Caps', 565, strokeY + 110, 12);

    // =====================
    // Spiral
    // =====================
    drawSectionHeader('Spiral', 400, 530, 200);

    sys.canvas.setStrokeColor(PALETTE[10]);
    sys.canvas.setStrokeWidth(2);
    drawSpiral(500, 600, 5, 50, 4, time);

    // =====================
    // Combined shape
    // =====================
    drawSectionHeader('Composition', 630, 530, 150);

    // Layered circles
    const compX = 700;
    const compY = 600;
    for (let i = 5; i >= 0; i--) {
        const hue = (time * 0.2 + i * 0.1) % 1;
        const alpha = Math.floor(180 + 75 * (i / 5));
        sys.canvas.setFillColor(PALETTE[i % PALETTE.length]);
        sys.canvas.setAlpha(alpha);
        sys.canvas.drawCircle(compX + Math.cos(time + i) * 20, compY + Math.sin(time + i) * 20, 15 + i * 5);
    }
    sys.canvas.setAlpha(255);

    // Instructions
    sys.canvas.setFillColor('#666');
    sys.canvas.drawText('Animated shapes showcase | Press ESC to exit', 20, height - 20, 14);

    // FPS
    sys.canvas.setFillColor('#999');
    sys.canvas.drawText(`Frame: ${input.frameCount}`, width - 120, height - 20, 12);

    sys.animation.requestFrame(animate);
}

sys.log('Starting shapes demo...');
sys.animation.requestFrame(animate);
