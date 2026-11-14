#!/usr/bin/env python3
"""Check that camera metadata mode emits only JSON and exits without a backend."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    binary = str(Path(sys.argv[1] if len(sys.argv) > 1 else "build/vcam").resolve())
    with tempfile.TemporaryDirectory(prefix="vcam-json-") as tmp:
        def run(*args):
            return subprocess.run([binary, *args], cwd=tmp, capture_output=True, text=True, timeout=10)

        ssid = 'test "camera"\\ssid\nline'
        password = 'password\\"value'
        result = run("fuji_x_t5", "wifi", "--dump-camera-json", "--dump", "--ssid", ssid, "--password", password, "--gateway", "192.168.42.1")
        assert result.returncode == 0, result.stderr
        camera = json.loads(result.stdout)
        assert camera["model"] == "X-T5" and camera["backend"] == "wifi"
        assert camera["wifi"] == {"ssid": ssid, "password": password, "gateway": "192.168.42.1"}
        assert not (Path(tmp) / "COMM_DUMP").exists()

        result = run("canon_1300d", "tcp", "--dump-camera-json", "--ssid", "Canon override", "--password", "testpass")
        assert result.returncode == 0, result.stderr
        camera = json.loads(result.stdout)
        assert camera["manufacturer"] == "Canon Inc."
        assert camera["wifi"]["ssid"] == "Canon override" and camera["wifi"]["password"] == "testpass"

        result = run("not_a_camera", "tcp", "--dump-camera-json")
        assert result.returncode != 0 and not result.stdout
        result = run("fuji_x_t5", "invalid", "--dump-camera-json")
        assert result.returncode != 0 and not result.stdout
    print("OK camera JSON output, overrides and errors")


if __name__ == "__main__":
    main()
