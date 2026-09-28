/**
 * Layout Demo - Budo
 *
 * Demonstrates layout techniques: grids, panels, cards, and responsive design.
 * Includes a small set of reusable animation/UI helpers (the `ui` namespace)
 * intended to be extracted into a library later.
 */

// =====================================================================
// Color palette
// =====================================================================
const COLORS = {
    bg: '#1E1E2E',
    bgGradient: '#181825',
    panel: '#313244',
    panelHover: '#45475A',
    accent: '#89B4FA',
    accent2: '#A6E3A1',
    accent3: '#F9E2AF',
    accent4: '#F38BA8',
    accent5: '#CBA6F7',
    text: '#CDD6F4',
    textMuted: '#6C7086',
    border: '#45475A',
};

// =====================================================================
// `ui` — reusable animation / UI helpers (candidate for extraction)
// =====================================================================
const ui = (() => {
    // ---- math / easing ----
    const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
    const lerp = (a, b, t) => a + (b - a) * t;
    const easeOutCubic = (t) => 1 - Math.pow(1 - t, 3);
    const easeInOutQuad = (t) => t < 0.5 ? 2 * t * t : 1 - Math.pow(-2 * t + 2, 2) / 2;

    // Frame-rate independent smoothing factor for a given half-life (in seconds).
    // Use as: value = lerp(value, target, smoothing(halfLife, dt));
    const smoothing = (halfLife, dt) => 1 - Math.pow(0.5, dt / Math.max(0.0001, halfLife));

    // ---- Smoothed (critically damped) scalar value ----
    class Smoothed {
        constructor(initial = 0, halfLife = 0.15) {
            this.value = initial;
            this.target = initial;
            this.halfLife = halfLife;
        }
        set(target) { this.target = target; }
        snap(value) { this.value = this.target = value; }
        update(dt) {
            this.value = lerp(this.value, this.target, smoothing(this.halfLife, dt));
            return this.value;
        }
    }

    // ---- Deterministic noise: smooth, repeatable, no flicker ----
    // Layered sine waves; cheap and continuous, perfect for chart data.
    const noise1D = (x, seed = 0) => {
        const a = Math.sin(x * 1.0 + seed * 12.9898) * 0.5;
        const b = Math.sin(x * 2.3 + seed * 7.233) * 0.3;
        const c = Math.sin(x * 5.1 + seed * 3.111) * 0.2;
        return (a + b + c) * 0.5 + 0.5; // normalized to ~0..1
    };

    // ---- Ripple manager (click feedback) ----
    class RippleSet {
        constructor() { this.list = []; }
        spawn(x, y, color, radius = 60, life = 0.6) {
            this.list.push({ x, y, color, radius, life, age: 0 });
        }
        update(dt) {
            for (const r of this.list) r.age += dt;
            this.list = this.list.filter(r => r.age < r.life);
        }
        draw() {
            for (const r of this.list) {
                const t = r.age / r.life;
                const radius = r.radius * easeOutCubic(t);
                const alpha = Math.round((1 - t) * 120);
                sys.canvas.setStrokeColor(r.color + alpha.toString(16).padStart(2, '0'));
                sys.canvas.setStrokeWidth(2);
                sys.canvas.drawCircle(r.x, r.y, radius);
            }
        }
    }

    // ---- Color helpers ----
    const withAlpha = (hex, alpha0to1) => {
        const a = clamp(Math.round(alpha0to1 * 255), 0, 255);
        return hex + a.toString(16).padStart(2, '0');
    };

    // ---- Hit-test helpers ----
    const pointInRect = (px, py, x, y, w, h) =>
        px >= x && px <= x + w && py >= y && py <= y + h;

    return {
        clamp, lerp, easeOutCubic, easeInOutQuad, smoothing,
        Smoothed, noise1D, RippleSet, withAlpha, pointInRect,
    };
})();

// =====================================================================
// Animation / interaction state
// =====================================================================
let time = 0;
let lastTimestamp = 0;
let appAge = 0;          // seconds since startup, for entry animations
let hoveredPanel = -1;
let selectedTab = 0;
let prevSelectedTab = 0;

