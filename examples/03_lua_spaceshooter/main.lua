-- ══════════════════════════════════════════════════════════
--  NEON SPACE SHOOTER  –  Enhanced Visual Edition
-- ══════════════════════════════════════════════════════════

local prog = sys.gl.createProgram('shader.vert', 'shader.frag')
sys.gl.setUniform1f(prog, 'u_bloom_intensity', 2.8)
sys.gl.setUniform1f(prog, 'u_hit_flash', 0.0)
sys.gl.setUniform1f(prog, 'u_warp', 0.0)

-- ── Audio ────────────────────────────────────────────────
sys.audio.start()
sys.audio.setMasterGain(0.55)

local function load_sound(path)
    local id = sys.audio.loadBuffer(path)
    if id < 0 and console and console.log then
        sys.log("Could not load sound " .. path .. ": " .. sys.audio.getError())
    end
    return id
end

local sounds = {
    shoot = load_sound("sounds/laserSmall_000.ogg"),
    hit = load_sound("sounds/impactMetal_002.ogg"),
    explosion = load_sound("sounds/explosionCrunch_000.ogg"),
    thud = load_sound("sounds/lowFrequency_explosion_000.ogg"),
    powerup = load_sound("sounds/forceField_000.ogg"),
}

local function play_sound(id, gain)
    if id and id >= 0 then
        sys.audio.playBuffer(id, false, gain or 1.0)
    end
end

-- ── Ship paths ───────────────────────────────────────────
-- Player: sleek fighter silhouette
local ship_path = sys.path.create()
sys.path.moveTo(ship_path, 0, -28)
sys.path.lineTo(ship_path, 7, -14)
sys.path.lineTo(ship_path, 24, 18)
sys.path.lineTo(ship_path, 12, 10)
sys.path.lineTo(ship_path, 7, 20)
sys.path.lineTo(ship_path, 0, 14)
sys.path.lineTo(ship_path, -7, 20)
sys.path.lineTo(ship_path, -12, 10)
sys.path.lineTo(ship_path, -24, 18)
sys.path.lineTo(ship_path, -7, -14)
sys.path.close(ship_path)

-- Wing accent for extra detail
local wing_l = sys.path.create()
sys.path.moveTo(wing_l, -7, -4)
sys.path.lineTo(wing_l, -24, 18)
sys.path.lineTo(wing_l, -12, 6)
sys.path.close(wing_l)
local wing_r = sys.path.create()
sys.path.moveTo(wing_r, 7, -4)
sys.path.lineTo(wing_r, 24, 18)
sys.path.lineTo(wing_r, 12, 6)
sys.path.close(wing_r)

-- Enemy type A: angular diamond
local enem_a = sys.path.create()
sys.path.moveTo(enem_a, 0, -22)
sys.path.lineTo(enem_a, 18, 0)
sys.path.lineTo(enem_a, 10, 18)
sys.path.lineTo(enem_a, 0, 10)
sys.path.lineTo(enem_a, -10, 18)
sys.path.lineTo(enem_a, -18, 0)
sys.path.close(enem_a)

-- Enemy type B: crab/star
local enem_b = sys.path.create()
for i = 0, 5 do
    local a_out = (i / 6) * math.pi * 2 - math.pi / 2
    local a_in  = a_out + math.pi / 6
    local ro = 22
    local ri = 10
    sys.path.lineTo(enem_b, math.cos(a_out) * ro, math.sin(a_out) * ro)
    sys.path.lineTo(enem_b, math.cos(a_in)  * ri, math.sin(a_in)  * ri)
end
sys.path.close(enem_b)

-- Enemy type C: fat boss saucer
local enem_c = sys.path.create()
sys.path.moveTo(enem_c, 0, -14)
sys.path.lineTo(enem_c, 28, -6)
sys.path.lineTo(enem_c, 32, 8)
sys.path.lineTo(enem_c, 16, 20)
sys.path.lineTo(enem_c, 0, 22)
sys.path.lineTo(enem_c, -16, 20)
sys.path.lineTo(enem_c, -32, 8)
sys.path.lineTo(enem_c, -28, -6)
sys.path.close(enem_c)

local enemy_paths = { enem_a, enem_b, enem_c }

