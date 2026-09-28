/**
 * Clock Demo - Budo
 *
 * Analog clock with a digital readout.
 */

const TAU = Math.PI * 2;

const BEACH_COLORS = {
    sky: '#36BFC9',
    ocean: '#047C8A',
    deepOcean: '#055B6D',
    sand: '#F2C46D',
    sunlitSand: '#FFE7A8',
    palmLeaf: '#2F8B57',
    palmShade: '#176344',
    driftwood: '#805834',
    coral: '#F56B4A',
    starfish: '#FF9B4A',
    sun: '#FFD34E',
    foam: '#FFF8DE',
    shell: '#F9D6C2',
    ink: '#123F3F',
};

const CLOCK_STYLE = {
    glowColor: BEACH_COLORS.sun,
    faceColor: BEACH_COLORS.sand,
    innerDiscColor: BEACH_COLORS.sunlitSand,
    borderColor: BEACH_COLORS.palmLeaf,
    innerRingColor: BEACH_COLORS.starfish,
    hourHandColor: BEACH_COLORS.coral,
    minuteHandColor: BEACH_COLORS.driftwood,
    secondHandColor: BEACH_COLORS.sun,
    centerDotColor: BEACH_COLORS.coral,
    majorTickColor: BEACH_COLORS.ink,
    minorTickColor: BEACH_COLORS.palmShade,
    numberColor: BEACH_COLORS.ink,
    displayBackgroundColor: BEACH_COLORS.deepOcean,
    displayTextColor: BEACH_COLORS.foam,
    labelColor: BEACH_COLORS.foam,
    quietTextColor: BEACH_COLORS.shell,
    hourHandWidth: 8,
    minuteHandWidth: 5,
    secondHandWidth: 2,
};

const FONT_SIZES = {
    title: 18,
    time: 32,
    date: 16,
    footer: 14,
    number: 24,
};

const DIGITAL_DISPLAY = {
    width: 180,
    height: 50,
    radius: 8,
};

const INNER_DISC_OFFSET = {
    x: 0.08,
    y: -0.07,
};

const INNER_DISC_RADIUS = 0.52;

const WEEKDAYS = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'];
const MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];

function getClockLayout(width, height) {
    const centerY = height / 2 - 30;
    const radius = Math.min(width, height) * 0.35;

    return {
        centerX: width / 2,
        centerY,
        radius,
        digitalDisplayY: centerY + radius + 40,
        dateY: centerY + radius + 110,
    };
}

function getClockTime(now) {
    return {
        analogHours: now.getHours() % 12,
        displayHours: now.getHours(),
        minutes: now.getMinutes(),
        seconds: now.getSeconds(),
        milliseconds: now.getMilliseconds(),
    };
}

function getHandAngles(clockTime) {
    return {
        hour: ((clockTime.analogHours + clockTime.minutes / 60) / 12) * TAU,
        minute: ((clockTime.minutes + clockTime.seconds / 60) / 60) * TAU,
        second: ((clockTime.seconds + clockTime.milliseconds / 1000) / 60) * TAU,
    };
}

function getPointOnCircle(centerX, centerY, radius, angle) {
    return {
        x: centerX + Math.cos(angle) * radius,
        y: centerY + Math.sin(angle) * radius,
    };
}

function getClockAngle(index, total) {
    return (index / total) * TAU - Math.PI / 2;
}

function getTickMarks(centerX, centerY, radius) {
    const tickMarks = [];

    for (let index = 0; index < 60; index++) {
        const angle = getClockAngle(index, 60);
        const isMajor = index % 5 === 0;
        const innerRadius = isMajor ? radius - 25 : radius - 15;
        const outerRadius = radius - 8;

        tickMarks.push({
            isMajor,
            start: getPointOnCircle(centerX, centerY, innerRadius, angle),
            end: getPointOnCircle(centerX, centerY, outerRadius, angle),
        });
    }

    return tickMarks;
}

function getHourNumberLabels(centerX, centerY, radius) {
    const numberRadius = radius - 45;
    const labels = [];

    for (let hour = 1; hour <= 12; hour++) {
        const position = getPointOnCircle(centerX, centerY, numberRadius, getClockAngle(hour, 12));
        const text = hour.toString();
        const size = sys.canvas.measureTextRect(text, FONT_SIZES.number);

        labels.push({
            text,
            x: position.x - size.width / 2,
            y: position.y + size.height / 2,
        });
    }

    return labels;
}

function formatTime(hours, minutes, seconds) {
    return [hours, minutes, seconds]
        .map(value => value.toString().padStart(2, '0'))
        .join(':');
}

function formatDate(now) {
    return `${WEEKDAYS[now.getDay()]}, ${MONTHS[now.getMonth()]} ${now.getDate()}, ${now.getFullYear()}`;
}