const ripples = new ui.RippleSet();

// Smoothed UI values
const tabIndicatorX = new ui.Smoothed(0, 0.08);
const tabIndicatorW = new ui.Smoothed(80, 0.08);
const panelHoverLift = new Map(); // index -> Smoothed

// Per-card animated values
const cardValues = [
    { current: new ui.Smoothed(0, 0.6), target: 12456, format: v => '$' + Math.round(v).toLocaleString(), color: COLORS.accent, delta: '+12%', deltaColor: COLORS.accent2 },
    { current: new ui.Smoothed(0, 0.6), target: 1234, format: v => Math.round(v).toLocaleString(), color: COLORS.accent2, delta: '+8%', deltaColor: COLORS.accent2 },
    { current: new ui.Smoothed(0, 0.6), target: 567, format: v => Math.round(v).toLocaleString(), color: COLORS.accent3, delta: '-3%', deltaColor: COLORS.accent4 },
    { current: new ui.Smoothed(0, 0.6), target: 15, format: v => '+' + Math.round(v) + '%', color: COLORS.accent4, delta: '+5%', deltaColor: COLORS.accent2 },
];
for (const c of cardValues) c.current.set(c.target);

// Performance bar smoothed values
const perfMetrics = [
    { label: 'CPU', base: 0.65, color: COLORS.accent, value: new ui.Smoothed(0, 0.4) },
    { label: 'Memory', base: 0.45, color: COLORS.accent2, value: new ui.Smoothed(0, 0.4) },
    { label: 'Disk', base: 0.30, color: COLORS.accent3, value: new ui.Smoothed(0, 0.4) },
    { label: 'Network', base: 0.80, color: COLORS.accent4, value: new ui.Smoothed(0, 0.4) },
];

// =====================================================================
// Panel
// =====================================================================
class Panel {
    constructor(x, y, width, height, title, color = COLORS.panel) {
        this.x = x;
        this.y = y;
        this.width = width;
        this.height = height;
        this.title = title;
        this.color = color;
        this.padding = 18;
        this.headerHeight = 48;
    }

    draw(index) {
        const isHovered = hoveredPanel === index;

        // Smoothed hover lift
        let lift = panelHoverLift.get(index);
        if (!lift) { lift = new ui.Smoothed(0, 0.08); panelHoverLift.set(index, lift); }
        lift.set(isHovered ? 1 : 0);
        const liftAmount = lift.value * 4;

        const drawX = this.x;
        const drawY = this.y - liftAmount;

        // Glow / shadow when hovered
        if (lift.value > 0.01) {
            sys.canvas.setFillColor(ui.withAlpha('#000000', 0.25 * lift.value));
            sys.canvas.drawRoundRect(drawX + 2, drawY + 6, this.width, this.height, 8, 8);
        }

        // Panel background
        const bgColor = isHovered ? COLORS.panelHover : this.color;
        sys.canvas.setFillColor(bgColor);
        sys.canvas.drawRoundRect(drawX, drawY, this.width, this.height, 8, 8);

        // Accent top border that grows in on hover
        if (lift.value > 0.01) {
            sys.canvas.setFillColor(ui.withAlpha(COLORS.accent, lift.value));
            sys.canvas.drawRoundRect(drawX, drawY, this.width, 2, 2, 2);
        }

        // Panel border
        sys.canvas.setStrokeColor(COLORS.border);
        sys.canvas.setStrokeWidth(1);
        sys.canvas.drawRoundRect(drawX, drawY, this.width, this.height, 8, 8);

        // Header line
        sys.canvas.setStrokeColor(COLORS.border);
        sys.canvas.drawLine(drawX, drawY + this.headerHeight, drawX + this.width, drawY + this.headerHeight);

        // Title
        sys.canvas.setFillColor(COLORS.text);
        sys.canvas.drawText(this.title, drawX + this.padding, drawY + 32, 20);

        return {
            contentX: drawX + this.padding,
            contentY: drawY + this.headerHeight + this.padding,
            contentWidth: this.width - this.padding * 2,
            contentHeight: this.height - this.headerHeight - this.padding * 2,
            drawX, drawY,
        };
    }