-- ── State ────────────────────────────────────────────────
local state = "TITLE"
local score = 0
local highscore = 0
local last_time = 0
local global_time = 0

local player = {
    x = 400, y = 400,
    vx = 0, vy = 0,
    hp = 5, max_hp = 5,
    invincible = 0,
    shield_flash = 0,
}

local bullets = {}
local enemy_bullets = {}
local enemies = {}
local particles = {}
local powerups = {}

local last_shot = 0
local enemy_spawn_timer = 0
local screenshake_x = 0
local screenshake_y = 0
local screenshake_time = 0
local hit_flash_timer = 0
local warp_timer = 0
local level = 1

-- Persistent random seed for repeatable debris
math.randomseed(42)
local debris = {}
for i = 1, 60 do
    table.insert(debris, {
        x = math.random(0, 1000),
        y = math.random(0, 700),
        speed = math.random(20, 90) * 0.001, -- stored as fraction
        size = math.random(1, 3),
        alpha = math.random(60, 180)
    })
end
math.randomseed(os.time())

-- ── Audio helpers ────────────────────────────────────────
function play_shoot()
    play_sound(sounds.shoot, 0.35)
end

function play_hit()
    play_sound(sounds.hit, 0.55)
end

function play_explosion()
    play_sound(sounds.explosion, 0.85)
    warp_timer = 0.35
    screenshake_time = 0.22
end

function play_thud()
    play_sound(sounds.thud, 0.75)
    screenshake_time = 0.12
    hit_flash_timer = 0.18
end

function play_powerup()
    play_sound(sounds.powerup, 0.55)
end

-- ── Utilities ────────────────────────────────────────────
function dist(x1, y1, x2, y2)
    local dx, dy = x1-x2, y1-y2
    return math.sqrt(dx*dx + dy*dy)
end

-- Spawn varied particles
function spark(x, y, col, count, speed_min, speed_max, life_max)
    for _ = 1, count do
        local angle = math.random() * math.pi * 2
        local speed = math.random(speed_min or 60, speed_max or 320)
        table.insert(particles, {
            x=x, y=y,
            vx=math.cos(angle)*speed,
            vy=math.sin(angle)*speed,
            life=1.0,
            max_life=0.4 + math.random()*(life_max or 0.6),
            color=col,
            size_max=3+math.random()*5,
            glow=true
        })
    end
end

-- Debris trail (engine smoke)
function smoke(x, y, col, count)
    for _ = 1, count do
        local angle = math.pi/2 + (math.random()-0.5)*0.8
        local speed = math.random(20, 80)
        table.insert(particles, {
            x=x + (math.random()-0.5)*8,
            y=y,
            vx=math.cos(angle)*speed,
            vy=math.sin(angle)*speed,
            life=1.0,
            max_life=0.25 + math.random()*0.2,
            color=col,
            size_max=2+math.random()*3,
            glow=false
        })
    end
end

-- ── Game reset ───────────────────────────────────────────
function start_game()
    score = 0
    level = 1
    player.hp = player.max_hp
    player.invincible = 0
    player.shield_flash = 0
    bullets = {}
    enemy_bullets = {}
    enemies = {}
    particles = {}
    powerups = {}
    state = "PLAY"
    local w = sys.window.getWidth()
    local h = sys.window.getHeight()
    player.x = w / 2
    player.y = h - 110
    player.vx = 0
    player.vy = 0
    screenshake_time = 0
    hit_flash_timer = 0
    warp_timer = 0
end

-- ── Enemy drawing helpers ────────────────────────────────
local ENEMY_FILL = { "#1A0022", "#001A18", "#1A0A00" }
local ENEMY_GLOW = { 0xFF993399, 0xFF00CCBB, 0xFFFF6600 }
local ENEMY_CORE = { "#FF44FF", "#00FFCC", "#FF8800" }
local BULLET_COLORS = { "#FF66FF", "#44FFDD", "#FFAA44" }

