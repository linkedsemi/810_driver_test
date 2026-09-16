#!/usr/bin/env python3
"""Unified GDB debug interface — auto-fallback backends.

Backends (auto-detected, priority order):
  1. pygdbmi  — interactive GDB MI (pip install pygdbmi)
  2. batch    — gdb -batch -x <script>  (zero deps, always works)

Usage:
    python gdb_proxy.py connect [--host H] [--port P] [--elf E] [--pc 0xADDR]
    python gdb_proxy.py load [--pc 0xADDR]
    python gdb_proxy.py break --addr 0xADDR
    python gdb_proxy.py continue [--timeout SEC]
    python gdb_proxy.py read-mem --addr 0xADDR [--count N] [--size {1,2,4}]
    python gdb_proxy.py read-reg --name REG
    python gdb_proxy.py read-var --name SYMBOL
    python gdb_proxy.py backtrace
    python gdb_proxy.py step [--n N]
    python gdb_proxy.py exec --cmd "raw gdb command"
    python gdb_proxy.py status
    python gdb_proxy.py disconnect
    python gdb_proxy.py batch --script FILE.gdb

All output is JSON on stdout:
  {"ok": true,  "backend": "pygdbmi", "command": "read-mem", "data": {...}}
  {"ok": false, "backend": "batch",   "command": "connect",  "error": "..."}
"""

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile

# ── Constants ──────────────────────────────────────────────────────────────

GDB = "riscv64-unknown-elf-gdb"
GDB_HOST = "127.0.0.1"
GDB_PORT = "1025"
SESSION_FILE = ".gdb_session.json"
DEFAULT_TIMEOUT = 30

# ── Backend detection ─────────────────────────────────────────────────────

try:
    from pygdbmi.gdbcontroller import GdbController
    HAS_PYGDBMI = True
except ImportError:
    HAS_PYGDBMI = False


def detect_backend():
    return "pygdbmi" if HAS_PYGDBMI else "batch"


# ── Session state ─────────────────────────────────────────────────────────

def load_session():
    if os.path.exists(SESSION_FILE):
        with open(SESSION_FILE) as f:
            return json.load(f)
    return {}


def save_session(state):
    with open(SESSION_FILE, "w") as f:
        json.dump(state, f, indent=2)


def clear_session():
    if os.path.exists(SESSION_FILE):
        os.remove(SESSION_FILE)


# ── JSON output helper ─────────────────────────────────────────────────────

def out(ok, backend, command, data=None, error=None):
    result = {"ok": ok, "backend": backend, "command": command}
    if data is not None:
        result["data"] = data
    if error is not None:
        result["error"] = error
    print(json.dumps(result))
    sys.exit(0 if ok else 1)


# ── pygdbmi backend ────────────────────────────────────────────────────────

class PygdbmiBackend:
    """Interactive GDB via pygdbmi (MI protocol)."""

    def __init__(self):
        self.gdb = None

    def connect(self, host, port, elf=None):
        self.gdb = GdbController(command=[GDB, "--interpreter=mi3"])
        resp = self.gdb.write(f"target remote {host}:{port}", timeout_sec=10)
        self._check(resp, "connect")
        if elf:
            resp = self.gdb.write(f"file {elf}", timeout_sec=10)
            self._check(resp, "file")
        return {"host": host, "port": port, "elf": elf}

    def load(self, pc=None):
        resp = self.gdb.write("load", timeout_sec=120)
        self._check(resp, "load")
        if pc:
            resp = self.gdb.write(f"set $pc = {pc}", timeout_sec=5)
            self._check(resp, "set_pc")
        return {}

    def break_(self, addr):
        resp = self.gdb.write(f"break *{addr}", timeout_sec=5)
        self._check(resp, "break")
        return {"addr": addr}

    def continue_(self, timeout=DEFAULT_TIMEOUT):
        resp = self.gdb.write("continue", timeout_sec=timeout, raise_error_on_timeout=False)
        return self._parse_stop(resp)

    def read_mem(self, addr, count=4, size=4):
        fmt = {1: "xb", 2: "xh", 4: "xw"}[size]
        resp = self.gdb.write(f"x/{count}{fmt} {addr}", timeout_sec=5)
        self._check(resp, "read-mem")
        # Parse MI console output: 0xADDR: 0xVAL 0xVAL ...
        values = []
        for r in resp:
            if r.get("type") == "console":
                for m in re.finditer(r"0x([0-9a-fA-F]+)", r.get("payload", "")):
                    values.append("0x" + m.group(1))
        return {"addr": addr, "values": values}

    def read_reg(self, name):
        resp = self.gdb.write(f"print ${name}", timeout_sec=5)
        self._check(resp, "read-reg")
        for r in resp:
            if r.get("type") == "console":
                m = re.search(r"=\s*(0x[0-9a-fA-F]+|\d+)", r.get("payload", ""))
                if m:
                    return {"name": name, "value": m.group(1)}
        return {"name": name, "value": None}

    def read_var(self, name):
        resp = self.gdb.write(f"print {name}", timeout_sec=5)
        self._check(resp, "read-var")
        for r in resp:
            if r.get("type") == "console":
                return {"name": name, "output": r.get("payload", "").strip()}
        return {"name": name, "output": None}

    def backtrace(self):
        resp = self.gdb.write("backtrace", timeout_sec=5)
        self._check(resp, "backtrace")
        lines = [r.get("payload", "") for r in resp if r.get("type") == "console"]
        return {"frames": lines}

    def step(self, n=1):
        for _ in range(n):
            resp = self.gdb.write("step", timeout_sec=5)
            self._check(resp, "step")
        return {"steps": n}

    def exec_cmd(self, cmd):
        resp = self.gdb.write(cmd, timeout_sec=DEFAULT_TIMEOUT,
                              raise_error_on_timeout=False)
        lines = [r.get("payload", "") for r in resp
                 if r.get("type") in ("console", "log", "output")]
        return {"output": "\n".join(lines)}

    def status(self):
        if self.gdb is None:
            return {"connected": False}
        resp = self.gdb.write("info target", timeout_sec=5,
                              raise_error_on_timeout=False)
        lines = [r.get("payload", "") for r in resp if r.get("type") == "console"]
        return {"connected": True, "target": lines}

    def disconnect(self):
        if self.gdb:
            self.gdb.write("detach", timeout_sec=5, raise_error_on_timeout=False)
            self.gdb.exit()
            self.gdb = None
        return {}

    # ── internal ──

    def _check(self, resp, cmd_name):
        for r in resp:
            if r.get("type") == "result" and r.get("message") == "error":
                raise RuntimeError(f"{cmd_name}: {r.get('payload', 'unknown error')}")

    def _parse_stop(self, resp):
        for r in resp:
            if r.get("type") == "notify" and r.get("message") == "stopped":
                payload = r.get("payload", {})
                reason = payload.get("reason", "unknown")
                frame = payload.get("frame", {})
                return {
                    "stopped": True,
                    "reason": reason,
                    "addr": frame.get("addr"),
                    "func": frame.get("func"),
                    "file": frame.get("file"),
                    "line": frame.get("line"),
                }
        return {"stopped": False, "timeout": True}