    contains(mx, my) {
        return ui.pointInRect(mx, my, this.x, this.y, this.width, this.height);
    }
}

// =====================================================================
// Layout
// =====================================================================
function createDashboardLayout(width, height) {
    const margin = 20;
    const gap = 15;
    const navHeight = 50;
    const sidebarWidth = 200;

    const contentX = margin + sidebarWidth + gap;
    const contentY = margin + navHeight + gap;
    const contentWidth = width - contentX - margin;
    const contentHeight = height - contentY - margin;

    const panels = [];

    const cardWidth = (contentWidth - gap * 3) / 4;
    const cardHeight = 130;

    for (let i = 0; i < 4; i++) {
        panels.push(new Panel(
            contentX + i * (cardWidth + gap),
            contentY,
            cardWidth,
            cardHeight,
            ['Revenue', 'Users', 'Orders', 'Growth'][i]
        ));
    }

    const chartY = contentY + cardHeight + gap;
    const chartHeight = (contentHeight - cardHeight - gap * 2) * 0.6;
    panels.push(new Panel(contentX, chartY,
        contentWidth * 0.65 - gap / 2, chartHeight, 'Analytics Chart'));
    panels.push(new Panel(contentX + contentWidth * 0.65 + gap / 2, chartY,
        contentWidth * 0.35 - gap / 2, chartHeight, 'Recent Activity'));

    const bottomY = chartY + chartHeight + gap;
    const bottomHeight = contentHeight - cardHeight - chartHeight - gap * 2;
    panels.push(new Panel(contentX, bottomY,
        contentWidth * 0.5 - gap / 2, bottomHeight, 'Tasks'));
    panels.push(new Panel(contentX + contentWidth * 0.5 + gap / 2, bottomY,
        contentWidth * 0.5 - gap / 2, bottomHeight, 'Performance'));

    return { panels, navHeight, sidebarWidth, margin, gap, contentX, contentY };
}

// =====================================================================
// Background
// =====================================================================
function drawBackground(width, height) {
    // Solid base
    sys.canvas.clear(COLORS.bg);

    // Subtle radial vignette using stacked translucent rings.
    const cx = width * 0.5;
    const cy = height * 0.4;
    const maxR = Math.max(width, height) * 0.9;
    const rings = 6;
    for (let i = rings; i > 0; i--) {
        const t = i / rings;
        sys.canvas.setFillColor(ui.withAlpha(COLORS.bgGradient, 0.08 * t));
        sys.canvas.drawCircle(cx, cy, maxR * t);
    }
}

// =====================================================================
// Navigation bar (with sliding indicator)
// =====================================================================
const TABS = ['Overview', 'Analytics', 'Reports', 'Settings'];
const TAB_WIDTH = 80;
const TAB_GAP = 20;
const TAB_BASE_X_OFFSET = 180; // from margin

function navTabRect(margin, i) {
    return {
        x: margin + TAB_BASE_X_OFFSET + i * (TAB_WIDTH + TAB_GAP) - 10,
        y: margin + 10,
        w: TAB_WIDTH,
        h: 30,
    };
}

function drawNavBar(width, navHeight, margin) {
    sys.canvas.setFillColor(COLORS.panel);
    sys.canvas.drawRoundRect(margin, margin, width - margin * 2, navHeight, 8, 8);

    // Logo/Title with subtle pulse
    const pulse = 0.85 + Math.sin(time * 2) * 0.15;
    sys.canvas.setFillColor(ui.withAlpha(COLORS.accent, pulse));
    sys.canvas.drawCircle(margin + 14, margin + 25, 6);
    sys.canvas.setFillColor(COLORS.accent);
    sys.canvas.drawText('Dashboard', margin + 30, margin + 33, 24);

    // Sliding tab indicator
    const target = navTabRect(margin, selectedTab);
    tabIndicatorX.set(target.x);
    tabIndicatorW.set(target.w);
    sys.canvas.setFillColor(ui.withAlpha(COLORS.accent, 0.18));
    sys.canvas.drawRoundRect(tabIndicatorX.value, target.y, tabIndicatorW.value, target.h, 6, 6);

    // Tab labels
    for (let i = 0; i < TABS.length; i++) {
        const r = navTabRect(margin, i);
        const isSelected = selectedTab === i;
        sys.canvas.setFillColor(isSelected ? COLORS.accent : COLORS.textMuted);
        sys.canvas.drawText(TABS[i], r.x + 10, r.y + 23, 17);
    }

    // Time display
    const now = new Date();
    const timeStr = `${now.getHours().toString().padStart(2, '0')}:${now.getMinutes().toString().padStart(2, '0')}:${now.getSeconds().toString().padStart(2, '0')}`;
    sys.canvas.setFillColor(COLORS.textMuted);
    sys.canvas.drawText(timeStr, width - margin - 100, margin + 33, 17);
}

