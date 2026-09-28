#!/usr/bin/env python3
"""Regenerate website demo pages and the homepage demos carousel."""

from __future__ import annotations

import html
import json
from pathlib import Path
import shutil
import subprocess
import sys
from urllib.parse import quote
from zipfile import ZIP_DEFLATED, ZipFile


ROOT = Path(__file__).resolve().parents[1]
WEBSITE_DIR = ROOT / "website"
DEMOS_DIR = WEBSITE_DIR / "demos"
DOWNLOAD_DEMOS_DIR = WEBSITE_DIR / "download" / "demos"
INDEX_HTML = WEBSITE_DIR / "index.html"

START_MARKER = "<!-- DEMOS:START -->"
END_MARKER = "<!-- DEMOS:END -->"
ENTRY_FILES = ("main.js", "main.lua", "main.ts", "main.wat", "main.wasm")
IMAGE_KEYS = ("image", "thumbnail", "cover", "icon")
GENERATED_PAGE = "index.html"
TEXT_SUFFIXES = {
    ".c", ".cc", ".cpp", ".css", ".frag", ".glsl", ".h", ".hpp", ".html", ".js",
    ".json", ".lua", ".md", ".mjs", ".obj", ".py", ".sh", ".svg", ".ts", ".txt",
    ".vert", ".wat", ".xml",
}
IMAGE_SUFFIXES = {".gif", ".jpeg", ".jpg", ".png", ".svg", ".webp"}
ACRONYMS = {
    "2d": "2D",
    "3d": "3D",
    "ai": "AI",
    "api": "API",
    "crt": "CRT",
    "gl": "GL",
    "obj": "OBJ",
    "ui": "UI",
    "wasm": "WASM",
}
METADATA_FIELDS = (
    "title", "name", "author", "version", "version_name", "package_name",
    "orientation", "min_sdk", "target_sdk", "network", "neural",
)


def pretty_title(slug: str) -> str:
    words = slug.replace("-", "_").split("_")
    return " ".join(ACRONYMS.get(word.lower(), word.capitalize()) for word in words if word)


def demo_url(demo_dir: Path, relative_path: str = "") -> str:
    parts = ["demos", demo_dir.name]
    if relative_path:
        parts.extend(part for part in relative_path.split("/") if part)
    return "/".join(quote(part) for part in parts)


def page_relative_url(relative_path: str) -> str:
    return "/".join(quote(part) for part in relative_path.split("/") if part)


def read_metadata(demo_dir: Path) -> dict:
    metadata_path = demo_dir / "app.json"
    if not metadata_path.exists():
        return {}

    try:
        with metadata_path.open("r", encoding="utf-8") as handle:
            data = json.load(handle)
    except (OSError, json.JSONDecodeError) as exc:
        print(f"warning: skipping invalid metadata in {metadata_path.relative_to(ROOT)}: {exc}", file=sys.stderr)
        return {}

    return data if isinstance(data, dict) else {}


def nested_get(data: dict, path: str) -> object:
    value: object = data
    for key in path.split("."):
        if not isinstance(value, dict):
            return None
        value = value.get(key)
    return value


def resolve_image(demo_dir: Path, metadata: dict) -> str:
    for key in IMAGE_KEYS:
        value = nested_get(metadata, key)
        if isinstance(value, str) and value.strip():
            image_path = demo_dir / value.strip()
            if image_path.exists() and image_path.is_file():
                return demo_url(demo_dir, value.strip())

    screenshots = metadata.get("screenshots")
    if isinstance(screenshots, list):
        for screenshot in screenshots:
            if isinstance(screenshot, str) and screenshot.strip():
                image_path = demo_dir / screenshot.strip()
                if image_path.exists() and image_path.is_file():
                    return demo_url(demo_dir, screenshot.strip())

    return ""


def is_demo_dir(path: Path) -> bool:
    if not path.is_dir() or path.name.startswith(".") or path.name.startswith("_"):
        return False
    return any((path / entry_file).exists() for entry_file in ENTRY_FILES) or (path / "app.json").exists()


def demo_dirs() -> list[Path]:
    if not DEMOS_DIR.exists():
        return []
    return sorted((path for path in DEMOS_DIR.iterdir() if is_demo_dir(path)), key=lambda path: path.name.lower())