# ── batch backend ──────────────────────────────────────────────────────────

class BatchBackend:
    """One-shot GDB batch script execution."""

    def __init__(self):
        self.session = load_session()

    def _run_script(self, script_lines, timeout=120):
        with tempfile.NamedTemporaryFile(
            mode="w", suffix=".gdb", delete=False
        ) as f:
            f.write("\n".join(script_lines))
            f.write("\n")
            script_path = f.name
        try:
            result = subprocess.run(
                [GDB, "-batch", "-x", script_path],
                capture_output=True, text=True, timeout=timeout,
            )
            return result.stdout + result.stderr
        except subprocess.TimeoutExpired:
            raise RuntimeError(f"batch timeout after {timeout}s")
        finally:
            os.unlink(script_path)

    def _init_lines(self):
        lines = [
            "set confirm off",
            "set pagination off",
            f"target remote {self.session.get('host', GDB_HOST)}:{self.session.get('port', GDB_PORT)}",
        ]
        elf = self.session.get("elf")
        if elf:
            lines.append(f"file {elf}")
        return lines

    def connect(self, host, port, elf=None):
        self.session["host"] = host
        self.session["port"] = port
        self.session["elf"] = elf
        save_session(self.session)
        lines = self._init_lines() + ["echo CONNECT_OK\\n", "detach"]
        output = self._run_script(lines, timeout=15)
        if "CONNECT_OK" not in output and "Remote target" not in output and "detached" not in output.lower():
            raise RuntimeError(f"connect failed: {output[-500:]}")
        return {"host": host, "port": port, "elf": elf}

    def load(self, pc=None):
        lines = self._init_lines() + ["reset", "load"]
        if pc:
            lines.append(f"set $pc = {pc}")
        lines += ["echo LOAD_OK\\n", "detach"]
        output = self._run_script(lines)
        if "LOAD_OK" not in output:
            raise RuntimeError(f"load failed: {output[-500:]}")
        return {}

    def break_(self, addr):
        self.session.setdefault("breakpoints", []).append(addr)
        save_session(self.session)
        return {"addr": addr, "note": "breakpoint saved; effective on next batch run"}

    def continue_(self, timeout=DEFAULT_TIMEOUT):
        bps = self.session.get("breakpoints", [])
        lines = self._init_lines()
        lines.append("reset")
        lines.append("load")
        pc = self.session.get("pc")
        if pc:
            lines.append(f"set $pc = {pc}")
        for bp in bps:
            lines.append(f"break *{bp}")
        lines.append("continue")
        lines.append("detach")
        output = self._run_script(lines, timeout=timeout + 30)
        # Parse breakpoint hit
        m = re.search(r"Breakpoint \d+, (0x[0-9a-f]+) in (\S+)", output)
        if m:
            return {"stopped": True, "reason": "breakpoint",
                    "addr": m.group(1), "func": m.group(2)}
        return {"stopped": False, "timeout": True, "output_tail": output[-500:]}

    def read_mem(self, addr, count=4, size=4):
        fmt = {1: "xb", 2: "xh", 4: "xw"}[size]
        lines = self._init_lines() + [
            f"x/{count}{fmt} {addr}",
            "detach",
        ]
        output = self._run_script(lines, timeout=15)
        values = re.findall(r"0x([0-9a-fA-F]+)", output)
        return {"addr": addr, "values": ["0x" + v for v in values]}

    def read_reg(self, name):
        lines = self._init_lines() + [f"print ${name}", "detach"]
        output = self._run_script(lines, timeout=15)
        m = re.search(r"=\s*(0x[0-9a-fA-F]+|\d+)", output)
        return {"name": name, "value": m.group(1) if m else None}

    def read_var(self, name):
        lines = self._init_lines() + [f"print {name}", "detach"]
        output = self._run_script(lines, timeout=15)
        m = re.search(r"\$\d+\s*=\s*(.+)", output)
        return {"name": name, "output": m.group(1).strip() if m else None}

    def backtrace(self):
        lines = self._init_lines() + ["backtrace", "detach"]
        output = self._run_script(lines, timeout=15)
        frames = [l.strip() for l in output.splitlines() if l.strip().startswith("#")]
        return {"frames": frames}

    def step(self, n=1):
        lines = self._init_lines()
        for _ in range(n):
            lines.append("step")
        lines.append("detach")
        self._run_script(lines, timeout=15)
        return {"steps": n}

    def exec_cmd(self, cmd):
        lines = self._init_lines() + [cmd, "detach"]
        output = self._run_script(lines)
        return {"output": output}

    def status(self):
        return {"connected": bool(self.session.get("host")),
                "session": self.session}

    def disconnect(self):
        lines = self._init_lines() + ["detach"]
        try:
            self._run_script(lines, timeout=10)
        except Exception:
            pass
        clear_session()
        return {}


