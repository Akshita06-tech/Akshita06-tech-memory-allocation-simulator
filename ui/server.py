"""Localhost HTTP server bridging the web UI to the compiled C memory simulator."""

from __future__ import annotations

import argparse
import csv
import json
import os
import shutil
import subprocess
import sys
import threading
import urllib.parse
import uuid
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

UI_DIR = Path(__file__).resolve().parent
ROOT_DIR = UI_DIR.parent
TMP_UPLOAD_DIR = UI_DIR / ".tmp_uploads"
BRIDGE_SRC = UI_DIR / "bridge.c"
BRIDGE_BIN = UI_DIR / ("bridge.exe" if os.name == "nt" else "bridge")
CORE_SOURCES = [
    BRIDGE_SRC,
    ROOT_DIR / "process.c",
    ROOT_DIR / "memory.c",
    ROOT_DIR / "policy.c",
    ROOT_DIR / "metrics.c",
    ROOT_DIR / "common.h",
    ROOT_DIR / "memory.h",
    ROOT_DIR / "policy.h",
    ROOT_DIR / "process.h",
]

HOST = "127.0.0.1"
DEFAULT_PORT = 8080
MAX_UPLOAD_BYTES = 260 * 1024 * 1024  # 260 MB cap (supports testing > 256 MB pool overflow)
STATIC_FILES = {
    "/": ("index.html", "text/html; charset=utf-8"),
    "/index.html": ("index.html", "text/html; charset=utf-8"),
    "/styles.css": ("styles.css", "text/css; charset=utf-8"),
    "/app.js": ("app.js", "application/javascript; charset=utf-8"),
    "/charts.js": ("charts.js", "application/javascript; charset=utf-8"),
}


def ensure_bridge_built() -> None:
    """Compile ui/bridge.c against the core C files if missing or outdated."""
    needs_build = not BRIDGE_BIN.exists()
    if not needs_build:
        bin_mtime = BRIDGE_BIN.stat().st_mtime
        needs_build = any(src.exists() and src.stat().st_mtime > bin_mtime for src in CORE_SOURCES)

    if not needs_build:
        return

    gcc = shutil.which("gcc")
    if not gcc:
        if BRIDGE_BIN.exists():
            return
        raise RuntimeError("gcc not found on PATH and ui/bridge binary is missing.")

    cmd = [
        gcc,
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Wpedantic",
        "-O2",
        str(BRIDGE_SRC),
        str(ROOT_DIR / "process.c"),
        str(ROOT_DIR / "memory.c"),
        str(ROOT_DIR / "policy.c"),
        str(ROOT_DIR / "metrics.c"),
        "-o",
        str(BRIDGE_BIN),
    ]
    print("Building C simulator bridge:", " ".join(cmd))
    subprocess.run(cmd, cwd=str(ROOT_DIR), check=True)