def demo_title(demo_dir: Path, metadata: dict) -> str:
    title_value = metadata.get("title") or metadata.get("name")
    return str(title_value).strip() if title_value else pretty_title(demo_dir.name)


def render_card(demo_dir: Path) -> str:
    metadata = read_metadata(demo_dir)
    title = demo_title(demo_dir, metadata)
    image = resolve_image(demo_dir, metadata)
    href = f"{demo_url(demo_dir)}/"

    escaped_title = html.escape(title)
    escaped_slug = html.escape(demo_dir.name)
    escaped_href = html.escape(href, quote=True)

    if image:
        escaped_image = html.escape(image, quote=True)
        thumb = f'<img src="{escaped_image}" alt="{escaped_title}">'
    else:
        initial = html.escape(title[:1].upper() if title else demo_dir.name[:1].upper())
        thumb = f'<span class="demo-thumb-placeholder" aria-hidden="true">{initial}</span>'

    return "\n".join(
        [
            f'            <a href="{escaped_href}" class="demo-card">',
            f'                <div class="demo-thumb">{thumb}</div>',
            '                <div class="demo-meta">',
            f'                    <h3>{escaped_title}</h3>',
            f'                    <span class="demo-slug">{escaped_slug}</span>',
            '                </div>',
            '            </a>',
        ]
    )


def generate_cards() -> str:
    dirs = demo_dirs()
    if not dirs:
        return "            <p class=\"demos-empty\">No demos synced yet.</p>"
    return "\n".join(render_card(demo_dir) for demo_dir in dirs)


def update_homepage() -> None:
    index_text = INDEX_HTML.read_text(encoding="utf-8")
    if START_MARKER not in index_text or END_MARKER not in index_text:
        raise RuntimeError(f"demo markers not found in {INDEX_HTML.relative_to(ROOT)}")

    before, rest = index_text.split(START_MARKER, 1)
    _, after = rest.split(END_MARKER, 1)
    cards = generate_cards()
    next_text = f"{before}{START_MARKER}\n{cards}\n            {END_MARKER}{after}"
    INDEX_HTML.write_text(next_text, encoding="utf-8")


def is_text_file(path: Path) -> bool:
    return path.suffix.lower() in TEXT_SUFFIXES


def is_image_file(path: Path) -> bool:
    return path.suffix.lower() in IMAGE_SUFFIXES


def is_generated_or_hidden_demo_file(relative_path: Path) -> bool:
    return relative_path.as_posix() == GENERATED_PAGE or any(part.startswith(".") for part in relative_path.parts)


def list_demo_files(demo_dir: Path) -> list[dict]:
    files: list[dict] = []
    for path in sorted((p for p in demo_dir.rglob("*") if p.is_file()), key=lambda p: p.relative_to(demo_dir).as_posix().lower()):
        relative_path = path.relative_to(demo_dir)
        if is_generated_or_hidden_demo_file(relative_path):
            continue
        rel = relative_path.as_posix()
        stat = path.stat()
        files.append(
            {
                "path": rel,
                "url": page_relative_url(rel),
                "size": stat.st_size,
                "text": is_text_file(path),
                "image": is_image_file(path),
            }
        )
    return files


def wasm_as_executable() -> str:
    found = shutil.which("wasm-as")
    if found:
        return found
    candidate = ROOT / "third_party" / "emsdk" / "upstream" / "bin" / "wasm-as"
    return str(candidate) if candidate.exists() else ""


def compile_wat_for_browser(demo_dir: Path) -> None:
    wat_path = demo_dir / "main.wat"
    wasm_path = demo_dir / "main.wasm"
    if not wat_path.exists() or wasm_path.exists():
        return

    compiler = wasm_as_executable()
    if not compiler:
        print(f"warning: cannot compile {wat_path.relative_to(ROOT)} to main.wasm: wasm-as not found", file=sys.stderr)
        return

    result = subprocess.run(
        [compiler, str(wat_path), "-o", str(wasm_path)],
        cwd=str(ROOT),
        text=True,
        capture_output=True,
        check=False,
    )
    if result.returncode != 0:
        print(f"warning: failed to compile {wat_path.relative_to(ROOT)}: {result.stderr.strip()}", file=sys.stderr)


