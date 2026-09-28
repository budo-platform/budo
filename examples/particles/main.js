/**
 * Particles Demo - Budo
 * 
 * Demonstrates particle systems with various effects:
 * - Fireworks
 * - Fire/Smoke
 * - Snow
 * - Confetti
 */

let time = 0;
let currentEffect = 0;
const effects = ['Fireworks', 'Fire', 'Snow', 'Confetti', 'Fountain'];

// Particle class
class Particle {
    constructor(x, y, vx, vy, color, life, size) {
        this.x = x;
        this.y = y;
        this.vx = vx;
        this.vy = vy;
        this.color = color;
        this.life = life;
        this.maxLife = life;
        this.size = size;
        this.rotation = Math.random() * 360;
        this.rotationSpeed = (Math.random() - 0.5) * 10;
    }

    update(dt, gravity = 0) {
        this.x += this.vx * dt;
        this.y += this.vy * dt;
        this.vy += gravity * dt;
        this.life -= dt;
        this.rotation += this.rotationSpeed;
    }

    isDead() {
        return this.life <= 0;
    }

    getAlpha() {
        return Math.max(0, Math.min(255, (this.life / this.maxLife) * 255));
    }
}

// Particle system
class ParticleSystem {
    constructor() {
        this.particles = [];
    }

    add(particle) {
        this.particles.push(particle);
    }

    update(dt, gravity = 0) {
        for (let i = this.particles.length - 1; i >= 0; i--) {
            this.particles[i].update(dt, gravity);
            if (this.particles[i].isDead()) {
                this.particles.splice(i, 1);
            }
        }
    }

    clear() {
        this.particles = [];
    }
}

// Color helpers
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

function randomColor() {
    return hslToHex(Math.random(), 0.8, 0.5);
}

// Create particle systems
const fireSystem = new ParticleSystem();
const explosionSystem = new ParticleSystem();
const snowSystem = new ParticleSystem();
const confettiSystem = new ParticleSystem();
const fountainSystem = new ParticleSystem();

// Firework rockets
const rockets = [];

// Spawn a firework rocket
function spawnRocket(width, height) {
    rockets.push({
        x: width * (0.2 + Math.random() * 0.6),
        y: height,
        vy: -400 - Math.random() * 200,
        color: randomColor(),
        exploded: false
    });
}

// Explode a rocket into particles
function explodeRocket(rocket) {
    const particleCount = 50 + Math.floor(Math.random() * 50);

    for (let i = 0; i < particleCount; i++) {
        const angle = (i / particleCount) * Math.PI * 2 + Math.random() * 0.5;
        const speed = 100 + Math.random() * 150;

        explosionSystem.add(new Particle(
            rocket.x,
            rocket.y,
            Math.cos(angle) * speed,
            Math.sin(angle) * speed,
            rocket.color,
            1 + Math.random() * 1.5,
            3 + Math.random() * 3
        ));
    }
}

// Update fireworks
function updateFireworks(dt, width, height) {
    // Update rockets
    for (let i = rockets.length - 1; i >= 0; i--) {
        const rocket = rockets[i];
        rocket.y += rocket.vy * dt;
        rocket.vy += 200 * dt; // Gravity

        // Explode when slowing down
        if (rocket.vy > -50 && !rocket.exploded) {
            explodeRocket(rocket);
            rocket.exploded = true;
            rockets.splice(i, 1);
        }
    }

    // Update explosion particles
    explosionSystem.update(dt, 100);

    // Spawn new rockets
    if (Math.random() < dt * 1.5) {
        spawnRocket(width, height);
    }
}

// Draw fireworks
function drawFireworks(width, height) {
    // Draw rockets
    for (const rocket of rockets) {
        // Rocket trail
        sys.canvas.setFillColor(rocket.color);
        sys.canvas.setAlpha(200);
        sys.canvas.drawCircle(rocket.x, rocket.y, 4);

        // Trail particles
        for (let i = 0; i < 3; i++) {
            sys.canvas.setAlpha(100 - i * 30);
            sys.canvas.drawCircle(rocket.x + (Math.random() - 0.5) * 4, rocket.y + i * 10, 2);
        }
    }
    sys.canvas.setAlpha(255);

    // Draw explosion particles
    for (const p of explosionSystem.particles) {
        sys.canvas.setFillColor(p.color);
        sys.canvas.setAlpha(p.getAlpha());
        sys.canvas.drawCircle(p.x, p.y, p.size * (p.life / p.maxLife));
    }
    sys.canvas.setAlpha(255);
}