function formatTimezone(now) {
    const offsetHours = -now.getTimezoneOffset() / 60;
    return `UTC${offsetHours >= 0 ? '+' : ''}${offsetHours}`;
}

function getCenteredTextX(text, centerX, fontSize) {
    return centerX - sys.canvas.measureTextRect(text, fontSize).width / 2;
}

function getRightAlignedTextX(text, rightX, fontSize) {
    return rightX - sys.canvas.measureTextRect(text, fontSize).width;
}

function drawBackground(width, height, animationTime) {
    sys.canvas.clear(BEACH_COLORS.sky);

    sys.canvas.setFillColor(BEACH_COLORS.sun);
    sys.canvas.setAlpha(210);
    sys.canvas.drawCircle(width * 0.82, height * 0.16, Math.min(width, height) * 0.14);

    sys.canvas.setFillColor(BEACH_COLORS.ocean);
    sys.canvas.setAlpha(55);
    sys.canvas.drawCircle(width * 0.2, height * 0.15, Math.min(width, height) * 0.35);

    sys.canvas.setFillColor(BEACH_COLORS.foam);
    sys.canvas.setAlpha(28);

    for (let index = 0; index < 5; index++) {
        const x = width / 2 + Math.sin(animationTime * 0.3 + index * 1.2) * 200;
        const y = height / 2 + Math.cos(animationTime * 0.2 + index * 0.8) * 150;
        const radius = 90 + Math.sin(animationTime + index) * 25;
        sys.canvas.drawCircle(x, y, radius);
    }

    sys.canvas.setAlpha(255);
}

function drawClockFace(centerX, centerY, radius) {
    for (let glow = 5; glow > 0; glow--) {
        sys.canvas.setFillColor(CLOCK_STYLE.glowColor);
        sys.canvas.setAlpha(18 + glow * 8);
        sys.canvas.drawCircle(centerX, centerY, radius + glow * 3);
    }
    sys.canvas.setAlpha(255);

    sys.canvas.setFillColor(CLOCK_STYLE.faceColor);
    sys.canvas.drawCircle(centerX, centerY, radius);

    const innerDiscX = centerX + radius * INNER_DISC_OFFSET.x;
    const innerDiscY = centerY + radius * INNER_DISC_OFFSET.y;
    const innerDiscRadius = radius * INNER_DISC_RADIUS;

    sys.canvas.setFillColor(CLOCK_STYLE.innerDiscColor);
    sys.canvas.drawCircle(innerDiscX, innerDiscY, innerDiscRadius);

    sys.canvas.setStrokeColor(CLOCK_STYLE.borderColor);
    sys.canvas.setStrokeWidth(4);
    sys.canvas.drawCircle(centerX, centerY, radius);

    sys.canvas.setStrokeColor(CLOCK_STYLE.innerRingColor);
    sys.canvas.setStrokeWidth(1);
    sys.canvas.drawCircle(innerDiscX, innerDiscY, innerDiscRadius);
}

function drawTickMarks(tickMarks) {
    for (const tickMark of tickMarks) {
        sys.canvas.setStrokeColor(tickMark.isMajor ? CLOCK_STYLE.majorTickColor : CLOCK_STYLE.minorTickColor);
        sys.canvas.setStrokeWidth(tickMark.isMajor ? 3 : 1);
        sys.canvas.drawLine(tickMark.start.x, tickMark.start.y, tickMark.end.x, tickMark.end.y);
    }
}

function drawHourNumberLabels(labels) {
    sys.canvas.setFillColor(CLOCK_STYLE.numberColor);

    for (const label of labels) {
        sys.canvas.drawText(label.text, label.x, label.y, FONT_SIZES.number);
    }
}

function drawHand(centerX, centerY, angle, length, width, color, hasArrow) {
    const drawAngle = angle - Math.PI / 2;
    const tip = getPointOnCircle(centerX, centerY, length, drawAngle);
    const tail = getPointOnCircle(centerX, centerY, -length * 0.2, drawAngle);

    sys.canvas.setStrokeColor(color);
    sys.canvas.setStrokeWidth(width);
    sys.canvas.setStrokeCap('round');
    sys.canvas.drawLine(tail.x, tail.y, tip.x, tip.y);

    if (hasArrow) {
        drawHandArrow(tip.x, tip.y, drawAngle, color);
    }
}