// =====================================================================
// Sidebar
// =====================================================================
const SIDEBAR_ITEMS = [
    { icon: '◉', label: 'Dashboard', active: true },
    { icon: '◎', label: 'Projects' },
    { icon: '◇', label: 'Team' },
    { icon: '◈', label: 'Calendar' },
    { icon: '◆', label: 'Documents' },
    { icon: '◇', label: 'Reports' },
];

function drawSidebar(sidebarWidth, navHeight, height, margin, gap, mouseX, mouseY) {
    const sidebarY = margin + navHeight + gap;
    const sidebarHeight = height - sidebarY - margin;

    sys.canvas.setFillColor(COLORS.panel);
    sys.canvas.drawRoundRect(margin, sidebarY, sidebarWidth, sidebarHeight, 8, 8);

    let itemY = sidebarY + 20;
    for (const item of SIDEBAR_ITEMS) {
        const itemX = margin + 10;
        const itemW = sidebarWidth - 20;
        const itemH = 35;
        const isHovered = ui.pointInRect(mouseX, mouseY, itemX, itemY - 5, itemW, itemH);

        if (item.active) {
            sys.canvas.setFillColor(ui.withAlpha(COLORS.accent, 0.14));
            sys.canvas.drawRoundRect(itemX, itemY - 5, itemW, itemH, 5, 5);
            // Active indicator bar
            sys.canvas.setFillColor(COLORS.accent);
            sys.canvas.drawRoundRect(itemX, itemY - 5, 3, itemH, 2, 2);
        } else if (isHovered) {
            sys.canvas.setFillColor(ui.withAlpha(COLORS.text, 0.06));
            sys.canvas.drawRoundRect(itemX, itemY - 5, itemW, itemH, 5, 5);
        }

        sys.canvas.setFillColor(item.active ? COLORS.accent : COLORS.textMuted);
        sys.canvas.drawText(item.icon, margin + 20, itemY + 20, 19);
        sys.canvas.drawText(item.label, margin + 50, itemY + 20, 17);

        itemY += 50;
    }

    // Divider
    sys.canvas.setStrokeColor(COLORS.border);
    sys.canvas.setStrokeWidth(1);
    sys.canvas.drawLine(margin + 15, sidebarY + sidebarHeight - 70,
        margin + sidebarWidth - 15, sidebarY + sidebarHeight - 70);

    // User avatar with breathing ring
    const ringPulse = 15 + Math.sin(time * 2) * 1.5;
    sys.canvas.setStrokeColor(ui.withAlpha(COLORS.accent2, 0.5));
    sys.canvas.setStrokeWidth(1.5);
    sys.canvas.drawCircle(margin + 30, sidebarY + sidebarHeight - 35, ringPulse + 3);

    sys.canvas.setFillColor(COLORS.accent2);
    sys.canvas.drawCircle(margin + 30, sidebarY + sidebarHeight - 35, ringPulse);

    sys.canvas.setFillColor(COLORS.text);
    sys.canvas.drawText('John Doe', margin + 55, sidebarY + sidebarHeight - 40, 17);
    sys.canvas.setFillColor(COLORS.textMuted);
    sys.canvas.drawText('Admin', margin + 55, sidebarY + sidebarHeight - 20, 14);
}