class BridgeClient:
    """Thread-safe wrapper around a persistent bridge subprocess."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._proc: subprocess.Popen[str] | None = None

    def _start_locked(self) -> None:
        ensure_bridge_built()
        self._proc = subprocess.Popen(
            [str(BRIDGE_BIN)],
            cwd=str(ROOT_DIR),
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            bufsize=1,
        )

    def command(self, *parts: object) -> dict:
        sanitized = [str(p).replace("\t", " ").replace("\r", "").replace("\n", "") for p in parts]
        line = "\t".join(sanitized) + "\n"
        with self._lock:
            if self._proc is None or self._proc.poll() is not None:
                self._start_locked()
            assert self._proc and self._proc.stdin and self._proc.stdout
            try:
                self._proc.stdin.write(line)
                self._proc.stdin.flush()
                reply = self._proc.stdout.readline()
            except BrokenPipeError:
                self._start_locked()
                assert self._proc and self._proc.stdin and self._proc.stdout
                self._proc.stdin.write(line)
                self._proc.stdin.flush()
                reply = self._proc.stdout.readline()

            if not reply:
                err = self._proc.stderr.read() if self._proc.stderr else ""
                self.close_locked()
                raise RuntimeError(f"Simulator bridge closed unexpectedly. {err.strip()}")
            return json.loads(reply)

    def close_locked(self) -> None:
        if self._proc is not None:
            try:
                if self._proc.poll() is None and self._proc.stdin:
                    self._proc.stdin.write("QUIT\n")
                    self._proc.stdin.flush()
                    self._proc.wait(timeout=2)
            except Exception:
                self._proc.kill()
            finally:
                self._proc = None

    def close(self) -> None:
        with self._lock:
            self.close_locked()


BRIDGE = BridgeClient()


def clean_filename(raw: str) -> str:
    decoded = urllib.parse.unquote(raw or "").strip()
    base = decoded.replace("\\", "/").split("/")[-1]
    return base.replace("\t", " ").replace("\r", "").replace("\n", "")


class SimulatorHandler(BaseHTTPRequestHandler):
    server_version = "MemorySimulatorUI/1.0"

    def log_message(self, format: str, *args: object) -> None:  # noqa: A002
        # Keep terminal output concise.
        sys.stderr.write(f"[{self.address_string()}] {format % args}\n")

    def send_json(self, status: int, payload: dict) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def read_json_body(self) -> dict:
        raw_len = self.headers.get("Content-Length")
        if not raw_len:
            return {}
        length = int(raw_len)
        if length > 1024 * 1024:
            raise ValueError("JSON payload too large.")
        data = self.rfile.read(length) if length > 0 else b"{}"
        return json.loads(data.decode("utf-8"))

    def stage_uploaded_file(self) -> tuple[str, Path]:
        raw_len = self.headers.get("Content-Length")
        if raw_len is None:
            raise ValueError("Content-Length header is required.")
        try:
            content_len = int(raw_len)
        except ValueError as exc:
            raise ValueError("Invalid Content-Length header.") from exc
        if content_len < 0:
            raise ValueError("Invalid negative Content-Length.")
        if content_len > MAX_UPLOAD_BYTES:
            raise OverflowError(
                f"Upload exceeds server maximum of {MAX_UPLOAD_BYTES // (1024 * 1024)} MB."
            )

        filename = clean_filename(self.headers.get("X-File-Name", ""))
        TMP_UPLOAD_DIR.mkdir(parents=True, exist_ok=True)
        tmp_path = TMP_UPLOAD_DIR / f"stage_{uuid.uuid4().hex}.bin"

        remaining = content_len
        written = 0
        chunk_size = 65536
        try:
            with tmp_path.open("wb") as fh:
                while remaining > 0:
                    chunk = self.rfile.read(min(chunk_size, remaining))
                    if not chunk:
                        break
                    written += len(chunk)
                    if written > MAX_UPLOAD_BYTES:
                        raise OverflowError("Upload exceeded maximum allowed size.")
                    fh.write(chunk)
                    remaining -= len(chunk)
        except Exception:
            tmp_path.unlink(missing_ok=True)
            raise

        return filename, tmp_path

    def do_GET(self) -> None:  # noqa: N802
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path in STATIC_FILES:
            rel, content_type = STATIC_FILES[parsed.path]
            target = UI_DIR / rel
            if not target.exists():
                self.send_json(HTTPStatus.NOT_FOUND, {"ok": False, "error": "Asset not found."})
                return
            data = target.read_bytes()
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)
            return

        if parsed.path == "/api/state":
            try:
                res = BRIDGE.command("STATE")
                self.send_json(HTTPStatus.OK, res)
            except Exception as exc:
                self.send_json(HTTPStatus.INTERNAL_SERVER_ERROR, {"ok": False, "error": str(exc)})
            return

        if parsed.path == "/api/csv/timing":
            csv_path = ROOT_DIR / "timing_results.csv"
            if not csv_path.exists():
                self.send_json(HTTPStatus.NOT_FOUND, {"ok": False, "error": "timing_results.csv not found."})
                return
            with csv_path.open("r", newline="", encoding="utf-8") as fh:
                rows = [
                    {
                        "processes": int(r["processes"]),
                        "seed": int(r["seed"]),
                        "strategy": r["strategy"],
                        "repetitions": int(r["repetitions"]),
                        "average_elapsed_seconds": float(r["average_elapsed_seconds"]),
                    }
                    for r in csv.DictReader(fh)
                ]
            self.send_json(HTTPStatus.OK, {"ok": True, "rows": rows})
            return

        if parsed.path == "/api/csv/experiments":
            csv_path = ROOT_DIR / "experiment_results.csv"
            if not csv_path.exists():
                self.send_json(HTTPStatus.NOT_FOUND, {"ok": False, "error": "experiment_results.csv not found."})
                return
            with csv_path.open("r", newline="", encoding="utf-8") as fh:
                rows = [
                    {
                        "mode": r["mode"],
                        "pattern": r["pattern"],
                        "pool_kb": int(r["pool_kb"]),
                        "processes": int(r["processes"]),
                        "seed": int(r["seed"]),
                        "strategy": r["strategy"],
                        "accepted": int(r["accepted"]),
                        "first_try_success": int(r["first_try_success"]),
                        "fragmentation_failures": int(r["fragmentation_failures"]),
                        "oom_failures": int(r["oom_failures"]),
                        "waiting_processes": int(r["waiting_processes"]),
                        "success_rate_pct": float(r["success_rate_pct"]),
                        "avg_wait_ticks": float(r["avg_wait_ticks"]),
                        "makespan_ticks": int(r["makespan_ticks"]),
                        "avg_fragmentation_pct": float(r["avg_fragmentation_pct"]),
                        "avg_search_length": float(r["avg_search_length"]),
                        "compactions": int(r["compactions"]),
                        "units_moved": int(r["units_moved"]),
                        "rank_sum": int(r["rank_sum"]),
                    }
                    for r in csv.DictReader(fh)
                ]
            self.send_json(HTTPStatus.OK, {"ok": True, "rows": rows})
            return

        self.send_json(HTTPStatus.NOT_FOUND, {"ok": False, "error": "Endpoint not found."})

    def do_POST(self) -> None:  # noqa: N802
        parsed = urllib.parse.urlparse(self.path)

        if parsed.path in ("/api/precheck", "/api/alloc"):
            strategy = self.headers.get("X-Strategy", "first").strip()
            tmp_path: Path | None = None
            try:
                filename, tmp_path = self.stage_uploaded_file()
                cmd = "PRECHECK" if parsed.path == "/api/precheck" else "ALLOC"
                res = BRIDGE.command(cmd, strategy, filename, str(tmp_path))
                self.send_json(HTTPStatus.OK, res)
            except OverflowError as exc:
                self.send_json(HTTPStatus.REQUEST_ENTITY_TOO_LARGE, {"ok": False, "error": str(exc)})
            except ValueError as exc:
                self.send_json(HTTPStatus.BAD_REQUEST, {"ok": False, "error": str(exc)})
            except Exception as exc:
                self.send_json(HTTPStatus.INTERNAL_SERVER_ERROR, {"ok": False, "error": str(exc)})
            finally:
                if tmp_path is not None:
                    tmp_path.unlink(missing_ok=True)
            return

        try:
            body = self.read_json_body()
        except Exception as exc:
            self.send_json(HTTPStatus.BAD_REQUEST, {"ok": False, "error": f"Invalid JSON: {exc}"})
            return

        try:
            if parsed.path == "/api/init":
                pool_kb = int(body.get("pool_kb", 65536))
                res = BRIDGE.command("INIT", pool_kb)
                self.send_json(HTTPStatus.OK, res)
            elif parsed.path == "/api/free":
                owner = str(body.get("owner", ""))
                res = BRIDGE.command("FREE", owner)
                self.send_json(HTTPStatus.OK, res)
            elif parsed.path == "/api/compact":
                res = BRIDGE.command("COMPACT")
                self.send_json(HTTPStatus.OK, res)
            elif parsed.path == "/api/sim/init":
                res = BRIDGE.command(
                    "SIM_INIT",
                    str(body.get("mode", "file")),
                    str(body.get("pattern", "balanced")),
                    int(body.get("pool_kb", 4096)),
                    int(body.get("seed", 42)),
                    int(body.get("count", 40)),
                    str(body.get("strategy", "first")),
                )
                self.send_json(HTTPStatus.OK, res)
            elif parsed.path == "/api/sim/step":
                steps = int(body.get("steps", 1))
                res = BRIDGE.command("SIM_STEP", steps)
                self.send_json(HTTPStatus.OK, res)
            elif parsed.path == "/api/sim/compare":
                res = BRIDGE.command(
                    "SIM_COMPARE",
                    str(body.get("mode", "file")),
                    str(body.get("pattern", "balanced")),
                    int(body.get("pool_kb", 4096)),
                    int(body.get("seed", 42)),
                    int(body.get("count", 40)),
                )
                self.send_json(HTTPStatus.OK, res)
            else:
                self.send_json(HTTPStatus.NOT_FOUND, {"ok": False, "error": "Endpoint not found."})
        except Exception as exc:
            self.send_json(HTTPStatus.INTERNAL_SERVER_ERROR, {"ok": False, "error": str(exc)})


def main() -> None:
    parser = argparse.ArgumentParser(description="Run the Memory Allocation Simulator web UI.")
    parser.add_argument("--port", type=int, default=int(os.environ.get("PORT", DEFAULT_PORT)))
    args = parser.parse_args()

    ensure_bridge_built()
    # Warm up the bridge on startup to fail fast if compilation/init breaks.
    BRIDGE.command("STATE")

    server = ThreadingHTTPServer((HOST, args.port), SimulatorHandler)
    print(f"Memory Allocation Simulator UI running at http://{HOST}:{args.port}")
    print("Bound to localhost only (127.0.0.1). Press Ctrl+C to stop.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down...")
    finally:
        server.server_close()
        BRIDGE.close()
        if TMP_UPLOAD_DIR.exists():
            shutil.rmtree(TMP_UPLOAD_DIR, ignore_errors=True)


if __name__ == "__main__":
    main()