# ── CLI dispatch ───────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="Unified GDB debug interface")
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("connect")
    p.add_argument("--host", default=GDB_HOST)
    p.add_argument("--port", default=GDB_PORT)
    p.add_argument("--elf")
    p.add_argument("--pc")

    p = sub.add_parser("load")
    p.add_argument("--pc")

    p = sub.add_parser("break")
    p.add_argument("--addr", required=True)

    p = sub.add_parser("continue")
    p.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT)

    p = sub.add_parser("read-mem")
    p.add_argument("--addr", required=True)
    p.add_argument("--count", type=int, default=4)
    p.add_argument("--size", type=int, default=4, choices=[1, 2, 4])

    p = sub.add_parser("read-reg")
    p.add_argument("--name", required=True)

    p = sub.add_parser("read-var")
    p.add_argument("--name", required=True)

    sub.add_parser("backtrace")

    p = sub.add_parser("step")
    p.add_argument("--n", type=int, default=1)

    p = sub.add_parser("exec")
    p.add_argument("--cmd", required=True)

    sub.add_parser("status")
    sub.add_parser("disconnect")

    p = sub.add_parser("batch")
    p.add_argument("--script", required=True)

    args = parser.parse_args()
    backend_name = detect_backend()
    backend = PygdbmiBackend() if backend_name == "pygdbmi" else BatchBackend()

    try:
        if args.command == "connect":
            data = backend.connect(args.host, args.port, args.elf)
            if args.pc and hasattr(backend, 'session'):
                backend.session["pc"] = args.pc
                save_session(backend.session)
            out(True, backend_name, "connect", data)

        elif args.command == "load":
            out(True, backend_name, "load", backend.load(args.pc))

        elif args.command == "break":
            out(True, backend_name, "break", backend.break_(args.addr))

        elif args.command == "continue":
            out(True, backend_name, "continue", backend.continue_(args.timeout))

        elif args.command == "read-mem":
            out(True, backend_name, "read-mem",
                backend.read_mem(args.addr, args.count, args.size))

        elif args.command == "read-reg":
            out(True, backend_name, "read-reg", backend.read_reg(args.name))

        elif args.command == "read-var":
            out(True, backend_name, "read-var", backend.read_var(args.name))

        elif args.command == "backtrace":
            out(True, backend_name, "backtrace", backend.backtrace())

        elif args.command == "step":
            out(True, backend_name, "step", backend.step(args.n))

        elif args.command == "exec":
            out(True, backend_name, "exec", backend.exec_cmd(args.cmd))

        elif args.command == "status":
            out(True, backend_name, "status", backend.status())

        elif args.command == "disconnect":
            out(True, backend_name, "disconnect", backend.disconnect())

        elif args.command == "batch":
            # Force batch mode regardless of pygdbmi availability
            batch = BatchBackend()
            output = batch._run_script(
                open(args.script).read().splitlines())
            out(True, "batch", "batch", {"output": output})

    except Exception as e:
        out(False, backend_name, args.command, error=str(e))


if __name__ == "__main__":
    main()
