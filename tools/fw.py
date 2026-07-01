#!/usr/bin/env python3
"""fw — FreeWili2 BSP task runner (cross-platform).

Commands:
  fw build [app]     configure+build an app for the RP2350B target (default hello_display)
  fw flash [app]     program the app over the cmsis-dap debug probe via OpenOCD
  fw rtt             stream SEGGER RTT diagnostics
  fw test            build+run host unit tests (CTest, no hardware)
  fw new-app <name>  scaffold apps/<name> from apps/template
Add --print to any build/flash/test command to print the command(s) instead of running.
"""
import argparse, pathlib, shutil, subprocess, sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_APP = "hello_display"

def build_command(app):
    return ["cmake", "--build", "--preset", "target", "--target", app]

def flash_command(app):
    elf = f"build/apps/{app}/{app}.elf"
    cfg = str(REPO_ROOT / "tools" / "openocd" / "freewili2.cfg")
    return ["openocd", "-f", cfg,
            "-c", f"program {elf} verify reset exit"]

def rtt_command():
    cfg = str(REPO_ROOT / "tools" / "openocd" / "freewili2.cfg")
    return ["openocd", "-f", cfg,
            "-c", "rtt setup 0x20000000 0x40000 \"SEGGER RTT\"",
            "-c", "init", "-c", "rtt start", "-c", "rtt server start 9090 0"]

def test_command():
    return [
        ["cmake", "--preset", "host-test"],
        ["cmake", "--build", "--preset", "host-test"],
        ["ctest", "--test-dir", "build-tests", "--output-on-failure"],
    ]

def new_app(name, repo_root=REPO_ROOT):
    src = pathlib.Path(repo_root) / "apps" / "template"
    dest = pathlib.Path(repo_root) / "apps" / name
    if dest.exists():
        raise FileExistsError(dest)
    shutil.copytree(src, dest)
    cml = dest / "CMakeLists.txt"
    cml.write_text(cml.read_text().replace("template", name))
    return dest

def _run(cmds, do_print):
    if isinstance(cmds[0], str):
        cmds = [cmds]
    for c in cmds:
        if do_print:
            print(" ".join(c))
        else:
            subprocess.run(c, cwd=REPO_ROOT, check=True)

def main(argv=None):
    p = argparse.ArgumentParser(prog="fw")
    sub = p.add_subparsers(dest="cmd", required=True)
    for name in ("build", "flash"):
        sp = sub.add_parser(name); sp.add_argument("app", nargs="?", default=DEFAULT_APP)
        sp.add_argument("--print", dest="show", action="store_true")
    sp = sub.add_parser("rtt"); sp.add_argument("--print", dest="show", action="store_true")
    sp = sub.add_parser("test"); sp.add_argument("--print", dest="show", action="store_true")
    sp = sub.add_parser("new-app"); sp.add_argument("name")

    a = p.parse_args(argv)
    if a.cmd == "build":   _run(build_command(a.app), a.show)
    elif a.cmd == "flash": _run(flash_command(a.app), a.show)
    elif a.cmd == "rtt":   _run(rtt_command(), a.show)
    elif a.cmd == "test":  _run(test_command(), a.show)
    elif a.cmd == "new-app":
        print("created", new_app(a.name))
    return 0

if __name__ == "__main__":
    sys.exit(main())
