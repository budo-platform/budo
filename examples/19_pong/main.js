/**
 * Pong Game - Budo
 * 
 * Classic Pong game with:
 * - Player vs AI
 * - Score tracking
 * - Visual effects
 * - Sound feedback (visual)
 */

// Game constants
const PADDLE_WIDTH = 15;
const PADDLE_HEIGHT = 100;
const BALL_SIZE = 12;
const PADDLE_SPEED = 400;
const BALL_BASE_SPEED = 350;
const AI_SPEED = 280;
const WINNING_SCORE = 5;

// Colors
const COLORS = {
    bg: '#0A0A1A',
    paddle1: '#4ECDC4',
    paddle2: '#FF6B6B',
    ball: '#FFFFFF',
    text: '#FFFFFF',
    court: '#1A1A2A',
    line: '#2A2A4A',
    glow: '#4ECDC4',
};

// Game state
let gameState = 'start'; // start, playing, paused, gameOver
let player1Score = 0;
let player2Score = 0;

// Paddle positions
let paddle1Y = 0;
let paddle2Y = 0;

// Ball state
let ballX = 0;
let ballY = 0;
let ballVX = 0;
let ballVY = 0;

// Trail effect
let ballTrail = [];
const MAX_TRAIL = 15;

// Screen shake
let shakeX = 0;
let shakeY = 0;
let shakeDuration = 0;

// Flash effect
let flashAlpha = 0;
let flashColor = COLORS.paddle1;

// Initialize game
function initGame(width, height) {
    paddle1Y = height / 2 - PADDLE_HEIGHT / 2;
    paddle2Y = height / 2 - PADDLE_HEIGHT / 2;
    resetBall(width, height, Math.random() > 0.5 ? 1 : -1);
    ballTrail = [];
}

// Reset ball to center
function resetBall(width, height, direction = 1) {
    ballX = width / 2;
    ballY = height / 2;

    // Random angle between -45 and 45 degrees
    const angle = (Math.random() - 0.5) * Math.PI / 2;
    ballVX = Math.cos(angle) * BALL_BASE_SPEED * direction;
    ballVY = Math.sin(angle) * BALL_BASE_SPEED;

    ballTrail = [];
}

// Trigger screen shake
function triggerShake(intensity = 10, duration = 0.2) {
    shakeDuration = duration;
    shakeX = (Math.random() - 0.5) * intensity;
    shakeY = (Math.random() - 0.5) * intensity;
}

// Trigger flash effect
function triggerFlash(color) {
    flashAlpha = 200;
    flashColor = color;
}

// Update paddle positions
function updatePaddles(dt, height, input) {
    // Player 1 (left) - follows mouse
    const targetY = input.mouse.y - PADDLE_HEIGHT / 2;
    const diff = targetY - paddle1Y;
    paddle1Y += diff * 10 * dt;

    // Clamp paddle position
    paddle1Y = Math.max(0, Math.min(height - PADDLE_HEIGHT, paddle1Y));

    // AI Player 2 (right)
    const ballTargetY = ballY - PADDLE_HEIGHT / 2;
    const aiDiff = ballTargetY - paddle2Y;

    // AI only reacts when ball is coming towards it
    if (ballVX > 0) {
        if (Math.abs(aiDiff) > 5) {
            const aiMove = Math.sign(aiDiff) * AI_SPEED * dt;
            paddle2Y += aiMove;
        }
    }

    paddle2Y = Math.max(0, Math.min(height - PADDLE_HEIGHT, paddle2Y));
}

