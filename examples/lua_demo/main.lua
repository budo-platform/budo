--[[
    Budo Lua Demo

    Demonstrates the full Lua canvas API:
    - Drawing primitives (rect, circle, oval, line, arc, round rect, text)
    - Paint styles (fill, stroke, alpha, caps, joins)
    - Transform stack (save/restore, translate, rotate, scale)
    - Path building and rendering
    - Input handling (mouse, keyboard)
    - Animation loop
]]

-- Animation state
local time = 0
local mouse_trail = {}
local MAX_TRAIL = 40

-- Colors
local COLORS = {
    "#FF6B6B", -- Red
    "#4ECDC4", -- Teal
    "#45B7D1", -- Blue
    "#96CEB4", -- Green
    "#FFEAA7", -- Yellow
    "#DDA0DD", -- Plum
    "#98D8C8", -- Mint
    "#F7DC6F", -- Gold
}

-- Bouncing balls
local balls = {}
for i = 1, 8 do
    balls[i] = {
        x = math.random(50, 750),
        y = math.random(50, 550),
        vx = (math.random() - 0.5) * 4,
        vy = (math.random() - 0.5) * 4,
        radius = math.random(8, 25),
        color = COLORS[((i - 1) % #COLORS) + 1],
    }
end

-- Helper: simple lerp
local function lerp(a, b, t)
    return a + (b - a) * t
end

-- Helper: HSL to hex color string
local function hsl_to_hex(h, s, l)
    local function hue2rgb(p, q, t)
        if t < 0 then t = t + 1 end
        if t > 1 then t = t - 1 end
        if t < 1/6 then return p + (q - p) * 6 * t end
        if t < 1/2 then return q end
        if t < 2/3 then return p + (q - p) * (2/3 - t) * 6 end
        return p
    end
    local r, g, b
    if s == 0 then
        r, g, b = l, l, l
    else
        local q = l < 0.5 and l * (1 + s) or l + s - l * s
        local p = 2 * l - q
        r = hue2rgb(p, q, h + 1/3)
        g = hue2rgb(p, q, h)
        b = hue2rgb(p, q, h - 1/3)
    end
    return string.format("#%02x%02x%02x",
        math.floor(r * 255 + 0.5),
        math.floor(g * 255 + 0.5),
        math.floor(b * 255 + 0.5))
end

-- Draw a rotating star using the path API
local function draw_star(cx, cy, outer_r, inner_r, points, rotation)
    local path = sys.path.create()
    local step = math.pi / points

    for i = 0, points * 2 - 1 do
        local r = (i % 2 == 0) and outer_r or inner_r
        local angle = i * step + rotation
        local x = cx + math.cos(angle) * r
        local y = cy + math.sin(angle) * r
        if i == 0 then
            sys.path.moveTo(path, x, y)
        else
            sys.path.lineTo(path, x, y)
        end
    end
    sys.path.close(path)
    sys.canvas.drawPath(path)
end

-- Draw a bezier wave using the path API
local function draw_wave(x, y, w, h, phase, segments)
    local path = sys.path.create()
    sys.path.moveTo(path, x, y)

    local seg_w = w / segments
    for i = 0, segments - 1 do
        local x1 = x + i * seg_w + seg_w * 0.33
        local y1 = y + math.sin(phase + i * 0.8) * h
        local x2 = x + i * seg_w + seg_w * 0.66
        local y2 = y - math.sin(phase + i * 0.8 + 0.4) * h
        local x3 = x + (i + 1) * seg_w
        local y3 = y
        sys.path.cubicTo(path, x1, y1, x2, y2, x3, y3)
    end

    sys.canvas.drawPath(path)
end

-- Update and draw bouncing balls
local function update_balls(width, height, dt)
    local dt_factor = dt * 60

    for _, ball in ipairs(balls) do
        ball.x = ball.x + ball.vx * dt_factor
        ball.y = ball.y + ball.vy * dt_factor

        if ball.x - ball.radius < 0 then
            ball.x = ball.radius
            ball.vx = -ball.vx
        elseif ball.x + ball.radius > width then
            ball.x = width - ball.radius
            ball.vx = -ball.vx
        end

        if ball.y - ball.radius < 0 then
            ball.y = ball.radius
            ball.vy = -ball.vy
        elseif ball.y + ball.radius > height then
            ball.y = height - ball.radius
            ball.vy = -ball.vy
        end

        -- Draw shadow
        sys.canvas.setFillColor("#00000030")
        sys.canvas.drawCircle(ball.x + 3, ball.y + 3, ball.radius)

        -- Draw ball
        sys.canvas.setFillColor(ball.color)
        sys.canvas.drawCircle(ball.x, ball.y, ball.radius)

        -- Draw highlight
        sys.canvas.setFillColor("#FFFFFF60")
        sys.canvas.drawCircle(ball.x - ball.radius * 0.25, ball.y - ball.radius * 0.25, ball.radius * 0.35)
    end
end

-- Main animation callback
sys.animation.start(function(timestamp)
    time = timestamp / 1000.0  -- convert to seconds
    local width = sys.window.getWidth()
    local height = sys.window.getHeight()
    local input = sys.input.get()

    -- Background gradient simulation using stripes
    for i = 0, height, 4 do
        local t = i / height
        local hue = (t * 0.1 + time * 0.02) % 1.0
        sys.canvas.setFillColor(hsl_to_hex(hue, 0.15, 0.12))
        sys.canvas.drawRect(0, i, width, 4)
    end

    -- Title
    sys.canvas.setFillColor("#FFFFFF")
    sys.canvas.drawText("Budo Lua Demo", 20, 35, 28)
    sys.canvas.setFillColor("#AAAAAA")
    sys.canvas.drawText(string.format("%.0f FPS  |  %dx%d  |  Lua %s",
        1.0 / math.max(input.deltaTime, 0.001), width, height, _VERSION), 20, 55, 14)

    -- Section 1: Bouncing balls
    update_balls(width, height, input.deltaTime)

    -- Section 2: Rotating star cluster
    sys.transform.save()
    sys.transform.translate(width * 0.75, height * 0.3)
    for i = 1, 5 do
        local angle = time * (0.3 + i * 0.15)
        local dist = 40 + i * 15
        local sx = math.cos(angle) * dist
        local sy = math.sin(angle) * dist
        sys.transform.save()
        sys.transform.translate(sx, sy)

        local hue = ((i - 1) / 5 + time * 0.1) % 1.0
        sys.canvas.setFillColor(hsl_to_hex(hue, 0.8, 0.6))
        draw_star(0, 0, 20 + math.sin(time * 2 + i) * 5, 8, 5, time * (1 + i * 0.5))

        sys.transform.restore()
    end
    sys.transform.restore()

    -- Section 3: Bezier waves
    sys.canvas.setStrokeWidth(2.5)
    for i = 0, 2 do
        local hue = (i / 3 + time * 0.05) % 1.0
        sys.canvas.setStrokeColor(hsl_to_hex(hue, 0.9, 0.65))
        draw_wave(20, height * 0.65 + i * 25, width - 40, 15, time * 2 + i * 1.5, 12)
    end

    -- Section 4: Drawing primitives showcase
    local base_y = height * 0.78

    -- Rounded rectangle
    sys.canvas.setFillColor("#FF6B6B80")
    sys.canvas.drawRoundRect(20, base_y, 100, 50, 12, 12)
    sys.canvas.setStrokeColor("#FF6B6B")
    sys.canvas.setStrokeWidth(2)
    sys.canvas.drawRoundRect(20, base_y, 100, 50, 12, 12)

    -- Oval
    sys.canvas.setFillColor("#4ECDC480")
    sys.canvas.drawOval(140, base_y, 100, 50)
    sys.canvas.setStrokeColor("#4ECDC4")
    sys.canvas.drawOval(140, base_y, 100, 50)

    -- Arc
    sys.canvas.setStrokeColor("#45B7D1")
    sys.canvas.setStrokeWidth(3)
    local sweep = (math.sin(time) + 1) * 150 + 60
    sys.canvas.drawArc(260, base_y, 50, 50, 0, sweep, true)

    -- Lines with different caps
    sys.canvas.setStrokeWidth(4)
    sys.canvas.setStrokeCap("round")
    sys.canvas.setStrokeColor("#FFEAA7")
    for j = 0, 4 do
        local y_off = base_y + 10 + j * 10
        sys.canvas.drawLine(340, y_off, 420, y_off + math.sin(time * 3 + j) * 8)
    end

    -- Points
    sys.canvas.setStrokeWidth(6)
    sys.canvas.setStrokeCap("round")
    for j = 0, 7 do
        local hue = (j / 8 + time * 0.3) % 1.0
        sys.canvas.setStrokeColor(hsl_to_hex(hue, 0.9, 0.65))
        local px = 450 + j * 15
        local py = base_y + 25 + math.sin(time * 4 + j * 0.8) * 15
        sys.canvas.drawPoint(px, py)
    end

    -- Section 5: Mouse trail with paths
    if input.mouse then
        table.insert(mouse_trail, { x = input.mouse.x, y = input.mouse.y })
        if #mouse_trail > MAX_TRAIL then
            table.remove(mouse_trail, 1)
        end
    end

    if #mouse_trail > 2 then
        sys.canvas.setStrokeWidth(3)
        sys.canvas.setStrokeCap("round")
        sys.canvas.setStrokeJoin("round")
        for i = 2, #mouse_trail do
            local t = i / #mouse_trail
            local hue = (t + time * 0.2) % 1.0
            sys.canvas.setStrokeColor(hsl_to_hex(hue, 0.9, 0.6))
            sys.canvas.setAlpha(math.floor(t * 255))
            sys.canvas.drawLine(
                mouse_trail[i-1].x, mouse_trail[i-1].y,
                mouse_trail[i].x, mouse_trail[i].y)
        end
        sys.canvas.setAlpha(255)
    end

    -- Section 6: Transform demo — rotating rectangles
    sys.transform.save()
    sys.transform.translate(width * 0.25, height * 0.35)
    for i = 0, 11 do
        sys.transform.save()
        sys.transform.rotate(i * 30 + time * 30)
        local alpha_val = math.floor(lerp(80, 220, (math.sin(time * 2 + i * 0.5) + 1) / 2))
        local hue = (i / 12 + time * 0.05) % 1.0
        local hex = hsl_to_hex(hue, 0.7, 0.55)
        -- Append alpha to hex color
        sys.canvas.setFillColor(hex)
        sys.canvas.setAlpha(alpha_val)
        sys.canvas.drawRect(-4, 30, 8, 40)
        sys.transform.restore()
    end
    sys.transform.restore()
    sys.canvas.setAlpha(255)

    -- Instructions
    sys.canvas.setFillColor("#FFFFFF80")
    sys.canvas.drawText("Move mouse to draw trail  |  Press ESC to quit", 20, height - 15, 12)
end)