// =====================================================================
// Stat card content (with mini sparkline + animated count-up)
// =====================================================================
function drawStatCard(panel, index, content) {
    const card = cardValues[index];
    card.current.set(card.target);

    // Animated value
    sys.canvas.setFillColor(card.color);
    sys.canvas.drawText(card.format(card.current.value),
        content.contentX, content.contentY + 30, 32);

    // Change indicator
    sys.canvas.setFillColor(card.deltaColor);
    sys.canvas.drawText(card.delta, content.contentX, content.contentY + 58, 16);

    // Mini sparkline on the right side of the card
    const sparkW = Math.min(80, content.contentWidth * 0.45);
    const sparkH = content.contentHeight - 10;
    const sparkX = content.contentX + content.contentWidth - sparkW;
    const sparkY = content.contentY;
    drawSparkline(sparkX, sparkY, sparkW, sparkH, index, card.color);
}

function drawSparkline(x, y, w, h, seed, color) {
    const points = 16;
    const path = sys.path.create();
    for (let i = 0; i < points; i++) {
        const u = i / (points - 1);
        // Slowly drifting smooth noise; no flicker.
        const v = ui.noise1D(u * 3 + time * 0.4, seed + 1);
        const px = x + u * w;
        const py = y + h - v * h;
        if (i === 0) sys.path.moveTo(path, px, py);
        else sys.path.lineTo(path, px, py);
    }
    sys.canvas.setStrokeColor(color);
    sys.canvas.setStrokeWidth(2);
    sys.canvas.drawPath(path);
}

// =====================================================================
// Analytics chart — smoothed line with area fill
// =====================================================================
const CHART_POINTS = 24;

function chartValueAt(i, t) {
    // Continuous, deterministic, slowly-evolving curve.
    // Combining a few sines avoids the per-frame jitter from Math.random().
    const phase = t * 0.35;
    return 0.5
        + 0.18 * Math.sin(i * 0.45 + phase)
        + 0.12 * Math.sin(i * 0.21 - phase * 0.7)
        + 0.07 * Math.sin(i * 0.95 + phase * 1.3);
}

function drawChartContent(content) {
    const chartPadding = 10;
    const chartWidth = content.contentWidth - chartPadding * 2;
    const chartHeight = content.contentHeight - 30;
    const baseX = content.contentX + chartPadding;
    const baseY = content.contentY;

    // Build data
    const data = new Array(CHART_POINTS);
    for (let i = 0; i < CHART_POINTS; i++) data[i] = chartValueAt(i, time);

    // Grid
    sys.canvas.setStrokeColor(ui.withAlpha(COLORS.border, 0.6));
    sys.canvas.setStrokeWidth(1);
    for (let i = 0; i <= 4; i++) {
        const gy = baseY + chartHeight * (i / 4);
        sys.canvas.drawLine(baseX, gy, baseX + chartWidth, gy);
    }

    // Helper to map (i, value) -> screen point
    const px = (i) => baseX + (chartWidth / (CHART_POINTS - 1)) * i;
    const py = (v) => baseY + chartHeight - v * chartHeight;

    // Filled area under the line (built as a closed path)
    const area = sys.path.create();
    sys.path.moveTo(area, px(0), baseY + chartHeight);
    for (let i = 0; i < CHART_POINTS; i++) sys.path.lineTo(area, px(i), py(data[i]));
    sys.path.lineTo(area, px(CHART_POINTS - 1), baseY + chartHeight);
    sys.path.close(area);
    sys.canvas.setFillColor(ui.withAlpha(COLORS.accent, 0.15));
    sys.canvas.drawPath(area);

    // Line
    const line = sys.path.create();
    for (let i = 0; i < CHART_POINTS; i++) {
        if (i === 0) sys.path.moveTo(line, px(i), py(data[i]));
        else sys.path.lineTo(line, px(i), py(data[i]));
    }
    sys.canvas.setStrokeColor(COLORS.accent);
    sys.canvas.setStrokeWidth(3);
    sys.canvas.drawPath(line);

    // Pulsing point on the latest value
    const lastX = px(CHART_POINTS - 1);
    const lastY = py(data[CHART_POINTS - 1]);
    const pulse = 5 + Math.sin(time * 3) * 2;
    sys.canvas.setFillColor(ui.withAlpha(COLORS.accent, 0.3));
    sys.canvas.drawCircle(lastX, lastY, pulse + 4);
    sys.canvas.setFillColor(COLORS.accent);
    sys.canvas.drawCircle(lastX, lastY, 4);

    // Static dots on each sample
    for (let i = 0; i < CHART_POINTS - 1; i++) {
        sys.canvas.setFillColor(COLORS.accent);
        sys.canvas.drawCircle(px(i), py(data[i]), 2.5);
    }
}

