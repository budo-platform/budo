/**
 * Network Demo
 *
 * Demonstrates the fetch() API by loading data from a public REST API
 * and displaying the results on screen.
 *
 * Requires app.json with a "network" policy (see app.json in this folder).
 */

// ============================================
// State
// ============================================

const state = {
    status: 'idle',       // idle | loading | loaded | error
    posts: [],
    errorMsg: '',
    scrollY: 0,
    hoveredPost: -1,
    selectedPost: null,
    fetchTime: 0,
};

// ============================================
// Data fetching
// ============================================

function loadPosts() {
    state.status = 'loading';
    state.errorMsg = '';

    const startTime = sys.input.get().totalTime;
    sys.net.fetch('https://jsonplaceholder.typicode.com/posts?_limit=20')
        .then(response => {
            if (!response.ok) {
                state.status = 'error';
                state.errorMsg = `HTTP ${response.status}: ${response.statusText}`;
                return;
            }

            state.posts = response.json();
            state.fetchTime = sys.input.get().totalTime - startTime;
            state.status = 'loaded';
            sys.log(`Fetched ${state.posts.length} posts in ${state.fetchTime.toFixed(2)}s`);
        })
        .catch(e => {
            state.status = 'error';
            state.errorMsg = String(e);
            sys.log('Fetch error: ' + state.errorMsg);
        });
}

function loadPostDetail(id) {
    sys.net.fetch(`https://jsonplaceholder.typicode.com/posts/${id}`)
        .then(response => {
            if (response.ok) {
                state.selectedPost = response.json();
            }
        })
        .catch(e => {
            sys.log('Detail fetch error: ' + e);
        });
}

// ============================================
// Rendering
// ============================================

const COLORS = {
    bg: '#F0F4F8',
    card: '#FFFFFF',
    cardHover: '#EBF0F7',
    primary: '#2563EB',
    primaryDark: '#1D4ED8',
    text: '#1E293B',
    textSecondary: '#64748B',
    border: '#E2E8F0',
    success: '#10B981',
    error: '#EF4444',
    loading: '#F59E0B',
};

const PADDING = 20;
const CARD_HEIGHT = 120;
const CARD_GAP = 10;
const HEADER_HEIGHT = 120;
const BUTTON_HEIGHT = 60;
const BUTTON_WIDTH = 240;

function drawHeader(width) {
    // Header background
    sys.canvas.setFillColor(COLORS.primary);
    sys.canvas.drawRect(0, 0, width, HEADER_HEIGHT);

    // Title
    sys.canvas.setFillColor(COLORS.text);
    sys.canvas.drawText('Network Demo', PADDING, 40, 44);

    // Status
    let statusText = '';
    let statusColor = '#FFFFFF';
    if (state.status === 'idle') {
        statusText = 'Press "Load" to fetch data';
    } else if (state.status === 'loading') {
        statusText = 'Loading...';
        statusColor = COLORS.loading;
    } else if (state.status === 'loaded') {
        statusText = `${state.posts.length} posts loaded in ${state.fetchTime.toFixed(2)}s`;
        statusColor = COLORS.success;
    } else if (state.status === 'error') {
        statusText = `Error: ${state.errorMsg}`;
        statusColor = COLORS.error;
    }

    sys.canvas.setFillColor(statusColor);
    sys.canvas.drawText(statusText, PADDING, 85, 26);

    // Load button
    const btnX = width - PADDING - BUTTON_WIDTH;
    const btnY = (HEADER_HEIGHT - BUTTON_HEIGHT) / 2;

    sys.canvas.setFillColor('#FFFFFF');
    sys.canvas.drawRoundRect(btnX, btnY, BUTTON_WIDTH, BUTTON_HEIGHT, 6, 6);

    sys.canvas.setFillColor(COLORS.primary);
    sys.canvas.drawText(
        state.status === 'loading' ? 'Loading...' : 'Load Posts',
        btnX + 30, btnY + 22, 32
    );

    return { btnX, btnY };
}

function drawPostList(width, height) {
    const startY = HEADER_HEIGHT + PADDING;
    const contentWidth = width - PADDING * 2;

    sys.canvas.save();
    sys.canvas.clipRect(0, HEADER_HEIGHT, width, height - HEADER_HEIGHT);
    sys.canvas.translate(0, -state.scrollY);

    for (let i = 0; i < state.posts.length; i++) {
        const post = state.posts[i];
        const y = startY + i * (CARD_HEIGHT + CARD_GAP);

        // Card background
        const isHovered = state.hoveredPost === i;
        sys.canvas.setFillColor(isHovered ? COLORS.cardHover : COLORS.card);
        sys.canvas.drawRoundRect(PADDING, y, contentWidth, CARD_HEIGHT, 8, 8);

        // Card border
        sys.canvas.setStrokeColor(COLORS.border);
        sys.canvas.setStrokeWidth(1);
        sys.canvas.drawRoundRect(PADDING, y, contentWidth, CARD_HEIGHT, 8, 8);

        // Post number
        sys.canvas.setFillColor(COLORS.primary);
        sys.canvas.drawText(`#${post.id}`, PADDING + 12, y + 28, 24);

        // Post title (truncate if needed)
        sys.canvas.setFillColor(COLORS.text);
        let title = post.title;
        if (title.length > 60) title = title.substring(0, 60) + '...';
        sys.canvas.drawText(title, PADDING + 80, y + 28, 28);

        // Post body preview
        sys.canvas.setFillColor(COLORS.textSecondary);
        let body = post.body.replace(/\n/g, ' ');
        if (body.length > 80) body = body.substring(0, 80) + '...';
        sys.canvas.drawText(body, PADDING + 12, y + 80, 24);
    }

    sys.canvas.restore();
}

