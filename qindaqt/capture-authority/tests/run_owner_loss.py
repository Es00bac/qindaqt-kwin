#!/usr/bin/env python3
"""Run only the noninstalled owner-loss fixture on a zero-activation private bus."""
import argparse
import os
from pathlib import Path
import signal
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("test_binary", type=Path)
parser.add_argument("test_functions", nargs="*")
args = parser.parse_args()
if os.geteuid() == 0:
    parser.error("capture authority proof requires an ordinary user")
binary = args.test_binary.resolve(strict=True)
with tempfile.TemporaryDirectory(prefix="qindaqt-capture-owner-loss-") as directory:
    root = Path(directory)
    env = os.environ.copy()
    for key in ("DISPLAY", "WAYLAND_DISPLAY", "DBUS_SESSION_BUS_ADDRESS", "SESSION_MANAGER"):
        env.pop(key, None)
    for key, leaf in {
        "HOME": "home", "XDG_RUNTIME_DIR": "runtime", "XDG_CONFIG_HOME": "config",
        "XDG_CACHE_HOME": "cache", "XDG_DATA_HOME": "data", "XDG_STATE_HOME": "state",
    }.items():
        path = root / leaf
        path.mkdir(mode=0o700)
        env[key] = str(path)
    env.update(QT_QPA_PLATFORM="offscreen", LIBGL_ALWAYS_SOFTWARE="1", KWIN_COMPOSE="O2")
    config = root / "bus.conf"
    # No included session configuration, service directories or activators.
    config.write_text(f'''<busconfig>
<type>session</type><listen>unix:tmpdir={root / 'runtime'}</listen><auth>EXTERNAL</auth>
<policy context="default">
<allow send_destination="*"/><allow receive_sender="*"/><allow own="*"/>
</policy>
</busconfig>''')
    process = subprocess.Popen(
        ["dbus-run-session", f"--config-file={config}", "--", str(binary), *args.test_functions],
        env=env, start_new_session=True,
    )
    try:
        result = process.wait(timeout=90)
    finally:
        # AGENT-GUARD: even failed rows retire only this disposable namespace.
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait()
    raise SystemExit(result)
