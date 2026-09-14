#!/usr/bin/env python3
"""App-boot smoke test: launch the real unabara binary headlessly and fail on
QML errors.

Unit tests cover core only — a missing property, a broken component creation
or a bad binding in the QML layer surfaces only at runtime, as stderr lines
referencing a .qml file. This test boots the app offscreen with an isolated,
empty config (fresh-run path: no What's New popup, default template load),
lets it settle for a few seconds, terminates it, and fails if the QML engine
reported problems or the process died on its own.

Usage: boot_smoke.py <path-to-unabara-binary>
Linux-only (config isolation relies on XDG_CONFIG_HOME).
"""

import os
import re
import signal
import subprocess
import sys
import tempfile
import time

SETTLE_SECONDS = 5.0
SHUTDOWN_GRACE_SECONDS = 10.0

# Anything the QML engine complains about carries a "<file>.qml:<line>"
# reference (ReferenceError, TypeError, missing types, unresolved properties,
# binding loops, failed component creation...). The engine's own load failure
# doesn't always name a file, so match it explicitly too.
FAIL_PATTERNS = (
    re.compile(r"\.qml:\d+"),
    re.compile(r"QQmlApplicationEngine failed"),
)


def main():
    if len(sys.argv) != 2:
        sys.exit(f"usage: {sys.argv[0]} <path-to-unabara-binary>")
    binary = sys.argv[1]
    if not os.access(binary, os.X_OK):
        sys.exit(f"ERROR: not an executable: {binary}")

    with tempfile.TemporaryDirectory(prefix="unabara-boot-smoke-") as tmp:
        env = dict(os.environ)
        env["QT_QPA_PLATFORM"] = "offscreen"
        # Fully isolated app state: settings, caches, runtime dir. The app
        # must never read or write the developer's real config from a test.
        for var, sub in (("XDG_CONFIG_HOME", "config"), ("XDG_DATA_HOME", "data"),
                         ("XDG_CACHE_HOME", "cache"), ("XDG_RUNTIME_DIR", "runtime")):
            path = os.path.join(tmp, sub)
            os.makedirs(path, mode=0o700, exist_ok=True)
            env[var] = path
        # Desktop-specific theming (e.g. kvantum) is noise for a smoke test.
        env.pop("QT_STYLE_OVERRIDE", None)

        stderr_path = os.path.join(tmp, "stderr.log")
        with open(stderr_path, "wb") as stderr_file:
            proc = subprocess.Popen([binary], env=env,
                                    stdout=subprocess.DEVNULL, stderr=stderr_file)
            # Let the QML engine load and the UI settle; an early exit is a
            # failed boot no matter what stderr says.
            deadline = time.monotonic() + SETTLE_SECONDS
            early_exit = None
            while time.monotonic() < deadline:
                early_exit = proc.poll()
                if early_exit is not None:
                    break
                time.sleep(0.2)

            if early_exit is None:
                proc.send_signal(signal.SIGTERM)
                try:
                    proc.wait(timeout=SHUTDOWN_GRACE_SECONDS)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()

        with open(stderr_path, "r", errors="replace") as f:
            stderr_lines = f.read().splitlines()

    problems = [line for line in stderr_lines
                if any(p.search(line) for p in FAIL_PATTERNS)]

    ok = True
    if early_exit is not None:
        print(f"FAIL: app exited on its own during startup (code {early_exit})")
        ok = False
    elif proc.returncode not in (0, -signal.SIGTERM):
        print(f"FAIL: app did not shut down cleanly on SIGTERM (code {proc.returncode})")
        ok = False
    if problems:
        print(f"FAIL: {len(problems)} QML error line(s) on stderr:")
        for line in problems:
            print(f"  {line}")
        ok = False

    if not ok:
        print("--- full stderr ---")
        print("\n".join(stderr_lines))
        return 1

    print(f"OK: app booted, ran {SETTLE_SECONDS:.0f}s and terminated with no QML errors "
          f"({len(stderr_lines)} benign stderr line(s)).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
