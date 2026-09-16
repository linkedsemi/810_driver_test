#!/usr/bin/env python3
"""GDB MCP Server — exposes GDB debug operations as MCP tools.

Register in .mcp.json:
  {
    "mcpServers": {
      "gdb": {
        "command": "python3",
        "args": ["810_driver_test/.claude/tools/gdb/gdb_mcp_server.py"]
      }
    }
  }

Requires: pip install pygdbmi mcp
"""

import asyncio
import json
import re
import sys

try:
    from pygdbmi.gdbcontroller import GdbController
except ImportError:
    print("Error: pip install pygdbmi", file=sys.stderr)
    sys.exit(1)

try:
    from mcp.server import Server
    from mcp.server.stdio import stdio_server
    from mcp.types import Tool, TextContent
except ImportError:
    print("Error: pip install mcp", file=sys.stderr)
    sys.exit(1)

GDB = "riscv64-unknown-elf-gdb"
GDB_HOST = "127.0.0.1"
GDB_PORT = "1025"

# ── GDB session state ─────────────────────────────────────────────────────

_gdb = None


def _get_gdb():
    global _gdb
    if _gdb is None:
        _gdb = GdbController(command=[GDB, "--interpreter=mi3"])
    return _gdb


def _check(resp, cmd_name):
    for r in resp:
        if r.get("type") == "result" and r.get("message") == "error":
            raise RuntimeError(f"{cmd_name}: {r.get('payload', 'unknown')}")


def _console_lines(resp):
    return [r.get("payload", "") for r in resp
            if r.get("type") in ("console", "log", "output")]


def _json(data):
    return [TextContent(type="text", text=json.dumps(data, indent=2))]


# ── Tool implementations ───────────────────────────────────────────────────

def gdb_connect(host: str = GDB_HOST, port: str = GDB_PORT,
                elf_path: str = "") -> list:
    gdb = _get_gdb()
    resp = gdb.write(f"target remote {host}:{port}", timeout_sec=10)
    _check(resp, "connect")
    result = {"connected": True, "host": host, "port": port}
    if elf_path:
        resp = gdb.write(f"file {elf_path}", timeout_sec=10)
        _check(resp, "file")
        result["elf"] = elf_path
    return _json(result)


def gdb_disconnect() -> list:
    global _gdb
    if _gdb:
        _gdb.write("detach", timeout_sec=5, raise_error_on_timeout=False)
        _gdb.exit()
        _gdb = None
    return _json({"disconnected": True})


def gdb_load(pc_addr: str = "") -> list:
    gdb = _get_gdb()
    resp = gdb.write("load", timeout_sec=120)
    _check(resp, "load")
    result = {"loaded": True}
    if pc_addr:
        resp = gdb.write(f"set $pc = {pc_addr}", timeout_sec=5)
        _check(resp, "set_pc")
        result["pc"] = pc_addr
    return _json(result)


def gdb_break(addr: str) -> list:
    gdb = _get_gdb()
    resp = gdb.write(f"break *{addr}", timeout_sec=5)
    _check(resp, "break")
    return _json({"breakpoint": addr})


def gdb_continue(timeout_ms: int = 30000) -> list:
    gdb = _get_gdb()
    resp = gdb.write("continue", timeout_sec=timeout_ms / 1000,
                     raise_error_on_timeout=False)
    for r in resp:
        if r.get("type") == "notify" and r.get("message") == "stopped":
            p = r.get("payload", {})
            return _json({
                "stopped": True,
                "reason": p.get("reason"),
                "addr": p.get("frame", {}).get("addr"),
                "func": p.get("frame", {}).get("func"),
                "file": p.get("frame", {}).get("file"),
                "line": p.get("frame", {}).get("line"),
            })
    return _json({"stopped": False, "timeout": True})


def gdb_read_memory(addr: str, count: int = 4, size: int = 4) -> list:
    gdb = _get_gdb()
    fmt = {1: "xb", 2: "xh", 4: "xw"}[size]
    resp = gdb.write(f"x/{count}{fmt} {addr}", timeout_sec=5)
    _check(resp, "read-mem")
    values = []
    for line in _console_lines(resp):
        values.extend(re.findall(r"0x[0-9a-fA-F]+", line))
    return _json({"addr": addr, "values": values})


def gdb_read_register(name: str) -> list:
    gdb = _get_gdb()
    resp = gdb.write(f"print ${name}", timeout_sec=5)
    _check(resp, "read-reg")
    for line in _console_lines(resp):
        m = re.search(r"=\s*(0x[0-9a-fA-F]+|\d+)", line)
        if m:
            return _json({"name": name, "value": m.group(1)})
    return _json({"name": name, "value": None})


