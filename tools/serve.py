#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Serves build/site (the companion app: the emulator, the firmware, the sounds) on 127.0.0.1:8792 with caching off, so the
browser never flashes an older package than the one just built.
  tools/serve.py [port]"""
import http.server
import os
import sys
from pathlib import Path


class NoCache(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cache-Control", "no-store, max-age=0")
        super().end_headers()


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8792
    os.chdir(Path(__file__).resolve().parents[1] / "build" / "site")
    http.server.ThreadingHTTPServer(("127.0.0.1", port), NoCache).serve_forever()


if __name__ == "__main__":
    main()