def create_demo_zip(demo_dir: Path) -> Path:
    DOWNLOAD_DEMOS_DIR.mkdir(parents=True, exist_ok=True)
    zip_path = DOWNLOAD_DEMOS_DIR / f"{demo_dir.name}.zip"
    with ZipFile(zip_path, "w", compression=ZIP_DEFLATED) as archive:
        for path in sorted((p for p in demo_dir.rglob("*") if p.is_file()), key=lambda p: p.relative_to(demo_dir).as_posix().lower()):
            relative_path = path.relative_to(demo_dir)
            if is_generated_or_hidden_demo_file(relative_path):
                continue
            archive.write(path, Path(demo_dir.name) / relative_path)
    return zip_path


def human_size(size: int) -> str:
    value = float(size)
    for unit in ("B", "KB", "MB", "GB"):
        if value < 1024 or unit == "GB":
            return f"{value:.1f} {unit}" if unit != "B" else f"{int(value)} B"
        value /= 1024
    return f"{size} B"


def json_script(data: object) -> str:
    return json.dumps(data, ensure_ascii=False, separators=(",", ":")).replace("</", "<\\/")


def metadata_rows(metadata: dict, slug: str) -> str:
    rows = [f"<div class=\"meta-row\"><span>slug</span><strong>{html.escape(slug)}</strong></div>"]
    for key in METADATA_FIELDS:
        if key not in metadata:
            continue
        value = metadata[key]
        display = json.dumps(value, ensure_ascii=False) if isinstance(value, (dict, list)) else str(value)
        rows.append(f"<div class=\"meta-row\"><span>{html.escape(key)}</span><strong>{html.escape(display)}</strong></div>")
    return "\n".join(rows)


def file_buttons(files: list[dict]) -> str:
    if not files:
        return '<p class="empty-note">No files found.</p>'

    buttons = []
    for item in files:
        depth = item["path"].count("/")
        label = html.escape(item["path"])
        size = human_size(int(item["size"]))
        buttons.append(
            f'<button class="file-entry" type="button" data-path="{html.escape(item["path"], quote=True)}" '
            f'style="--depth:{depth}"><span>{label}</span><small>{html.escape(size)}</small></button>'
        )
    return "\n".join(buttons)


def first_viewable_file(files: list[dict]) -> str:
    for entry_name in ENTRY_FILES:
        for item in files:
            if item["path"] == entry_name:
                return str(item["path"])
    for item in files:
        if item["text"] or item["image"]:
            return str(item["path"])
    return ""