// Fire effect
function updateFire(dt, cx, cy) {
    // Spawn fire particles
    const spawnRate = 100;
    const toSpawn = spawnRate * dt;

    for (let i = 0; i < toSpawn; i++) {
        const offset = (Math.random() - 0.5) * 60;
        fireSystem.add(new Particle(
            cx + offset,
            cy,
            (Math.random() - 0.5) * 30,
            -50 - Math.random() * 100,
            hslToHex(0.08 - Math.random() * 0.08, 1, 0.5), // Orange-red
            0.5 + Math.random() * 1,
            10 + Math.random() * 15
        ));
    }

    fireSystem.update(dt, -50); // Negative gravity (rises)
}

function drawFire() {
    for (const p of fireSystem.particles) {
        const life = p.life / p.maxLife;
        // Change color based on life (red -> orange -> yellow)
        const hue = 0.02 + life * 0.08;
        sys.canvas.setFillColor(hslToHex(hue, 1, 0.4 + life * 0.2));
        sys.canvas.setAlpha(p.getAlpha() * 0.8);
        sys.canvas.drawCircle(p.x, p.y, p.size * life);
    }
    sys.canvas.setAlpha(255);
}

// Snow effect
function updateSnow(dt, width) {
    // Spawn snowflakes
    const spawnRate = 30;
    const toSpawn = spawnRate * dt;

    for (let i = 0; i < toSpawn; i++) {
        snowSystem.add(new Particle(
            Math.random() * width,
            -10,
            (Math.random() - 0.5) * 20,
            30 + Math.random() * 50,
            '#FFFFFF',
            10 + Math.random() * 5,
            2 + Math.random() * 4
        ));
    }

    // Update with wind
    for (const p of snowSystem.particles) {
        p.vx += Math.sin(time * 2 + p.y * 0.01) * 0.5;
    }

    snowSystem.update(dt, 0);
}

function drawSnow() {
    for (const p of snowSystem.particles) {
        sys.canvas.setFillColor('#FFFFFF');
        sys.canvas.setAlpha(200);
        sys.canvas.drawCircle(p.x, p.y, p.size);

        // Sparkle effect
        if (Math.random() < 0.01) {
            sys.canvas.setAlpha(255);
            sys.canvas.drawCircle(p.x, p.y, p.size * 1.5);
        }
    }
    sys.canvas.setAlpha(255);
}

// Confetti effect
function updateConfetti(dt, width, height, input) {
    // Spawn on click
    if (input.mouse.leftPressed) {
        for (let i = 0; i < 30; i++) {
            const angle = Math.random() * Math.PI * 2;
            const speed = 100 + Math.random() * 200;
            confettiSystem.add(new Particle(
                input.mouse.x,
                input.mouse.y,
                Math.cos(angle) * speed,
                Math.sin(angle) * speed - 100,
                randomColor(),
                2 + Math.random() * 2,
                5 + Math.random() * 10
            ));
        }
    }

    confettiSystem.update(dt, 300);
}

function drawConfetti() {
    for (const p of confettiSystem.particles) {
        sys.canvas.save();
        sys.canvas.translate(p.x, p.y);
        sys.canvas.rotate(p.rotation);

        sys.canvas.setFillColor(p.color);
        sys.canvas.setAlpha(p.getAlpha());
        sys.canvas.drawRect(-p.size / 2, -p.size / 4, p.size, p.size / 2);

        sys.canvas.restore();
    }
    sys.canvas.setAlpha(255);
}

// Fountain effect
function updateFountain(dt, cx, cy) {
    // Spawn particles
    const spawnRate = 80;
    const toSpawn = spawnRate * dt;

    for (let i = 0; i < toSpawn; i++) {
        const angle = -Math.PI / 2 + (Math.random() - 0.5) * 0.5;
        const speed = 200 + Math.random() * 100;

        fountainSystem.add(new Particle(
            cx + (Math.random() - 0.5) * 10,
            cy,
            Math.cos(angle) * speed,
            Math.sin(angle) * speed,
            hslToHex(0.55 + Math.random() * 0.1, 0.8, 0.6), // Blue-cyan
            2 + Math.random() * 1,
            3 + Math.random() * 3
        ));
    }

    fountainSystem.update(dt, 400);
}