// Update ball position
function updateBall(dt, width, height) {
    // Store trail
    ballTrail.unshift({ x: ballX, y: ballY });
    if (ballTrail.length > MAX_TRAIL) {
        ballTrail.pop();
    }

    // Move ball
    ballX += ballVX * dt;
    ballY += ballVY * dt;

    // Wall collision (top/bottom)
    if (ballY - BALL_SIZE / 2 < 0) {
        ballY = BALL_SIZE / 2;
        ballVY = Math.abs(ballVY);
        triggerShake(5, 0.1);
    }
    if (ballY + BALL_SIZE / 2 > height) {
        ballY = height - BALL_SIZE / 2;
        ballVY = -Math.abs(ballVY);
        triggerShake(5, 0.1);
    }

    // Paddle collision (left)
    if (ballX - BALL_SIZE / 2 < 40 + PADDLE_WIDTH) {
        if (ballY >= paddle1Y && ballY <= paddle1Y + PADDLE_HEIGHT) {
            ballX = 40 + PADDLE_WIDTH + BALL_SIZE / 2;

            // Calculate bounce angle based on hit position
            const hitPos = (ballY - paddle1Y) / PADDLE_HEIGHT;
            const angle = (hitPos - 0.5) * Math.PI / 2;
            const speed = Math.sqrt(ballVX * ballVX + ballVY * ballVY) * 1.05; // Speed up

            ballVX = Math.cos(angle) * Math.min(speed, BALL_BASE_SPEED * 2);
            ballVY = Math.sin(angle) * Math.min(speed, BALL_BASE_SPEED * 2);

            triggerShake(8, 0.15);
            triggerFlash(COLORS.paddle1);
        }
    }

    // Paddle collision (right)
    if (ballX + BALL_SIZE / 2 > width - 40 - PADDLE_WIDTH) {
        if (ballY >= paddle2Y && ballY <= paddle2Y + PADDLE_HEIGHT) {
            ballX = width - 40 - PADDLE_WIDTH - BALL_SIZE / 2;

            const hitPos = (ballY - paddle2Y) / PADDLE_HEIGHT;
            const angle = Math.PI - (hitPos - 0.5) * Math.PI / 2;
            const speed = Math.sqrt(ballVX * ballVX + ballVY * ballVY) * 1.05;

            ballVX = Math.cos(angle) * Math.min(speed, BALL_BASE_SPEED * 2);
            ballVY = Math.sin(angle) * Math.min(speed, BALL_BASE_SPEED * 2);

            triggerShake(8, 0.15);
            triggerFlash(COLORS.paddle2);
        }
    }

    // Score detection
    if (ballX < 0) {
        player2Score++;
        triggerShake(20, 0.3);
        triggerFlash(COLORS.paddle2);

        if (player2Score >= WINNING_SCORE) {
            gameState = 'gameOver';
        } else {
            resetBall(width, height, 1);
        }
    }

    if (ballX > width) {
        player1Score++;
        triggerShake(20, 0.3);
        triggerFlash(COLORS.paddle1);

        if (player1Score >= WINNING_SCORE) {
            gameState = 'gameOver';
        } else {
            resetBall(width, height, -1);
        }
    }
}

// Update effects
function updateEffects(dt) {
    // Shake decay
    if (shakeDuration > 0) {
        shakeDuration -= dt;
        shakeX *= 0.9;
        shakeY *= 0.9;
    } else {
        shakeX = 0;
        shakeY = 0;
    }

    // Flash decay
    if (flashAlpha > 0) {
        flashAlpha -= 500 * dt;
    }
}

// Draw court
function drawCourt(width, height) {
    // Background
    sys.canvas.clear(COLORS.bg);

    // Court rectangle
    sys.canvas.setFillColor(COLORS.court);
    sys.canvas.drawRect(20, 20, width - 40, height - 40);

    // Center line (dashed effect)
    sys.canvas.setStrokeColor(COLORS.line);
    sys.canvas.setStrokeWidth(4);

    const dashHeight = 20;
    const gap = 15;
    for (let y = 30; y < height - 30; y += dashHeight + gap) {
        sys.canvas.drawLine(width / 2, y, width / 2, y + dashHeight);
    }

    // Center circle
    sys.canvas.setStrokeColor(COLORS.line);
    sys.canvas.setStrokeWidth(2);
    sys.canvas.drawCircle(width / 2, height / 2, 50);
}

// Draw paddles with glow
function drawPaddles(width, height) {
    // Player 1 paddle glow
    sys.canvas.setFillColor(COLORS.paddle1);
    sys.canvas.setAlpha(50);
    sys.canvas.drawRoundRect(40 - 5, paddle1Y - 5, PADDLE_WIDTH + 10, PADDLE_HEIGHT + 10, 8, 8);
    sys.canvas.setAlpha(255);

    // Player 1 paddle
    sys.canvas.setFillColor(COLORS.paddle1);
    sys.canvas.drawRoundRect(40, paddle1Y, PADDLE_WIDTH, PADDLE_HEIGHT, 5, 5);

    // Player 2 paddle glow
    sys.canvas.setFillColor(COLORS.paddle2);
    sys.canvas.setAlpha(50);
    sys.canvas.drawRoundRect(width - 40 - PADDLE_WIDTH - 5, paddle2Y - 5, PADDLE_WIDTH + 10, PADDLE_HEIGHT + 10, 8, 8);
    sys.canvas.setAlpha(255);

    // Player 2 paddle
    sys.canvas.setFillColor(COLORS.paddle2);
    sys.canvas.drawRoundRect(width - 40 - PADDLE_WIDTH, paddle2Y, PADDLE_WIDTH, PADDLE_HEIGHT, 5, 5);
}

