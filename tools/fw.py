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
import argparse, pathlib, shutil, socket, subprocess, sys, time

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_APP = "hello_display"
OPENOCD_CFG = str(REPO_ROOT / "tools" / "openocd" / "freewili2.cfg")
RTT_PORT = 9090
# RP2350 SRAM is 0x20000000..0x20082000; scan the whole range for the RTT block.
RTT_SETUP = 'rtt setup 0x20000000 0x82000 "SEGGER RTT"'

def build_command(app):
    return ["cmake", "--build", "--preset", "target", "--target", app]

def _openocd():
    """(exe, scripts_dir) for the Pico-SDK OpenOCD. Uses the ~/.pico-sdk install
    (newest version) when present — matching how subghz flashes — otherwise falls
    back to `openocd` on PATH with its built-in scripts (scripts_dir = None)."""
    root = pathlib.Path.home() / ".pico-sdk" / "openocd"
    if root.is_dir():
        exe_name = "openocd.exe" if sys.platform == "win32" else "openocd"
        for ver in sorted(root.iterdir(), reverse=True):
            exe, scripts = ver / exe_name, ver / "scripts"
            if exe.exists():
                return str(exe), (str(scripts) if scripts.is_dir() else None)
    return "openocd", None

def _openocd_base():
    exe, scripts = _openocd()
    cmd = [exe]
    if scripts:
        cmd += ["-s", scripts]
    return cmd + ["-f", OPENOCD_CFG]

def flash_command(app):
    elf = f"build/apps/{app}/{app}.elf"
    return _openocd_base() + ["-c", f"program {elf} verify reset exit"]

def rtt_command():
    return _openocd_base() + [
        "-c", "init", "-c", RTT_SETUP, "-c", "rtt start",
        "-c", f"rtt server start {RTT_PORT} 0"]

def _host_toolchain_args():
    """Extra `cmake` configure args that pin a host C compiler + Ninja for the
    standalone tests/ tree (no Pico SDK, no cross-compiler). Returns [] entries
    that are simply omitted when a tool can't be found, so CMake falls back to
    its own defaults (e.g. system cc/gcc, non-Ninja generator).
    """
    args = []
    if sys.platform == "win32":
        # Mirrors the subghz repo's proven host-test toolchain: MSYS2 MinGW
        # GCC + the Ninja bundled with the Pico SDK VS Code extension.
        gcc = pathlib.Path("C:/msys64/mingw64/bin/gcc.exe")
        if gcc.exists():
            args += [f"-DCMAKE_C_COMPILER={gcc}"]
        ninja_root = pathlib.Path.home() / ".pico-sdk" / "ninja"
        ninja = next(iter(sorted(ninja_root.glob("*/ninja.exe"), reverse=True)), None) \
            if ninja_root.is_dir() else None
        if ninja is not None:
            args += ["-G", "Ninja", f"-DCMAKE_MAKE_PROGRAM={ninja}"]
    else:
        # Non-Windows: trust the default host cc/gcc; use Ninja if it's on
        # PATH, otherwise let CMake pick its default generator (e.g. Make).
        if shutil.which("ninja"):
            args += ["-G", "Ninja"]
    return args

def test_command():
    tests_dir = REPO_ROOT / "tests"
    build_dir = REPO_ROOT / "build-tests"
    configure = ["cmake", "-S", str(tests_dir), "-B", str(build_dir)]
    configure += _host_toolchain_args()
    return [
        configure,
        ["cmake", "--build", str(build_dir)],
        ["ctest", "--test-dir", str(build_dir), "--output-on-failure"],
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

def run_rtt(seconds=0):
    """Start OpenOCD's RTT server (attached, no flash) and stream channel 0 to
    stdout. seconds=0 runs until Ctrl+C; seconds>0 exits after that window
    (for scripted checks). Diagnostics on the FreeWili2 are RTT-only."""
    proc = subprocess.Popen(rtt_command(), cwd=REPO_ROOT,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(2)  # let OpenOCD attach and locate the RTT control block
        if proc.poll() is not None:
            print("openocd exited early — is the debug probe connected?", file=sys.stderr)
            return 1
        try:
            sock = socket.create_connection(("127.0.0.1", RTT_PORT), timeout=5)
        except OSError as e:
            print(f"could not connect to RTT server on {RTT_PORT}: {e}", file=sys.stderr)
            return 1
        sock.settimeout(0.5)
        deadline = time.time() + seconds if seconds > 0 else None
        print(f"--- RTT connected (port {RTT_PORT}); Ctrl+C to stop ---", file=sys.stderr)
        while deadline is None or time.time() < deadline:
            try:
                data = sock.recv(4096)
                if not data:
                    break
                sys.stdout.write(data.decode("ascii", "replace"))
                sys.stdout.flush()
            except socket.timeout:
                pass
    except KeyboardInterrupt:
        pass
    finally:
        try:
            sock.close()
        except NameError:
            pass
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
    return 0

def main(argv=None):
    p = argparse.ArgumentParser(prog="fw")
    sub = p.add_subparsers(dest="cmd", required=True)
    for name in ("build", "flash"):
        sp = sub.add_parser(name); sp.add_argument("app", nargs="?", default=DEFAULT_APP)
        sp.add_argument("--print", dest="show", action="store_true")
    sp = sub.add_parser("rtt")
    sp.add_argument("--print", dest="show", action="store_true")
    sp.add_argument("-s", "--seconds", type=int, default=0,
                    help="capture for N seconds then exit (0 = until Ctrl+C)")
    sp = sub.add_parser("test"); sp.add_argument("--print", dest="show", action="store_true")
    sp = sub.add_parser("new-app"); sp.add_argument("name")

    a = p.parse_args(argv)
    if a.cmd == "build":   _run(build_command(a.app), a.show)
    elif a.cmd == "flash": _run(flash_command(a.app), a.show)
    elif a.cmd == "rtt":
        if a.show:
            _run(rtt_command(), True)
        else:
            return run_rtt(a.seconds)
    elif a.cmd == "test":  _run(test_command(), a.show)
    elif a.cmd == "new-app":
        print("created", new_app(a.name))
    return 0

if __name__ == "__main__":
    sys.exit(main())