function drawFountain() {
    for (const p of fountainSystem.particles) {
        sys.canvas.setFillColor(p.color);
        sys.canvas.setAlpha(p.getAlpha());
        sys.canvas.drawCircle(p.x, p.y, p.size);
    }
    sys.canvas.setAlpha(255);
}

// Main animation loop
function animate(timestamp) {
    const dt = Math.min(0.05, (timestamp - (time * 1000)) / 1000);
    time = timestamp / 1000;

    const width = sys.window.getWidth();
    const height = sys.window.getHeight();
    const input = sys.input.get();

    // Handle effect switching
    if (input.mouse.rightPressed) {
        currentEffect = (currentEffect + 1) % effects.length;
        // Clear all systems
        fireSystem.clear();
        explosionSystem.clear();
        snowSystem.clear();
        confettiSystem.clear();
        fountainSystem.clear();
        rockets.length = 0;
    }

    // Background based on effect
    switch (currentEffect) {
        case 0: // Fireworks
            sys.canvas.clear('#0A0A15');
            updateFireworks(dt, width, height);
            drawFireworks(width, height);
            break;

        case 1: // Fire
            sys.canvas.clear('#1A0A0A');
            updateFire(dt, width / 2, height - 50);
            drawFire();
            // Draw base
            sys.canvas.setFillColor('#2A1A1A');
            sys.canvas.drawRect(width / 2 - 50, height - 30, 100, 30);
            break;

        case 2: // Snow
            sys.canvas.clear('#1A2A3A');
            // Draw ground
            sys.canvas.setFillColor('#FFFFFF');
            sys.canvas.drawRect(0, height - 30, width, 30);
            // Draw trees
            for (let i = 0; i < 5; i++) {
                const tx = 100 + i * (width - 200) / 4;
                sys.canvas.setFillColor('#2A4A2A');
                sys.canvas.drawCircle(tx, height - 80, 40);
                sys.canvas.drawCircle(tx, height - 120, 30);
                sys.canvas.drawCircle(tx, height - 150, 20);
                sys.canvas.setFillColor('#4A3A2A');
                sys.canvas.drawRect(tx - 8, height - 50, 16, 30);
            }
            updateSnow(dt, width);
            drawSnow();
            break;

        case 3: // Confetti
            sys.canvas.clear('#2A2A4A');
            updateConfetti(dt, width, height, input);
            drawConfetti();
            // Instructions
            sys.canvas.setFillColor('#AAA');
            sys.canvas.drawText('Click anywhere to burst confetti!', width / 2 - 120, height / 2, 18);
            break;

        case 4: // Fountain
            sys.canvas.clear('#0A1A2A');
            updateFountain(dt, width / 2, height - 20);
            drawFountain();
            // Draw basin
            sys.canvas.setFillColor('#1A3A5A');
            sys.canvas.drawRoundRect(width / 2 - 80, height - 30, 160, 40, 10, 10);
            break;
    }

    // UI - Effect name
    sys.canvas.setFillColor('#FFFFFF');
    sys.canvas.setAlpha(200);
    sys.canvas.drawRoundRect(10, 10, 200, 50, 8, 8);
    sys.canvas.setAlpha(255);

    sys.canvas.setFillColor('#333');
    sys.canvas.drawText('Particle Effect:', 20, 35, 14);
    sys.canvas.setFillColor('#E74C3C');
    sys.canvas.drawText(effects[currentEffect], 20, 52, 18);

    // Particle count
    let totalParticles = fireSystem.particles.length +
        explosionSystem.particles.length +
        snowSystem.particles.length +
        confettiSystem.particles.length +
        fountainSystem.particles.length +
        rockets.length;

    sys.canvas.setFillColor('#666');
    sys.canvas.drawText(`Particles: ${totalParticles}`, width - 130, 30, 14);

    // Instructions
    sys.canvas.setFillColor('#888');
    sys.canvas.drawText('Right-click to switch effects | ESC to exit', 20, height - 15, 14);

    sys.animation.requestFrame(animate);
}

sys.log('Starting particles demo...');
sys.log('Right-click to switch between effects');
sys.animation.requestFrame(animate);
