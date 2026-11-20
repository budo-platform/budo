"""Exercise a self-packaged executable independently of its source directory."""

import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    budo = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="budo package test ") as temp:
        root = Path(temp)
        env = dict(os.environ, **{
            "HOME": str(root),
            "LOCALAPPDATA": str(root),
            "XDG_DATA_HOME": str(root),
            "SDL_AUDIODRIVER": "dummy",
            "SDL_VIDEODRIVER": "dummy",
        })
        data_root = root / "Library/Application Support" if sys.platform == "darwin" else root

        def run(*args, expected=0):
            result = subprocess.run(
                args, env=env, cwd=root, capture_output=True, text=True, timeout=30
            )
            if expected is not None:
                assert result.returncode == expected, (args, result)
            return result

        assert "Usage:" in run(budo).stderr
        project = root / "Example"
        project.mkdir()
        assets = project / "assets"
        assets.mkdir()
        (assets / "empty").mkdir()
        message = assets / "read me.txt"
        script = project / "main.js"
        suffix = ".exe" if os.name == "nt" else ""
        first = root / f"first build{suffix}"
        second = root / f"second build{suffix}"
        script.write_text(
            """
const asset = sys.assets.readText('assets/read me.txt');
if (asset === 'first') {
    if (typeof sys.files.writeText('files/save.txt', 'kept') !== 'string') sys.exit(12);
} else if (asset !== 'second' || sys.files.readText('files/save.txt') !== 'kept') {
    sys.exit(13);
}
const db = sys.db.open('state');
if (db < 0) sys.exit(14);
if (asset === 'first') {
    sys.db.execute(db, 'CREATE TABLE IF NOT EXISTS state (id INTEGER PRIMARY KEY)');
    sys.db.execute(db, 'INSERT OR IGNORE INTO state VALUES (1)');
} else if (sys.db.query(db, 'SELECT id FROM state')[0].id !== 1) {
    sys.exit(15);
}
sys.db.close(db);
sys.exit(0);
""".lstrip(),
            encoding="utf-8",
        )
        message.write_text("first", encoding="utf-8")
        run(budo, "package", project, "self", "-o", first)
        assert run(first, "--version").stdout == run(budo, "--version").stdout
        assert "Usage:" in run(first, "--help").stderr
        app = data_root / "budo/desktop-apps/example"
        assert not app.exists()
        run(budo, "package", project, "self", "-o", first, expected=1)
        if os.name != "nt":
            symlink = project / "external"
            symlink.symlink_to(budo)
            assert "unsupported file" in run(budo, "package", project, "self", "-o", second, expected=1).stderr
            symlink.unlink()
            assert not second.exists()

        run(first)
        save = app / "data/save.txt"
        assert save.read_text(encoding="utf-8") == "kept"
        versions = app / "versions"
        (version_one,) = versions.iterdir()
        assert (version_one / "example/assets/empty").is_dir()
        assert (version_one / "example/assets/read me.txt").read_text(encoding="utf-8") == "first"
        run(first)
        assert list(versions.iterdir()) == [version_one]

        message.write_text("second", encoding="utf-8")
        run(first, "package", project, "self", "--name", "example", "-o", second)
        assert abs(second.stat().st_size - first.stat().st_size) < 128
        project.rename(root / "source removed")
        run(second)
        assert len(list(versions.iterdir())) == 2
        assert save.read_text(encoding="utf-8") == "kept"

        # A damaged payload must not run even when its cache already exists.
        with second.open("r+b") as output:
            output.seek(budo.stat().st_size)
            byte = output.read(1)
            output.seek(-1, os.SEEK_CUR)
            output.write(bytes([byte[0] ^ 1]))
        result = run(second, expected=1)
        assert "could not read or extract the embedded Budo application" in result.stderr, result
        run(second, "--version")

        if os.name != "nt":
            # Exercise the release-download path locally without a published
            # release or network access. The fixture is a real Budo runtime.
            project = root / "source removed"
            asset = "budo-macos-aarch64" if sys.platform == "darwin" else "budo-linux-x86_64"
            platform = "macos" if sys.platform == "darwin" else "linux"
            fixture = root / "release"
            fixture.mkdir()
            (fixture / asset).symlink_to(budo)
            checksum = fixture / "SHA256SUMS.txt"
            checksum.write_text(hashlib.sha256(budo.read_bytes()).hexdigest() + f"  {asset}\n")
            curl = fixture / "curl"
            curl.write_text("""#!/bin/sh
set -eu
while [ "$1" != "--output" ]; do shift; done
case "$3" in
    */releases/download/vfixture/*) ;;
    *) exit 1 ;;
esac
cp "$BUDO_TEST_RELEASE/${3##*/}" "$2"
""")
            curl.chmod(0o755)
            env["BUDO_TEST_RELEASE"] = str(fixture)
            env["PATH"] = str(fixture) + os.pathsep + env["PATH"]
            downloaded = root / "from-release"
            args = (budo, "package", project, platform, "--release", "vfixture", "--name", "example")
            run(*args, "-o", downloaded)
            run(downloaded)
            checksum.write_text("0" * 64 + f"  {asset}\n")
            failure = run(*args, "-o", root / "wrong-hash", expected=1)
            assert "could not download or verify" in failure.stderr, failure
            assert not (root / "wrong-hash").exists()


if __name__ == "__main__":
    main()