function drawSelectedPost(width, height) {
    if (!state.selectedPost) return;

    const post = state.selectedPost;
    const margin = 40;
    const cardW = width - margin * 2;
    const cardH = 460;
    const cardX = margin;
    const cardY = (height - cardH) / 2;

    // Overlay
    sys.canvas.setFillColor('#00000060');
    sys.canvas.drawRect(0, 0, width, height);

    // Card
    sys.canvas.setFillColor(COLORS.card);
    sys.canvas.drawRoundRect(cardX, cardY, cardW, cardH, 12, 12);

    // Border
    sys.canvas.setStrokeColor(COLORS.primary);
    sys.canvas.setStrokeWidth(2);
    sys.canvas.drawRoundRect(cardX, cardY, cardW, cardH, 12, 12);

    // Close hint
    sys.canvas.setFillColor(COLORS.textSecondary);
    sys.canvas.drawText('Click anywhere to close', cardX + cardW - 340, cardY + 30, 24);

    // Title
    sys.canvas.setFillColor(COLORS.primary);
    sys.canvas.drawText(`Post #${post.id}`, cardX + 20, cardY + 32, 28);

    sys.canvas.setFillColor(COLORS.text);
    sys.canvas.drawText(post.title, cardX + 20, cardY + 72, 36);

    // Body - word wrap manually
    sys.canvas.setFillColor(COLORS.textSecondary);
    const words = post.body.replace(/\n/g, ' ').split(' ');
    let line = '';
    let lineY = cardY + 120;
    const maxLineWidth = cardW - 40;
    const charWidth = 14; // approximate

    for (const word of words) {
        const testLine = line ? line + ' ' + word : word;
        if (testLine.length * charWidth > maxLineWidth) {
            sys.canvas.drawText(line, cardX + 20, lineY, 28);
            lineY += 40;
            line = word;
        } else {
            line = testLine;
        }
    }
    if (line) {
        sys.canvas.drawText(line, cardX + 20, lineY, 28);
    }
}

function drawEmptyState(width, height) {
    const cx = width / 2;
    const cy = height / 2;

    sys.canvas.setFillColor(COLORS.textSecondary);
    sys.canvas.drawText('No data loaded yet', cx - 160, cy - 10, 36);
    sys.canvas.drawText('Click "Load Posts" to fetch from JSONPlaceholder API', cx - 370, cy + 40, 26);
}

// ============================================
// Input handling
// ============================================

function handleInput(input, width, height) {
    const mouse = input.mouse;

    // Scroll
    if (mouse.wheelY) {
        const maxScroll = Math.max(0, state.posts.length * (CARD_HEIGHT + CARD_GAP) - (height - HEADER_HEIGHT - PADDING * 2));
        state.scrollY = Math.max(0, Math.min(maxScroll, state.scrollY + mouse.wheelY * 40));
    }

    // Hover detection on posts
    state.hoveredPost = -1;
    if (mouse.y > HEADER_HEIGHT && !state.selectedPost) {
        const adjustedY = mouse.y + state.scrollY;
        const startY = HEADER_HEIGHT + PADDING;
        for (let i = 0; i < state.posts.length; i++) {
            const cardY = startY + i * (CARD_HEIGHT + CARD_GAP);
            if (adjustedY >= cardY && adjustedY < cardY + CARD_HEIGHT &&
                mouse.x >= PADDING && mouse.x <= width - PADDING) {
                state.hoveredPost = i;
                break;
            }
        }
    }

    // Click
    if (mouse.leftPressed) {
        // Close detail view
        if (state.selectedPost) {
            state.selectedPost = null;
            return;
        }

        // Load button
        const btnX = width - PADDING - BUTTON_WIDTH;
        const btnY = (HEADER_HEIGHT - BUTTON_HEIGHT) / 2;
        if (mouse.x >= btnX && mouse.x <= btnX + BUTTON_WIDTH &&
            mouse.y >= btnY && mouse.y <= btnY + BUTTON_HEIGHT) {
            if (state.status !== 'loading') {
                loadPosts();
            }
            return;
        }

        // Post click
        if (state.hoveredPost >= 0) {
            loadPostDetail(state.posts[state.hoveredPost].id);
        }
    }
}

// ============================================
// Main loop
// ============================================

function frame(timestamp) {
    const width = sys.window.getWidth();
    const height = sys.window.getHeight();
    const input = sys.input.get();

    sys.canvas.clear(COLORS.bg);

    handleInput(input, width, height);

    drawHeader(width);

    if (state.posts.length > 0) {
        drawPostList(width, height);
    } else if (state.status !== 'loading') {
        drawEmptyState(width, height);
    } else {
        // Loading indicator
        sys.canvas.setFillColor(COLORS.loading);
        const cx = width / 2;
        const cy = height / 2;
        const angle = (timestamp / 500) * 360;
        sys.canvas.save();
        sys.canvas.translate(cx, cy);
        sys.canvas.rotate(angle);
        sys.canvas.drawRect(-20, -3, 40, 6);
        sys.canvas.restore();
    }

    drawSelectedPost(width, height);

    sys.animation.requestFrame(frame);
}

sys.log('Starting Network Demo...');
sys.animation.requestFrame(frame);
