#!/usr/bin/env python3
"""The setup screen's Lua must load, and must define every callback the OS will call.

A Lua error in a ScriptApp widget does not report itself usefully on the calculator -- the student
gets a blank or broken screen, and the only way to find out is to push the document and look. That
is a device round-trip for a typo, and this repo's standing rule is to build the smallest thing that
catches it on the host instead.

WHAT THIS CAN AND CANNOT SEE. It loads installer/gui.lua under a real Lua interpreter with the
Nspire globals stubbed, so it catches syntax errors, bad references at load time, and a missing
callback. It does NOT execute on.paint against a real `gc`, so it cannot catch a drawing call the
device rejects -- that still needs hardware. Stated rather than implied, because a gate whose limits
are unstated gets read as proof of more than it checked.
"""
import pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
GUI = ROOT / "installer/gui.lua"
IPC = ROOT / "installer/ipc.lua"

# The callbacks the TI-Nspire runtime invokes. A screen missing on.paint draws nothing at all.
REQUIRED = ["paint", "timer", "charIn", "create",
            "arrowKey", "enterKey", "mouseDown", "returnKey", "tabKey"]

STUBS = """
platform = { apilevel = nil, window = { invalidate = function() end,
                                        setFocus = function() end,
                                        width = function() return 320 end,
                                        height = function() return 240 end } }
math.eval = function(s) return nil, 930 end
timer = { start = function() end, getMilliSecCounter = function() return 0 end }
clipboard = { addText = function() end, getText = function() return nil end }
on = {}
string.split = function(s, sep) return {s} end
"""


def main():
    try:
        import lupa
    except ImportError:
        print("  CANNOT CHECK: lupa is not installed (pip install lupa). Not a pass.")
        return 2
    if not GUI.exists():
        print("  CANNOT CHECK: installer/gui.lua is missing. Not a pass.")
        return 2

    src = GUI.read_text()
    # ipc.lua is substituted into this widget at build time by template.sed; do the same here so
    # the file under test is the file that ships rather than an approximation of it.
    if "-- ZZZ_IPC_LUA_ZZZ" not in src:
        print("  FAIL: gui.lua no longer carries the -- ZZZ_IPC_LUA_ZZZ marker, so template.sed "
              "will not inject the IPC layer and ipc_send/ipc_subscribe will be nil at runtime.")
        return 1
    src = src.replace("-- ZZZ_IPC_LUA_ZZZ", IPC.read_text())

    L = lupa.LuaRuntime(unpack_returned_tuples=True)
    try:
        L.execute(STUBS)
        L.execute(src)
    except Exception as e:
        print(f"  FAIL: gui.lua does not load\n    {e}")
        return 1
    print("  gui.lua loads under Lua with the Nspire globals stubbed (ipc.lua injected as at build)")

    on = L.globals()["on"]
    missing = [k for k in REQUIRED if on[k] is None]
    for k in REQUIRED:
        print(f"    on.{k:<10} {'defined' if on[k] is not None else 'MISSING'}")
    if missing:
        print(f"\n  FAIL: the runtime calls these and they are not defined: "
              f"{', '.join('on.' + m for m in missing)}")
        return 1

    # on.create wires the IPC subscriptions; if it throws, the screen never learns the result.
    try:
        on["create"]()
    except Exception as e:
        print(f"\n  FAIL: on.create() raised -- the install result would never reach the screen\n    {e}")
        return 1
    print("    on.create() runs without raising")

    # AND on.paint IS ACTUALLY RUN, against a gc that records rather than draws. A misspelled
    # method (drawStrng) or a wrong argument count is a runtime error in Lua, not a load error, so
    # nothing above would have seen it -- and on the device it is a blank screen.
    #
    # Every state is painted, because the failure screens are exactly the ones nobody exercises by
    # hand and exactly the ones a student sees on a bad day.
    # THE STUB REFUSES A METHOD THAT DOES NOT EXIST, which is the entire point of it. The first
    # version returned a function for ANY key, so `gc:drawStrng(...)` ran happily and the control
    # for this gate passed while the defect it exists to catch went through -- a stub that accepts
    # everything measures nothing. The list is the TI-Nspire graphics context API.
    L.execute("""
    _calls = {}
    local API = { setColorRGB=1, setFont=1, drawString=1, getStringWidth=1, getStringHeight=1,
                  fillRect=1, drawRect=1, drawLine=1, fillPolygon=1, drawPolyLine=1, setPen=1,
                  drawArc=1, fillArc=1, clipRect=1, begin=1, finish=1, drawImage=1, setAlpha=1,
                  fillRoundRect=1, drawRoundRect=1 }
    local mt = { __index = function(t, k)
        if not API[k] then error("gc has no method '" .. tostring(k) .. "'", 2) end
        return function(self, ...) _calls[#_calls+1] = k; return 10 end
    end }
    gcstub = setmetatable({}, mt)
    """)
    paint = L.globals()["on"]["paint"]
    STATES = [("ready", None), ("install_start", None), ("install_requested", None),
              ("install_done", None), ("install_failed", "os_invalid"),
              ("install_failed", "no_resources"), ("install_failed", "setup")]
    for st, reason in STATES:
        L.execute(f'status = "{st}"; failed = {("nil" if reason is None else chr(34)+reason+chr(34))}')
        for cx in ("true", "false"):
            L.execute(f"cxii = {cx}")
            try:
                paint(L.globals()["gcstub"])
            except Exception as e:
                print(f"\n  FAIL: on.paint raised in state {st}"
                      f"{'/' + reason if reason else ''} (cxii={cx})\n    {e}")
                return 1
    n = len(list(L.globals()["_calls"]))
    print(f"    on.paint runs in all {len(STATES)} states x 2 device kinds ({n} draw calls made)")
    if n < 50:
        print("  FAIL: suspiciously few draw calls -- the screen is probably drawing nothing.")
        return 1

    print("\n  PASS: loads, every callback defined, on.create clean, on.paint runs in every state. "
          "NOT checked here: whether the real device accepts these draw calls -- that needs hardware.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