def gdb_backtrace() -> list:
    gdb = _get_gdb()
    resp = gdb.write("backtrace", timeout_sec=5)
    _check(resp, "backtrace")
    return _json({"frames": _console_lines(resp)})


def gdb_step(n: int = 1) -> list:
    gdb = _get_gdb()
    for _ in range(n):
        resp = gdb.write("step", timeout_sec=5)
        _check(resp, "step")
    return _json({"steps": n})


def gdb_execute(cmd: str, timeout_ms: int = 30000) -> list:
    gdb = _get_gdb()
    resp = gdb.write(cmd, timeout_sec=timeout_ms / 1000,
                     raise_error_on_timeout=False)
    return _json({"output": "\n".join(_console_lines(resp))})


def gdb_status() -> list:
    global _gdb
    if _gdb is None:
        return _json({"connected": False})
    resp = _gdb.write("info target", timeout_sec=5,
                      raise_error_on_timeout=False)
    return _json({"connected": True,
                  "target": _console_lines(resp)})


# ── MCP Server setup ───────────────────────────────────────────────────────

TOOLS = [
    Tool(name="gdb_connect",
         description="Connect to GDB target (DebugServer).",
         inputSchema={"type": "object", "properties": {
             "host": {"type": "string", "default": GDB_HOST},
             "port": {"type": "string", "default": GDB_PORT},
             "elf_path": {"type": "string", "description": "Path to ELF file"},
         }}),
    Tool(name="gdb_disconnect",
         description="Disconnect from GDB target.",
         inputSchema={"type": "object", "properties": {}}),
    Tool(name="gdb_load",
         description="Load firmware to target and optionally set PC.",
         inputSchema={"type": "object", "properties": {
             "pc_addr": {"type": "string", "description": "Entry address (e.g. 0x10000000)"},
         }}),
    Tool(name="gdb_break",
         description="Set breakpoint at address.",
         inputSchema={"type": "object", "properties": {
             "addr": {"type": "string", "description": "Hex address (e.g. 0x10003ef4)"},
         }, "required": ["addr"]}),
    Tool(name="gdb_continue",
         description="Continue execution until breakpoint or timeout.",
         inputSchema={"type": "object", "properties": {
             "timeout_ms": {"type": "integer", "default": 30000},
         }}),
    Tool(name="gdb_read_memory",
         description="Read memory from target.",
         inputSchema={"type": "object", "properties": {
             "addr": {"type": "string", "description": "Hex address"},
             "count": {"type": "integer", "default": 4},
             "size": {"type": "integer", "enum": [1, 2, 4], "default": 4},
         }, "required": ["addr"]}),
    Tool(name="gdb_read_register",
         description="Read CPU register value.",
         inputSchema={"type": "object", "properties": {
             "name": {"type": "string", "description": "Register name (e.g. pc, sp, a0)"},
         }, "required": ["name"]}),
    Tool(name="gdb_backtrace",
         description="Show call stack.",
         inputSchema={"type": "object", "properties": {}}),
    Tool(name="gdb_step",
         description="Step N instructions.",
         inputSchema={"type": "object", "properties": {
             "n": {"type": "integer", "default": 1},
         }}),
    Tool(name="gdb_execute",
         description="Execute raw GDB command.",
         inputSchema={"type": "object", "properties": {
             "cmd": {"type": "string", "description": "GDB command string"},
             "timeout_ms": {"type": "integer", "default": 30000},
         }, "required": ["cmd"]}),
    Tool(name="gdb_status",
         description="Query GDB connection and target status.",
         inputSchema={"type": "object", "properties": {}}),
]

HANDLERS = {
    "gdb_connect": gdb_connect,
    "gdb_disconnect": gdb_disconnect,
    "gdb_load": gdb_load,
    "gdb_break": gdb_break,
    "gdb_continue": gdb_continue,
    "gdb_read_memory": gdb_read_memory,
    "gdb_read_register": gdb_read_register,
    "gdb_backtrace": gdb_backtrace,
    "gdb_step": gdb_step,
    "gdb_execute": gdb_execute,
    "gdb_status": gdb_status,
}


async def main():
    server = Server("gdb-debug")

    @server.list_tools()
    async def list_tools():
        return TOOLS

    @server.call_tool()
    async def call_tool(name, arguments):
        handler = HANDLERS.get(name)
        if not handler:
            return _json({"error": f"unknown tool: {name}"})
        try:
            return handler(**arguments)
        except Exception as e:
            return _json({"error": str(e)})

    async with stdio_server() as (read_stream, write_stream):
        await server.run(read_stream, write_stream,
                         server.create_initialization_options())


if __name__ == "__main__":
    asyncio.run(main())