function draw_neon_path(path_id, fill_col, glow_col, glow_w, glow_alpha, core_col)
    -- Outer glow ring (wide, low alpha)
    sys.canvas.setAlpha(glow_alpha or 60)
    sys.canvas.setStrokeColor(glow_col)
    sys.canvas.setStrokeWidth(glow_w + 6)
    sys.canvas.drawPath(path_id)
    -- Mid glow
    sys.canvas.setAlpha(120)
    sys.canvas.setStrokeWidth(glow_w)
    sys.canvas.drawPath(path_id)
    -- Fill
    sys.canvas.setAlpha(255)
    sys.canvas.setFillColor(fill_col)
    sys.canvas.drawPath(path_id)
    -- Core bright edge
    sys.canvas.setStrokeColor(core_col)
    sys.canvas.setStrokeWidth(1.5)
    sys.canvas.drawPath(path_id)
end

-- ── Main frame ───────────────────────────────────────────
function frame(t)
    local dt = (t - last_time) / 1000.0
    if dt > 0.05 then dt = 0.016 end
    last_time = t
    global_time = global_time + dt

    local w = sys.window.getWidth()
    local h = sys.window.getHeight()
    local input = sys.input.get()

    -- Screen shake decay
    screenshake_time = math.max(0, screenshake_time - dt)
    local shk = screenshake_time * 14
    screenshake_x = (math.random()-0.5) * shk
    screenshake_y = (math.random()-0.5) * shk

    -- Shader uniform updates
    hit_flash_timer = math.max(0, hit_flash_timer - dt)
    warp_timer = math.max(0, warp_timer - dt)
    sys.gl.setUniform1f(prog, 'u_hit_flash', hit_flash_timer / 0.18)
    sys.gl.setUniform1f(prog, 'u_warp', warp_timer / 0.35)

    -- ── UPDATE ───────────────────────────────────────────
    if state == "TITLE" then
        if input.pointer and input.pointer.pressed then start_game() end

    elseif state == "GAMEOVER" then
        if input.pointer and input.pointer.pressed then start_game() end

    elseif state == "PLAY" then
        level = 1 + math.floor(score / 150)

        -- Player smooth follow
        if input.pointer and input.pointer.down then
            local tx = input.pointer.x
            local ty = input.pointer.y - 50
            player.vx = player.vx + (tx - player.x) * 26 * dt
            player.vy = player.vy + (ty - player.y) * 26 * dt
        end
        player.vx = player.vx * 0.84
        player.vy = player.vy * 0.84
        player.x = player.x + player.vx * dt
        player.y = player.y + player.vy * dt

        player.x = math.max(28, math.min(w-28, player.x))
        player.y = math.max(28, math.min(h-28, player.y))

        player.invincible = math.max(0, player.invincible - dt)

        -- Engine smoke (always visible in play)
        if math.random() < 0.5 then
            smoke(player.x - 8, player.y + 18, "#00CCFF", 1)
            smoke(player.x + 8, player.y + 18, "#00CCFF", 1)
        end

        -- Shooting (auto + pointer down)
        last_shot = last_shot + dt
        local fire_rate = math.max(0.055, 0.14 - score * 0.00008)
        if last_shot > fire_rate then
            last_shot = 0
            if input.pointer and input.pointer.down then
                local spread = (level > 4) and 3 or 2
                for s = 1, spread do
                    local ox = (s - (spread+1)/2) * 14
                    table.insert(bullets, {
                        x = player.x + ox, y = player.y - 15,
                        vy = -780 - level * 15,
                        w = 5, h = 22
                    })
                end
                play_shoot()
                -- muzzle flash
                spark(player.x, player.y - 20, "#FFFFFF", 2, 30, 120, 0.12)
            end
        end

        -- Enemy spawn
        enemy_spawn_timer = enemy_spawn_timer + dt
        local spawn_rate = math.max(0.12, 0.9 - level * 0.08)
        if enemy_spawn_timer > spawn_rate then
            enemy_spawn_timer = 0
            local kind = math.random(1, 3)
            local base_hp = kind + math.floor(level * 0.7)
            local base_spd = 120 + level * 18
            local e = {
                x = math.random(35, math.floor(w-35)),
                y = -40,
                vy = math.random(base_spd, base_spd + 130),
                vx = math.random(-55, 55),
                hp = base_hp,
                max_hp = base_hp,
                rot = 0, rot_speed = (math.random()-0.5) * 140,
                kind = kind,
                shoot_timer = math.random() * 2,
                radius = (kind == 3) and 34 or 22,
                points = kind * 10
            }
            table.insert(enemies, e)
        end

        -- Update player bullets
        for i = #bullets, 1, -1 do
            local b = bullets[i]
            b.y = b.y + b.vy * dt
            if b.y < -60 then table.remove(bullets, i) end
        end

        -- Update enemy bullets
        for i = #enemy_bullets, 1, -1 do
            local b = enemy_bullets[i]
            b.x = b.x + b.vx * dt
            b.y = b.y + b.vy * dt
            if b.y > h + 40 or b.x < -40 or b.x > w + 40 then
                table.remove(enemy_bullets, i)
            elseif player.invincible <= 0 and dist(b.x, b.y, player.x, player.y) < 22 then
                player.hp = player.hp - 1
                table.remove(enemy_bullets, i)
                spark(player.x, player.y, "#FF4444", 12)
                play_thud()
                player.invincible = 1.2
                if player.hp <= 0 then
                    state = "GAMEOVER"
                    if score > highscore then highscore = score end
                    spark(player.x, player.y, "#FF8800", 30)
                    play_explosion()
                end
            end
        end

        -- Update enemies
        for i = #enemies, 1, -1 do
            local e = enemies[i]
            e.y = e.y + e.vy * dt
            e.x = e.x + e.vx * dt
            e.rot = e.rot + e.rot_speed * dt
            if e.x < e.radius then e.vx = math.abs(e.vx); e.x = e.radius end
            if e.x > w-e.radius then e.vx = -math.abs(e.vx); e.x = w-e.radius end

            -- Enemy shooting (types 2 and 3)
            if e.kind >= 2 then
                e.shoot_timer = e.shoot_timer - dt
                if e.shoot_timer <= 0 then
                    e.shoot_timer = 2.0 + math.random() * 2.2
                    local ang = math.atan(player.y - e.y, player.x - e.x)
                    local spd = 260 + level * 12
                    table.insert(enemy_bullets, {
                        x = e.x, y = e.y + 10,
                        vx = math.cos(ang) * spd,
                        vy = math.sin(ang) * spd,
                        kind = e.kind
                    })
                    if e.kind == 3 then
                        -- Boss spreads 3 bullets
                        for k = -1, 1, 2 do
                            table.insert(enemy_bullets, {
                                x = e.x, y = e.y + 10,
                                vx = math.cos(ang + k*0.35) * spd,
                                vy = math.sin(ang + k*0.35) * spd,
                                kind = e.kind
                            })
                        end
                    end
                end
            end

            -- Bullet vs enemy
            local destroyed = false
            for j = #bullets, 1, -1 do
                local b = bullets[j]
                if dist(e.x, e.y, b.x, b.y) < e.radius + 6 then
                    e.hp = e.hp - 1
                    spark(b.x, b.y, BULLET_COLORS[e.kind], 4, 80, 200, 0.3)
                    table.remove(bullets, j)
                    play_hit()
                    if e.hp <= 0 then
                        destroyed = true
                        score = score + e.points
                        spark(e.x, e.y, ENEMY_CORE[e.kind], 18 + e.kind*5, 80, 380, 0.7)
                        spark(e.x, e.y, "#FFFFFF", 6, 200, 500, 0.4)
                        play_explosion()
                        -- Chance to drop power-up
                        if math.random() < 0.15 + e.kind * 0.05 then
                            table.insert(powerups, { x=e.x, y=e.y, vy=60, life=6 })
                        end
                        break
                    end
                end
            end

            if destroyed then
                table.remove(enemies, i)
            elseif e.y > h + 50 then
                table.remove(enemies, i)
                player.hp = player.hp - 1
                spark(player.x - 5, player.y, "#FF2200", 8)
                play_thud()
                if player.hp <= 0 then
                    state = "GAMEOVER"
                    if score > highscore then highscore = score end
                end
            elseif player.invincible <= 0 and dist(e.x, e.y, player.x, player.y) < e.radius + 20 then
                spark(player.x, player.y, "#FF3300", 22)
                player.hp = player.hp - (e.kind == 3 and 2 or 1)
                table.remove(enemies, i)
                play_explosion()
                player.invincible = 1.5
                if player.hp <= 0 then
                    state = "GAMEOVER"
                    if score > highscore then highscore = score end
                end
            end
        end

        -- Powerups
        for i = #powerups, 1, -1 do
            local p = powerups[i]
            p.y = p.y + p.vy * dt
            p.life = p.life - dt
            if p.life <= 0 or p.y > h + 20 then
                table.remove(powerups, i)
            elseif dist(p.x, p.y, player.x, player.y) < 28 then
                player.hp = math.min(player.max_hp, player.hp + 1)
                spark(p.x, p.y, "#00FF88", 14, 60, 250, 0.5)
                play_powerup()
                table.remove(powerups, i)
            end
        end
    end

    -- Update particles
    for i = #particles, 1, -1 do
        local p = particles[i]
        p.x = p.x + p.vx * dt
        p.y = p.y + p.vy * dt
        p.vx = p.vx * 0.92
        p.vy = p.vy * 0.92
        p.life = p.life - dt / p.max_life
        if p.life <= 0 then table.remove(particles, i) end
    end

    -- ── DRAW ─────────────────────────────────────────────
    sys.canvas.clear(0x00000000)
    sys.canvas.setAntiAlias(1)

    -- Apply screen shake as a global translate
    if screenshake_time > 0 then
        sys.canvas.save()
        sys.canvas.translate(screenshake_x, screenshake_y)
    end

    -- Speed-lines (hyperspace streaks) — only during play
    if state == "PLAY" then
        for i, db in ipairs(debris) do
            local sy = (db.y + global_time * db.speed * h * 1.5) % h
            local sx = db.x % w
            local streak = math.min(30, 8 + level * 3) * db.speed * 80
            sys.canvas.setAlpha(db.alpha)
            sys.canvas.setFillColor(0xFFCCDDFF)
            sys.canvas.drawRect(sx, sy, db.size, streak)
        end
        sys.canvas.setAlpha(255)
    end

    -- ── Player ──────────────────────────────────────────
    if state == "PLAY" or state == "TITLE" then
        local px = (state == "TITLE") and (w/2) or player.x
        local py = (state == "TITLE") and (h/2 + math.sin(global_time*2.5)*25) or player.y

        -- Invincibility blink
        local visible = true
        if state == "PLAY" and player.invincible > 0 then
            visible = (math.floor(global_time * 12) % 2 == 0)
        end

        if visible then
            sys.canvas.save()
            sys.canvas.translate(px, py)

            -- Outer hull glow (wide, very transparent)
            sys.canvas.setAlpha(40)
            sys.canvas.setStrokeColor(0xFF00FFFF)
            sys.canvas.setStrokeWidth(14)
            sys.canvas.drawPath(ship_path)
            -- Mid glow
            sys.canvas.setAlpha(90)
            sys.canvas.setStrokeWidth(6)
            sys.canvas.drawPath(ship_path)
            -- Wing fills
            sys.canvas.setAlpha(255)
            sys.canvas.setFillColor("#002244")
            sys.canvas.drawPath(wing_l)
            sys.canvas.drawPath(wing_r)
            sys.canvas.setFillColor("#003366")
            sys.canvas.drawPath(ship_path)
            -- Bright edge
            sys.canvas.setStrokeColor("#00FFFF")
            sys.canvas.setStrokeWidth(1.8)
            sys.canvas.drawPath(ship_path)
            -- Cockpit
            sys.canvas.setFillColor("#88EEFF")
            sys.canvas.drawCircle(0, -8, 5)

            -- Engine flames (multi-layer)
            local flicker1 = 14 + math.random()*12
            local flicker2 = 10 + math.random()*8
            sys.canvas.setAlpha(200)
            sys.canvas.setFillColor("#FF8800")
            sys.canvas.drawRect(-5, 16, 10, flicker2)
            sys.canvas.setAlpha(160)
            sys.canvas.setFillColor("#FFE000")
            sys.canvas.drawRect(-3, 16, 6, flicker1)
            sys.canvas.setAlpha(100)
            sys.canvas.setFillColor("#FFFFFF")
            sys.canvas.drawRect(-1.5, 16, 3, 8)
            sys.canvas.setAlpha(255)

            sys.canvas.restore()
        end
    end

    -- ── Enemies ──────────────────────────────────────────
    for _, e in ipairs(enemies) do
        local hp_frac = e.hp / e.max_hp
        local fill = ENEMY_FILL[e.kind]
        local glow_c = ENEMY_GLOW[e.kind]
        local core_c = ENEMY_CORE[e.kind]

        -- Damage color shift
        if hp_frac < 0.35 then
            fill = "#220000"
            core_c = "#FFCC00"
        end

        sys.canvas.save()
        sys.canvas.translate(e.x, e.y)
        sys.canvas.rotate(e.rot)

        -- Outer halo on boss
        if e.kind == 3 then
            sys.canvas.setAlpha(30)
            sys.canvas.setFillColor(ENEMY_CORE[3])
            sys.canvas.drawCircle(0, 0, 38 + math.sin(global_time*4)*4)
            sys.canvas.setAlpha(255)
        end

        draw_neon_path(enemy_paths[e.kind], fill, glow_c, 4, 55, core_c)

        -- Core pip / eye
        sys.canvas.setAlpha(255)
        sys.canvas.setFillColor(core_c)
        sys.canvas.drawCircle(0, 0, 5 - e.kind)

        sys.canvas.restore()
    end

    -- ── Player bullets ───────────────────────────────────
    sys.canvas.setStrokeWidth(0)
    for _, b in ipairs(bullets) do
        -- Outer glow
        sys.canvas.setAlpha(60)
        sys.canvas.setFillColor("#00FFFF")
        sys.canvas.drawRect(b.x - b.w/2 - 3, b.y - b.h/2 - 4, b.w + 6, b.h + 8)
        -- Core bolt
        sys.canvas.setAlpha(255)
        sys.canvas.setFillColor("#FFFFFF")
        sys.canvas.drawRect(b.x - b.w/2, b.y - b.h/2, b.w, b.h)
        sys.canvas.setFillColor("#88FFFF")
        sys.canvas.drawRect(b.x - 1, b.y - b.h/2, 2, b.h)
    end

    -- ── Enemy bullets ────────────────────────────────────
    for _, b in ipairs(enemy_bullets) do
        local col = BULLET_COLORS[b.kind]
        sys.canvas.setAlpha(70)
        sys.canvas.setFillColor(col)
        sys.canvas.drawCircle(b.x, b.y, 9)
        sys.canvas.setAlpha(255)
        sys.canvas.setFillColor(col)
        sys.canvas.drawCircle(b.x, b.y, 5)
        sys.canvas.setFillColor("#FFFFFF")
        sys.canvas.drawCircle(b.x, b.y, 2)
    end

    -- ── Power-ups ────────────────────────────────────────
    for _, p in ipairs(powerups) do
        local pulse = math.abs(math.sin(global_time * 5)) * 6
        sys.canvas.setAlpha(180)
        sys.canvas.setFillColor("#00FF88")
        sys.canvas.drawCircle(p.x, p.y, 9 + pulse)
        sys.canvas.setAlpha(255)
        sys.canvas.setFillColor("#FFFFFF")
        sys.canvas.drawCircle(p.x, p.y, 5)
        -- "+" cross
        sys.canvas.setFillColor("#00FF88")
        sys.canvas.drawRect(p.x - 5, p.y - 1.5, 10, 3)
        sys.canvas.drawRect(p.x - 1.5, p.y - 5, 3, 10)
    end

    -- ── Particles ────────────────────────────────────────
    for _, p in ipairs(particles) do
        local a = math.max(0, math.floor(p.life * 255))
        sys.canvas.setAlpha(a)
        if p.glow then
            -- glow ring
            sys.canvas.setFillColor(p.color)
            sys.canvas.drawCircle(p.x, p.y, (p.life * p.size_max) + 3)
            sys.canvas.setAlpha(math.floor(a * 0.5))
            sys.canvas.drawCircle(p.x, p.y, p.life * p.size_max * 2)
        else
            sys.canvas.setFillColor(p.color)
            sys.canvas.drawCircle(p.x, p.y, p.life * p.size_max)
        end
    end
    sys.canvas.setAlpha(255)

    -- ── HUD ──────────────────────────────────────────────
    if state == "TITLE" then
        -- Big title glow layers
        sys.canvas.setAlpha(50)
        sys.canvas.setFillColor("#00FFFF")
        sys.canvas.drawText("NEON  SPACE  SHOOTER", w/2-215, h/3+3, 40)
        sys.canvas.setAlpha(150)
        sys.canvas.drawText("NEON  SPACE  SHOOTER", w/2-215, h/3+1, 40)
        sys.canvas.setAlpha(255)
        sys.canvas.setFillColor("#FFFFFF")
        sys.canvas.drawText("NEON  SPACE  SHOOTER", w/2-215, h/3, 40)

        sys.canvas.setFillColor("#00FFCC")
        sys.canvas.drawText("HOLD & DRAG to move and shoot", w/2-185, h/2 + 30, 20)
        sys.canvas.drawText("Avoid enemies and their fire", w/2-165, h/2 + 58, 18)
        sys.canvas.drawText("Collect  +  power-ups for HP", w/2-165, h/2 + 82, 18)

        if math.sin(global_time * 4) > 0 then
            sys.canvas.setFillColor("#FFFFFF")
            sys.canvas.drawText("CLICK  TO  START", w/2-105, h/2+130, 26)
        end

        if highscore > 0 then
            sys.canvas.setFillColor("#FFDD00")
            sys.canvas.drawText("BEST: " .. highscore, w/2-55, h - 40, 22)
        end

    elseif state == "GAMEOVER" then
        sys.canvas.setAlpha(80)
        sys.canvas.setFillColor("#FF0000")
        sys.canvas.drawText("GAME  OVER", w/2-125, h/3+4, 44)
        sys.canvas.setAlpha(255)
        sys.canvas.setFillColor("#FF4444")
        sys.canvas.drawText("GAME  OVER", w/2-125, h/3, 44)

        sys.canvas.setFillColor("#FFFFFF")
        sys.canvas.drawText("SCORE:  " .. score, w/2-100, h/2, 30)
        sys.canvas.setFillColor("#FFDD00")
        sys.canvas.drawText("BEST:   " .. highscore, w/2-95, h/2+40, 24)

        if math.sin(global_time * 4) > 0 then
            sys.canvas.setFillColor("#FFFFFF")
            sys.canvas.drawText("CLICK TO PLAY AGAIN", w/2-130, h/2 + 110, 26)
        end

    elseif state == "PLAY" then
        -- Score
        sys.canvas.setFillColor("#00FFFF")
        sys.canvas.drawText("SCORE", 20, 22, 13)
        sys.canvas.setFillColor("#FFFFFF")
        sys.canvas.drawText(tostring(score), 20, 42, 26)

        -- Level
        sys.canvas.setFillColor("#FFDD00")
        sys.canvas.drawText("LV " .. level, 20, 72, 16)

        -- HP bar (right side)
        local bar_x = w - 20 - player.max_hp * 24
        sys.canvas.setFillColor("#00FFFF")
        sys.canvas.drawText("HP", bar_x - 36, 30, 16)
        for i = 1, player.max_hp do
            local bx = bar_x + (i-1) * 24
            if i <= player.hp then
                sys.canvas.setAlpha(60)
                sys.canvas.setFillColor("#00FFFF")
                sys.canvas.drawRect(bx - 2, 12, 20, 22)
                sys.canvas.setAlpha(255)
                sys.canvas.setFillColor("#00FFFF")
                sys.canvas.drawRect(bx, 14, 16, 18)
            else
                sys.canvas.setAlpha(255)
                sys.canvas.setFillColor("#111133")
                sys.canvas.drawRect(bx, 14, 16, 18)
                sys.canvas.setStrokeColor("#223355")
                sys.canvas.setStrokeWidth(1)
                sys.canvas.drawRect(bx, 14, 16, 18)
            end
        end
        sys.canvas.setAlpha(255)
    end

    -- Restore screenshake transform
    if screenshake_time > 0 then
        sys.canvas.restore()
    end

    -- ── POST-PROCESS ─────────────────────────────────────
    if prog > 0 then
        sys.gl.drawFullscreen(prog)
    end

    sys.animation.requestFrame(frame)
end

sys.animation.requestFrame(frame)