function drawHandArrow(tipX, tipY, angle, color) {
    const arrowSize = 10;
    const sideAngle = angle + Math.PI / 2;

    const path = sys.path.create();
    sys.path.moveTo(path, tipX, tipY);
    sys.path.lineTo(path,
        tipX - Math.cos(angle) * arrowSize + Math.cos(sideAngle) * arrowSize / 2,
        tipY - Math.sin(angle) * arrowSize + Math.sin(sideAngle) * arrowSize / 2
    );
    sys.path.lineTo(path,
        tipX - Math.cos(angle) * arrowSize - Math.cos(sideAngle) * arrowSize / 2,
        tipY - Math.sin(angle) * arrowSize - Math.sin(sideAngle) * arrowSize / 2
    );
    sys.path.close(path);

    sys.canvas.setFillColor(color);
    sys.canvas.drawPath(path);
}

function drawHands(centerX, centerY, radius, angles) {
    drawHand(centerX, centerY, angles.hour,
        radius * 0.5, CLOCK_STYLE.hourHandWidth, CLOCK_STYLE.hourHandColor, false);
    drawHand(centerX, centerY, angles.minute,
        radius * 0.7, CLOCK_STYLE.minuteHandWidth, CLOCK_STYLE.minuteHandColor, false);
    drawHand(centerX, centerY, angles.second,
        radius * 0.8, CLOCK_STYLE.secondHandWidth, CLOCK_STYLE.secondHandColor, true);
}

function drawCenterDot(centerX, centerY) {
    sys.canvas.setFillColor(CLOCK_STYLE.innerDiscColor);
    sys.canvas.drawCircle(centerX, centerY, 12);

    sys.canvas.setFillColor(CLOCK_STYLE.centerDotColor);
    sys.canvas.drawCircle(centerX, centerY, 8);

    sys.canvas.setFillColor(BEACH_COLORS.foam);
    sys.canvas.drawCircle(centerX, centerY, 3);
}

function drawDigitalDisplay(centerX, y, clockTime) {
    const time = formatTime(clockTime.displayHours, clockTime.minutes, clockTime.seconds);
    const x = centerX - DIGITAL_DISPLAY.width / 2;
    const textX = getCenteredTextX(time, centerX, FONT_SIZES.time);

    sys.canvas.setFillColor(CLOCK_STYLE.displayBackgroundColor);
    sys.canvas.drawRoundRect(x, y,
        DIGITAL_DISPLAY.width, DIGITAL_DISPLAY.height,
        DIGITAL_DISPLAY.radius, DIGITAL_DISPLAY.radius);

    sys.canvas.setStrokeColor(CLOCK_STYLE.sunlitSand);
    sys.canvas.setStrokeWidth(2);
    sys.canvas.drawRoundRect(x, y,
        DIGITAL_DISPLAY.width, DIGITAL_DISPLAY.height,
        DIGITAL_DISPLAY.radius, DIGITAL_DISPLAY.radius);

    sys.canvas.setFillColor(CLOCK_STYLE.displayTextColor);
    sys.canvas.drawText(time, textX, y + 35, FONT_SIZES.time);
}

function drawDateDisplay(centerX, y, now) {
    const date = formatDate(now);

    sys.canvas.setFillColor(CLOCK_STYLE.labelColor);
    sys.canvas.drawText(date, getCenteredTextX(date, centerX, FONT_SIZES.date), y, FONT_SIZES.date);
}

function drawScreenLabels(width, height, now) {
    const timezone = formatTimezone(now);

    sys.canvas.setFillColor(CLOCK_STYLE.labelColor);
    sys.canvas.drawText('Beach Clock', 20, 30, FONT_SIZES.title);

    sys.canvas.setFillColor(CLOCK_STYLE.quietTextColor);
    sys.canvas.drawText('Press ESC to exit', 20, height - 20, FONT_SIZES.footer);
    sys.canvas.drawText(timezone,
        getRightAlignedTextX(timezone, width - 20, FONT_SIZES.footer),
        height - 20, FONT_SIZES.footer);
}

function drawClock(layout, now) {
    const clockTime = getClockTime(now);
    const handAngles = getHandAngles(clockTime);
    const tickMarks = getTickMarks(layout.centerX, layout.centerY, layout.radius);
    const hourLabels = getHourNumberLabels(layout.centerX, layout.centerY, layout.radius);

    drawClockFace(layout.centerX, layout.centerY, layout.radius);
    drawTickMarks(tickMarks);
    drawHourNumberLabels(hourLabels);
    drawHands(layout.centerX, layout.centerY, layout.radius, handAngles);
    drawCenterDot(layout.centerX, layout.centerY);
    drawDigitalDisplay(layout.centerX, layout.digitalDisplayY, clockTime);
    drawDateDisplay(layout.centerX, layout.dateY, now);
}

function frame(timestamp) {
    const width = sys.window.getWidth();
    const height = sys.window.getHeight();
    const now = new Date();
    const layout = getClockLayout(width, height);

    drawBackground(width, height, timestamp / 1000);
    drawClock(layout, now);
    drawScreenLabels(width, height, now);

    sys.animation.requestFrame(frame);
}

sys.animation.requestFrame(frame);