// Draw ball with trail
function drawBall() {
    // Draw trail
    for (let i = 0; i < ballTrail.length; i++) {
        const alpha = 150 * (1 - i / ballTrail.length);
        const size = BALL_SIZE * (1 - i / ballTrail.length * 0.5);

        sys.canvas.setFillColor(COLORS.ball);
        sys.canvas.setAlpha(alpha);
        sys.canvas.drawCircle(ballTrail[i].x, ballTrail[i].y, size / 2);
    }
    sys.canvas.setAlpha(255);

    // Ball glow
    sys.canvas.setFillColor(COLORS.ball);
    sys.canvas.setAlpha(100);
    sys.canvas.drawCircle(ballX, ballY, BALL_SIZE);
    sys.canvas.setAlpha(255);

    // Ball
    sys.canvas.setFillColor(COLORS.ball);
    sys.canvas.drawCircle(ballX, ballY, BALL_SIZE / 2);
}

// Draw scores
function drawScores(width) {
    sys.canvas.setFillColor(COLORS.paddle1);
    sys.canvas.drawText(player1Score.toString(), width / 4, 80, 64);

    sys.canvas.setFillColor(COLORS.paddle2);
    sys.canvas.drawText(player2Score.toString(), width * 3 / 4 - 20, 80, 64);
}

// Draw flash effect
function drawFlash(width, height) {
    if (flashAlpha > 0) {
        sys.canvas.setFillColor(flashColor);
        sys.canvas.setAlpha(flashAlpha);
        sys.canvas.drawRect(0, 0, width, height);
        sys.canvas.setAlpha(255);
    }
}

// Draw start screen
function drawStartScreen(width, height) {
    sys.canvas.clear(COLORS.bg);

    sys.canvas.setFillColor(COLORS.text);
    sys.canvas.drawText('PONG', width / 2 - 80, height / 2 - 50, 72);

    sys.canvas.setFillColor(COLORS.paddle1);
    sys.canvas.drawText('Player', width / 2 - 120, height / 2 + 30, 24);
    sys.canvas.setFillColor(COLORS.text);
    sys.canvas.drawText('vs', width / 2 - 15, height / 2 + 30, 24);
    sys.canvas.setFillColor(COLORS.paddle2);
    sys.canvas.drawText('AI', width / 2 + 30, height / 2 + 30, 24);

    sys.canvas.setFillColor('#888');
    sys.canvas.drawText('Click to start', width / 2 - 60, height / 2 + 80, 18);
    sys.canvas.drawText('Move mouse to control paddle', width / 2 - 115, height / 2 + 110, 16);
}

// Draw game over screen
function drawGameOverScreen(width, height) {
    // Dim background
    sys.canvas.setFillColor('#000000');
    sys.canvas.setAlpha(150);
    sys.canvas.drawRect(0, 0, width, height);
    sys.canvas.setAlpha(255);

    // Winner text
    const winner = player1Score >= WINNING_SCORE ? 'PLAYER' : 'AI';
    const winnerColor = player1Score >= WINNING_SCORE ? COLORS.paddle1 : COLORS.paddle2;

    sys.canvas.setFillColor(winnerColor);
    sys.canvas.drawText(winner, width / 2 - 80, height / 2 - 30, 48);

    sys.canvas.setFillColor(COLORS.text);
    sys.canvas.drawText('WINS!', width / 2 - 55, height / 2 + 20, 48);

    sys.canvas.setFillColor('#888');
    sys.canvas.drawText('Click to play again', width / 2 - 80, height / 2 + 80, 18);
}

// Main animation loop
function animate(timestamp) {
    const width = sys.window.getWidth();
    const height = sys.window.getHeight();
    const input = sys.input.get();
    const dt = Math.min(0.05, input.deltaTime);

    // Handle game state
    if (gameState === 'start') {
        drawStartScreen(width, height);

        if (input.mouse.leftPressed) {
            gameState = 'playing';
            player1Score = 0;
            player2Score = 0;
            initGame(width, height);
        }
    } else if (gameState === 'playing') {
        // Update
        updatePaddles(dt, height, input);
        updateBall(dt, width, height);
        updateEffects(dt);

        // Apply shake
        sys.canvas.save();
        sys.canvas.translate(shakeX, shakeY);

        // Draw
        drawCourt(width, height);
        drawPaddles(width, height);
        drawBall();
        drawScores(width);

        sys.canvas.restore();

        // Flash on top
        drawFlash(width, height);

        // Instructions
        sys.canvas.setFillColor('#555');
        sys.canvas.drawText('Move mouse to control | First to 5 wins | ESC to exit', 20, height - 15, 12);

    } else if (gameState === 'gameOver') {
        // Keep showing the final state
        drawCourt(width, height);
        drawPaddles(width, height);
        drawScores(width);

        drawGameOverScreen(width, height);

        if (input.mouse.leftPressed) {
            gameState = 'playing';
            player1Score = 0;
            player2Score = 0;
            initGame(width, height);
        }
    }

    sys.animation.requestFrame(animate);
}

sys.log('Starting Pong game...');
sys.animation.requestFrame(animate);
