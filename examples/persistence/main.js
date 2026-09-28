/**
 * Persistence Example - TODO List Manager
 *
 * Demonstrates the SQLite persistence API with a fully interactive
 * TODO list that survives application restarts.
 *
 * Features:
 * - Add, complete, and delete tasks
 * - Data persisted in a SQLite database
 * - Click to complete/uncomplete tasks
 * - Visual feedback for interactions
 */

import { render, handleKeyboard, handleMouse } from './ui.js';

// ============================================
// Database setup
// ============================================

const database = sys.db.open('todos');

sys.db.execute(database, `
    CREATE TABLE IF NOT EXISTS tasks (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        title TEXT NOT NULL,
        done INTEGER DEFAULT 0,
        created_at TEXT DEFAULT (datetime('now'))
    )
`);

// ============================================
// State
// ============================================

const state = {
    tasks: [],
    inputText: '',
    cursorBlink: 0,
    scrollY: 0,
    hoveredTask: -1,
    hoveredButton: '',
    lastClickTime: 0,
};

// ============================================
// Data operations
// ============================================

function loadTasks() {
    state.tasks = sys.db.query(database, 'SELECT * FROM tasks ORDER BY done ASC, created_at DESC');
}

function addTask(title) {
    if (!title.trim()) return;
    sys.db.run(database, 'INSERT INTO tasks (title) VALUES (?)', title.trim());
    loadTasks();
}

function toggleTask(id) {
    sys.db.run(database, 'UPDATE tasks SET done = CASE WHEN done = 0 THEN 1 ELSE 0 END WHERE id = ?', id);
    loadTasks();
}

function deleteTask(id) {
    sys.db.run(database, 'DELETE FROM tasks WHERE id = ?', id);
    loadTasks();
}

function clearCompleted() {
    sys.db.run(database, 'DELETE FROM tasks WHERE done = 1');
    loadTasks();
}

const actions = { addTask, toggleTask, deleteTask, clearCompleted };

// Initial load
loadTasks();

// ============================================
// Main loop
// ============================================

function animate(timestamp) {
    const width = sys.window.getWidth();
    const height = sys.window.getHeight();
    const input = sys.input.get();

    state.cursorBlink += input.deltaTime;

    handleKeyboard(input, state, actions);
    handleMouse(input, state, actions, width, height);
    render(state, width, height);

    sys.animation.requestFrame(animate);
}

sys.log('Starting TODO List with persistence...');
sys.animation.requestFrame(animate);