// =====================================================================
// Activity list — staggered fade-in + breathing dots
// =====================================================================
const ACTIVITIES = [
    { text: 'New user signup', time: '2m ago', color: COLORS.accent2 },
    { text: 'Order completed', time: '5m ago', color: COLORS.accent },
    { text: 'Payment received', time: '12m ago', color: COLORS.accent3 },
    { text: 'Report generated', time: '1h ago', color: COLORS.accent4 },
    { text: 'System update', time: '2h ago', color: COLORS.accent5 },
];

function drawActivityContent(content) {
    let y = content.contentY + 10;

    for (let i = 0; i < ACTIVITIES.length; i++) {
        const a = ACTIVITIES[i];

        // Staggered entry animation
        const entryT = ui.clamp((appAge - i * 0.08) / 0.4, 0, 1);
        const eased = ui.easeOutCubic(entryT);
        const offsetX = (1 - eased) * 16;
        const alpha = eased;

        // Breathing dot
        const phase = time * 1.8 + i * 0.9;
        const dotPulse = 4 + Math.sin(phase) * 1.2;
        sys.canvas.setFillColor(ui.withAlpha(a.color, 0.25 * alpha));
        sys.canvas.drawCircle(content.contentX + 5 + offsetX, y + 5, dotPulse + 3);
        sys.canvas.setFillColor(ui.withAlpha(a.color, alpha));
        sys.canvas.drawCircle(content.contentX + 5 + offsetX, y + 5, dotPulse);

        sys.canvas.setFillColor(ui.withAlpha(COLORS.text, alpha));
        sys.canvas.drawText(a.text, content.contentX + 24 + offsetX, y + 12, 16);

        sys.canvas.setFillColor(ui.withAlpha(COLORS.textMuted, alpha));
        sys.canvas.drawText(a.time,
            content.contentX + content.contentWidth - 65, y + 12, 13);

        y += 40;
    }
}

// =====================================================================
// Tasks
// =====================================================================
const TASKS = [
    { text: 'Review pull requests', done: true },
    { text: 'Update documentation', done: true },
    { text: 'Fix navigation bug', done: false },
    { text: 'Deploy to production', done: false },
];

function drawTasksContent(content) {
    let y = content.contentY + 10;
    for (let i = 0; i < TASKS.length; i++) {
        const task = TASKS[i];

        // Checkbox
        sys.canvas.setStrokeColor(task.done ? COLORS.accent2 : COLORS.border);
        sys.canvas.setStrokeWidth(2);
        sys.canvas.drawRoundRect(content.contentX, y - 5, 24, 24, 4, 4);

        if (task.done) {
            sys.canvas.setFillColor(COLORS.accent2);
            sys.canvas.drawText('✓', content.contentX + 4, y + 13, 17);
        }

        sys.canvas.setFillColor(task.done ? COLORS.textMuted : COLORS.text);
        sys.canvas.drawText(task.text, content.contentX + 36, y + 13, 17);

        // Subtle progress hint behind active tasks
        if (!task.done) {
            const pulse = 0.4 + Math.sin(time * 2 + i) * 0.2;
            sys.canvas.setFillColor(ui.withAlpha(COLORS.accent, 0.08 * pulse));
            sys.canvas.drawRoundRect(content.contentX + 34, y - 5,
                content.contentWidth - 34, 24, 4, 4);
        }

        y += 40;
    }
}

