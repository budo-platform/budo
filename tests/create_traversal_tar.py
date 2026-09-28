#!/usr/bin/env python3
"""Create a minimal gzip tar containing a parent-directory traversal entry."""

import io
import sys
import tarfile

if len(sys.argv) != 2:
    raise SystemExit("usage: create_traversal_tar.py OUTPUT")

payload = b"must not escape\n"
with tarfile.open(sys.argv[1], "w:gz", format=tarfile.USTAR_FORMAT) as archive:
    entry = tarfile.TarInfo("../budo-support-escaped.txt")
    entry.size = len(payload)
    entry.mode = 0o644
    archive.addfile(entry, io.BytesIO(payload))