def render_demo_page(demo_dir: Path) -> str:
    metadata = read_metadata(demo_dir)
    title = demo_title(demo_dir, metadata)
    description = nested_get(metadata, "store_listing.short_description") or metadata.get("description") or ""
    files = list_demo_files(demo_dir)
    first_file = first_viewable_file(files)
    apk_href = f"../../download/demos/{quote(demo_dir.name)}.apk"
    zip_href = f"../../download/demos/{quote(demo_dir.name)}.zip"
    escaped_title = html.escape(title)
    escaped_description = html.escape(str(description))
    file_data = json_script(files)
    initial_file = json_script(first_file)

    return f'''<!DOCTYPE html>
<html lang="en">

<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <link rel="icon" type="image/svg+xml" href="../../budo-icon.svg">
    <link rel="stylesheet" href="../../theme-base.css">
    <link rel="stylesheet" href="../../theme-playful.css">
    <title>{escaped_title} - Budo Demo</title>
    <style>
        body {{ min-height: 100vh; line-height: 1.5; }}
        nav {{ position: sticky; top: 0; z-index: 20; background: rgba(14, 14, 18, .9); backdrop-filter: blur(12px); border-bottom: 1px solid var(--color-border); }}
        .nav-inner {{ max-width: 1440px; margin: 0 auto; padding: .9rem 1.5rem; display: flex; align-items: center; justify-content: space-between; gap: 1rem; }}
        .nav-logo {{ color: var(--color-text); font-size: 1.15rem; font-weight: 700; display: inline-flex; align-items: center; gap: .55rem; }}
        .nav-logo img {{ width: 26px; height: 26px; }}
        .nav-logo span {{ color: var(--color-primary); }}
        .nav-links {{ display: flex; gap: 1.2rem; list-style: none; }}
        .nav-links a {{ color: var(--color-muted); font-size: .9rem; }}
        .nav-links a:hover {{ color: var(--color-text); text-decoration: none; }}
        .demo-shell {{ max-width: 1440px; margin: 0 auto; display: grid; grid-template-columns: minmax(280px, 360px) minmax(0, 1fr); gap: 1.25rem; padding: 1.25rem; min-height: calc(100vh - 68px); }}
        aside {{ display: flex; flex-direction: column; gap: 1rem; min-width: 0; }}
        .panel {{ background: var(--color-card); border: 1px solid var(--color-border); border-radius: 8px; padding: 1rem; }}
        h1 {{ font-size: 1.65rem; line-height: 1.15; margin-bottom: .55rem; }}
        .subtitle {{ color: var(--color-muted); font-size: .95rem; margin-bottom: 1rem; }}
        .meta-list {{ display: grid; gap: .45rem; }}
        .meta-row {{ display: grid; grid-template-columns: minmax(82px, 34%) minmax(0, 1fr); gap: .65rem; align-items: baseline; font-size: .82rem; }}
        .meta-row span {{ color: var(--color-muted); }}
        .meta-row strong {{ color: var(--color-text); font-weight: 600; overflow-wrap: anywhere; }}
        .panel-title {{ color: var(--color-muted); font-size: .78rem; font-weight: 700; text-transform: uppercase; margin-bottom: .75rem; }}
        .file-list {{ display: flex; flex-direction: column; gap: .25rem; max-height: 42vh; overflow: auto; padding-right: .25rem; }}
        .file-entry {{ width: 100%; border: 1px solid transparent; border-radius: 6px; background: transparent; color: var(--color-text); cursor: pointer; display: grid; grid-template-columns: minmax(0, 1fr) auto; gap: .55rem; padding: .45rem .5rem .45rem calc(.5rem + var(--depth) * .75rem); text-align: left; font: inherit; font-size: .84rem; }}
        .file-entry:hover, .file-entry.active {{ border-color: rgba(124, 111, 247, .45); background: rgba(124, 111, 247, .1); }}
        .file-entry span {{ overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }}
        .file-entry small {{ color: var(--color-muted); }}
        .actions {{ display: grid; gap: .7rem; }}
        .btn {{ justify-content: center; border-radius: 8px; min-height: 42px; padding: .68rem 1rem; font-size: .92rem; }}
        .apk-link {{ display: flex; align-items: center; justify-content: center; min-height: 40px; border: 1px solid var(--color-border); border-radius: 8px; color: var(--color-text); font-size: .9rem; font-weight: 600; }}
        .apk-link:hover {{ border-color: var(--color-primary); text-decoration: none; }}
        .workspace {{ min-width: 0; background: var(--color-surface); border: 1px solid var(--color-border); border-radius: 8px; display: grid; grid-template-rows: auto minmax(520px, 1fr); overflow: hidden; }}
        .workspace-bar {{ min-height: 48px; display: flex; align-items: center; justify-content: space-between; gap: 1rem; padding: .75rem 1rem; border-bottom: 1px solid var(--color-border); }}
        .workspace-title {{ min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; font-size: .9rem; color: var(--color-muted); }}
        .workspace-title strong {{ color: var(--color-text); }}
        .workspace-body {{ min-height: 0; position: relative; background: #07070a; }}
        .workspace-body:fullscreen {{ width: 100%; height: 100%; background: #000; }}
        .workspace-body:-webkit-full-screen {{ width: 100%; height: 100%; background: #000; }}
        pre {{ height: 100%; overflow: auto; padding: 1rem; color: #e8e6f0; font-family: var(--font-mono); font-size: .86rem; line-height: 1.55; white-space: pre-wrap; overflow-wrap: anywhere; }}
        .media-preview {{ width: 100%; height: 100%; display: flex; align-items: center; justify-content: center; padding: 1.5rem; }}
        .media-preview img {{ max-width: 100%; max-height: 100%; object-fit: contain; }}
        .run-frame {{ width: 100%; height: 100%; border: 0; display: block; background: #000; }}
        .workspace-body:fullscreen .run-frame {{ height: 100%; }}
        .workspace-body:-webkit-full-screen .run-frame {{ height: 100%; }}
        .fullscreen-btn {{ position: absolute; right: .7rem; bottom: .7rem; z-index: 5; display: inline-flex; align-items: center; justify-content: center; width: 40px; height: 40px; padding: 0; border: 1.5px solid rgba(255,255,255,.85); border-radius: 8px; background: rgba(0,0,0,.65); color: #fff; cursor: pointer; backdrop-filter: blur(6px); box-shadow: 0 2px 10px rgba(0,0,0,.5); transition: background .15s, border-color .15s, transform .15s, box-shadow .15s; }}
        .fullscreen-btn:hover {{ background: rgba(0,0,0,.85); border-color: #fff; box-shadow: 0 2px 14px rgba(255,255,255,.25); }}
        .fullscreen-btn:active {{ transform: scale(.95); }}
        .fullscreen-btn svg {{ width: 18px; height: 18px; display: block; }}
        .empty-state {{ color: var(--color-muted); padding: 1rem; }}
        @media (max-width: 900px) {{ .demo-shell {{ grid-template-columns: 1fr; }} .file-list {{ max-height: 280px; }} .workspace {{ grid-template-rows: auto minmax(520px, 70vh); }} .nav-links {{ display: none; }} }}
    </style>
</head>

<body>
    <nav>
        <div class="nav-inner">
            <a class="nav-logo" href="../../index.html"><img src="../../budo-icon.svg" alt=""><span>Budo</span></a>
            <ul class="nav-links">
                <li><a href="../../index.html#demos">Demos</a></li>
                <li><a href="../../doc/">Docs</a></li>
                <li><a href="../../playground.html">Playground</a></li>
            </ul>
        </div>
    </nav>

    <main class="demo-shell">
        <aside>
            <section class="panel">
                <h1>{escaped_title}</h1>
                <p class="subtitle">{escaped_description}</p>
                <div class="meta-list">{metadata_rows(metadata, demo_dir.name)}</div>
            </section>
            <section class="panel">
                <div class="panel-title">Files</div>
                <div class="file-list" id="file-list">{file_buttons(files)}</div>
            </section>
            <section class="panel actions">
                <button id="run-demo" class="btn btn-primary" type="button">RUN</button>
                <a class="apk-link" href="{html.escape(zip_href, quote=True)}" download>Download demo folder (.zip)</a>
                <a class="apk-link" href="{html.escape(apk_href, quote=True)}" download>Download Android APK</a>
            </section>
        </aside>
        <section class="workspace">
            <div class="workspace-bar">
                <div class="workspace-title" id="workspace-title"><strong>{escaped_title}</strong></div>
                <a id="open-file" href="#" target="_blank" rel="noreferrer">Open file</a>
            </div>
            <div class="workspace-body" id="workspace-body"><p class="empty-state">Select a file or press RUN.</p></div>
        </section>
    </main>

    <script id="demo-files" type="application/json">{file_data}</script>
    <script>
        const DEMO_FILES = JSON.parse(document.getElementById('demo-files').textContent);
        const INITIAL_FILE = {initial_file};
        const fileByPath = new Map(DEMO_FILES.map((file) => [file.path, file]));
        const listEl = document.getElementById('file-list');
        const bodyEl = document.getElementById('workspace-body');
        const titleEl = document.getElementById('workspace-title');
        const openFileEl = document.getElementById('open-file');
        const runButton = document.getElementById('run-demo');

        function escapeHtml(value) {{
            return value.replace(/[&<>"']/g, (char) => ({{ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }}[char]));
        }}

        function setActive(path) {{
            document.querySelectorAll('.file-entry').forEach((entry) => entry.classList.toggle('active', entry.dataset.path === path));
        }}

        async function showFile(path) {{
            const file = fileByPath.get(path);
            if (!file) return;
            setActive(path);
            titleEl.innerHTML = '<strong>' + escapeHtml(path) + '</strong>';
            openFileEl.href = file.url;

            if (file.image) {{
                bodyEl.innerHTML = '<div class="media-preview"><img src="' + file.url + '" alt="' + escapeHtml(path) + '"></div>';
                return;
            }}

            if (!file.text || file.size > 700000) {{
                bodyEl.innerHTML = '<p class="empty-state">Preview is not available for this file. Use Open file to download or view it directly.</p>';
                return;
            }}

            bodyEl.innerHTML = '<p class="empty-state">Loading...</p>';
            const response = await fetch(file.url);
            if (!response.ok) throw new Error('HTTP ' + response.status);
            const text = await response.text();
            bodyEl.innerHTML = '<pre>' + escapeHtml(text) + '</pre>';
        }}

        function runnerDocument() {{
            const files = JSON.stringify(DEMO_FILES.map((file) => ({{ path: file.path, url: new URL(file.url, window.location.href).href }})));
            const runtimeBase = JSON.stringify(new URL('../../', window.location.href).href);
            return `<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1.0,maximum-scale=1.0,user-scalable=no"><style>*{{box-sizing:border-box}}html,body{{width:100%;height:100%;margin:0;overflow:hidden;background:#000;touch-action:none}}#canvas{{position:absolute;inset:0;width:100%;height:100%;outline:0}}#loading,#error{{position:absolute;inset:0;display:flex;align-items:center;justify-content:center;color:#aaa;font-family:system-ui,sans-serif;background:#000}}#error{{display:none;color:#ff7b73;white-space:pre-wrap;padding:24px;align-items:flex-start;justify-content:flex-start;overflow:auto}}</style></head><body tabindex="0"><canvas id="canvas" tabindex="0"></canvas><div id="loading">Loading demo...</div><div id="error"></div><script>const DEMO_FILES=${{files}};const RUNTIME_BASE=${{runtimeBase}};const mountedFiles=[];function focusRuntime(){{try{{window.focus();document.getElementById('canvas').focus({{preventScroll:true}});}}catch(e){{}}}}function showError(message){{document.getElementById('loading').style.display='none';const error=document.getElementById('error');error.textContent=message;error.style.display='flex';}}function dirname(path){{const index=path.lastIndexOf('/');return index>=0?path.slice(0,index):'';}}function basename(path){{const index=path.lastIndexOf('/');return index>=0?path.slice(index+1):path;}}function ensureDir(path){{if(!path)return;let current='';for(const part of path.split('/')){{if(!part)continue;current+='/'+part;try{{FS.mkdir(current);}}catch(e){{}}}}}}async function fetchFiles(){{for(const file of DEMO_FILES){{const response=await fetch(file.url);if(!response.ok)throw new Error('Failed to fetch '+file.path+' (HTTP '+response.status+')');mountedFiles.push({{path:file.path,data:new Uint8Array(await response.arrayBuffer())}});}}}}document.addEventListener('pointerdown',focusRuntime,true);document.addEventListener('mousedown',focusRuntime,true);document.addEventListener('touchstart',focusRuntime,true);focusRuntime();var Module={{canvas:document.getElementById('canvas'),locateFile:function(path){{return new URL(path,RUNTIME_BASE).href;}},preRun:[function(){{for(const file of mountedFiles){{const dir=dirname(file.path);ensureDir(dir);Module.FS_createDataFile('/'+dir,basename(file.path),file.data,true,true,true);}}}}],onRuntimeInitialized:function(){{document.getElementById('loading').style.display='none';focusRuntime();}},print:function(text){{console.log(text);}},printErr:function(text){{console.warn(text);}},onAbort:function(what){{showError('Runtime error: '+what);}}}};fetchFiles().then(function(){{const script=document.createElement('script');script.src=new URL('budo.js',RUNTIME_BASE).href;script.onerror=function(){{showError('Failed to load Budo web runtime. Run make playground or make everything first.');}};document.body.appendChild(script);focusRuntime();}}).catch(function(error){{showError(error && error.stack ? error.stack : String(error));}});<\\/script></body></html>`;
        }}

        function requestFullscreen(element) {{
            if (element.requestFullscreen) return element.requestFullscreen();
            if (element.webkitRequestFullscreen) return Promise.resolve(element.webkitRequestFullscreen());
            if (element.msRequestFullscreen) return Promise.resolve(element.msRequestFullscreen());
            return Promise.resolve();
        }}

        function exitFullscreen() {{
            if (document.exitFullscreen) return document.exitFullscreen();
            if (document.webkitExitFullscreen) return Promise.resolve(document.webkitExitFullscreen());
            if (document.msExitFullscreen) return Promise.resolve(document.msExitFullscreen());
            return Promise.resolve();
        }}

        function fullscreenElement() {{
            return document.fullscreenElement || document.webkitFullscreenElement || document.msFullscreenElement;
        }}

        function fullscreenButton() {{
            const button = document.createElement('button');
            button.className = 'fullscreen-btn';
            button.type = 'button';
            button.title = 'Full screen';
            button.setAttribute('aria-label', 'Enter full screen');
            button.innerHTML = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M4 9V4h5"/><path d="M20 9V4h-5"/><path d="M4 15v5h5"/><path d="M20 15v5h-5"/></svg>';
            button.addEventListener('click', () => {{
                if (fullscreenElement()) {{
                    exitFullscreen().catch(() => {{}});
                }} else {{
                    requestFullscreen(bodyEl).catch(() => {{}});
                }}
            }});
            return button;
        }}

        function syncFullscreenButtonLabel() {{
            const button = bodyEl.querySelector('.fullscreen-btn');
            if (!button) return;
            const inFullscreen = fullscreenElement() === bodyEl;
            button.title = inFullscreen ? 'Exit full screen' : 'Full screen';
            button.setAttribute('aria-label', inFullscreen ? 'Exit full screen' : 'Enter full screen');
        }}

        function refreshRunnerSize() {{
            const iframe = bodyEl.querySelector('.run-frame');
            if (!iframe) return;
            iframe.style.height = '100%';
            iframe.style.width = '100%';
            try {{ iframe.contentWindow.dispatchEvent(new Event('resize')); }} catch (error) {{}}
        }}

        function handleFullscreenChange() {{
            syncFullscreenButtonLabel();
            requestAnimationFrame(() => {{
                refreshRunnerSize();
                requestAnimationFrame(refreshRunnerSize);
            }});
        }}

        function runDemo() {{
            setActive('');
            titleEl.innerHTML = '<strong>Running demo</strong>';
            openFileEl.href = '#';
            const iframe = document.createElement('iframe');
            iframe.className = 'run-frame';
            iframe.tabIndex = -1;
            iframe.setAttribute('allow', 'fullscreen; gamepad; clipboard-read; clipboard-write');
            iframe.addEventListener('load', () => {{
                try {{
                    iframe.contentWindow.focus();
                    iframe.contentDocument.getElementById('canvas').focus({{ preventScroll: true }});
                }} catch (error) {{}}
            }});
            iframe.srcdoc = runnerDocument();
            bodyEl.replaceChildren(iframe, fullscreenButton());
            syncFullscreenButtonLabel();
            requestAnimationFrame(() => {{
                iframe.focus({{ preventScroll: true }});
                try {{ iframe.contentWindow.focus(); }} catch (error) {{}}
            }});
        }}

        listEl.addEventListener('click', (event) => {{
            const button = event.target.closest('.file-entry');
            if (!button) return;
            showFile(button.dataset.path).catch((error) => {{
                bodyEl.innerHTML = '<p class="empty-state">' + escapeHtml(String(error)) + '</p>';
            }});
        }});
        runButton.addEventListener('click', runDemo);
        document.addEventListener('fullscreenchange', handleFullscreenChange);
        document.addEventListener('webkitfullscreenchange', handleFullscreenChange);
        if (INITIAL_FILE) showFile(INITIAL_FILE).catch(() => {{}});
    </script>
</body>

</html>
'''


def generate_demo_pages() -> int:
    count = 0
    for demo_dir in demo_dirs():
        compile_wat_for_browser(demo_dir)
        create_demo_zip(demo_dir)
        page = render_demo_page(demo_dir)
        (demo_dir / GENERATED_PAGE).write_text(page, encoding="utf-8")
        count += 1
    return count


def main() -> int:
    try:
        update_homepage()
        page_count = generate_demo_pages()
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print(f"Updated demos section in {INDEX_HTML.relative_to(ROOT)}")
    print(f"Generated {page_count} demo pages in {DEMOS_DIR.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())