// =====================================================================
// Performance bars — smoothed values, soft glow
// =====================================================================
function drawPerformanceContent(content) {
    let y = content.contentY + 10;
    const barWidth = content.contentWidth - 60;

    for (let i = 0; i < perfMetrics.length; i++) {
        const m = perfMetrics[i];
        // Slow drifting target around the base value.
        const target = ui.clamp(m.base + Math.sin(time * 0.7 + i * 1.7) * 0.08, 0, 1);
        m.value.set(target);
        const v = m.value.value;

        sys.canvas.setFillColor(COLORS.text);
        sys.canvas.drawText(m.label, content.contentX, y + 12, 16);

        // Background track
        sys.canvas.setFillColor(COLORS.border);
        sys.canvas.drawRoundRect(content.contentX, y + 22, barWidth, 10, 5, 5);

        // Glow
        sys.canvas.setFillColor(ui.withAlpha(m.color, 0.25));
        sys.canvas.drawRoundRect(content.contentX - 2, y + 20,
            barWidth * v + 4, 14, 7, 7);

        // Foreground bar
        sys.canvas.setFillColor(m.color);
        sys.canvas.drawRoundRect(content.contentX, y + 22, barWidth * v, 10, 5, 5);

        // Percentage
        sys.canvas.setFillColor(COLORS.textMuted);
        sys.canvas.drawText(`${Math.round(v * 100)}%`,
            content.contentX + barWidth + 12, y + 30, 14);

        y += 46;
    }
}

// =====================================================================
// Main animation loop
// =====================================================================
function animate(timestamp) {
    // Δt in seconds, clamped to avoid jumps after long pauses.
    const dt = lastTimestamp === 0 ? 1 / 60
        : ui.clamp((timestamp - lastTimestamp) / 1000, 0, 1 / 15);
    lastTimestamp = timestamp;
    time = timestamp / 1000;
    appAge += dt;

    const width = sys.window.getWidth();
    const height = sys.window.getHeight();
    const input = sys.input.get();

    // Background
    drawBackground(width, height);

    // Layout
    const layout = createDashboardLayout(width, height);

    // Hover state
    hoveredPanel = -1;
    for (let i = 0; i < layout.panels.length; i++) {
        if (layout.panels[i].contains(input.mouse.x, input.mouse.y)) {
            hoveredPanel = i;
            break;
        }
    }

    // Tab clicks (use the single source of truth: navTabRect)
    if (input.mouse.leftPressed) {
        for (let i = 0; i < TABS.length; i++) {
            const r = navTabRect(layout.margin, i);
            if (ui.pointInRect(input.mouse.x, input.mouse.y, r.x, r.y, r.w, r.h)) {
                if (selectedTab !== i) {
                    prevSelectedTab = selectedTab;
                    selectedTab = i;
                }
                break;
            }
        }
        // Click ripple anywhere
        ripples.spawn(input.mouse.x, input.mouse.y, COLORS.accent, 80, 0.7);
    }

    // Update animated values
    tabIndicatorX.update(dt);
    tabIndicatorW.update(dt);
    for (const lift of panelHoverLift.values()) lift.update(dt);
    for (const c of cardValues) c.current.update(dt);
    for (const m of perfMetrics) m.value.update(dt);
    ripples.update(dt);

    // Draw chrome
    drawNavBar(width, layout.navHeight, layout.margin);
    drawSidebar(layout.sidebarWidth, layout.navHeight, height,
        layout.margin, layout.gap, input.mouse.x, input.mouse.y);

    // Draw panels with content
    for (let i = 0; i < layout.panels.length; i++) {
        const content = layout.panels[i].draw(i);
        if (i < 4) drawStatCard(layout.panels[i], i, content);
        else if (i === 4) drawChartContent(content);
        else if (i === 5) drawActivityContent(content);
        else if (i === 6) drawTasksContent(content);
        else if (i === 7) drawPerformanceContent(content);
    }

    // Click ripples on top of everything
    ripples.draw();

    // Footer
    sys.canvas.setFillColor(COLORS.textMuted);
    sys.canvas.drawText('Press ESC to exit | Click to ripple, hover panels for lift',
        layout.margin, height - 12, 14);

    sys.animation.requestFrame(animate);
}

sys.log('Starting layout demo...');
sys.animation.requestFrame(animate);
