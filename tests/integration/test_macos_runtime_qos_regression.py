#!/usr/bin/env python3
# Regression guard for the v0.7.6-mac.1 "scrubbing absurdly slow" bug.
#
# Background
# ----------
# When DaVinci Resolve calls the OFX kOfxImageEffectActionRender entry point,
# it typically does so on a worker thread running at QOS_CLASS_UTILITY or
# QOS_CLASS_BACKGROUND. Before the fix, posix_spawn of the runtime server
# inherited that low QoS class, which on Apple Silicon caused the server's
# Metal work to be preempted by Resolve's higher-QoS Metal workload. Steady-
# state per-frame latency ballooned from ~1.7s to 65-117s under BACKGROUND
# parent QoS.
#
# The fix has two layers:
#   1. posix_spawnattr_set_qos_class_np(QOS_CLASS_USER_INITIATED) in
#      src/plugins/ofx/ofx_runtime_client.cpp launch_server
#   2. pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED) at the top
#      of the ofx-runtime-server subcommand in src/cli/main.cpp
#
# This test runs the ofx_rpc_benchmark_harness with --parent-qos-class
# background and asserts steady-state latency stays within the budget.
# Without the fix it fails; with the fix it passes.

import json
import os
import shutil
import subprocess
import sys
from pathlib import Path


def locate_model(project_root: Path) -> Path:
    candidates = [
        project_root / "models" / "corridorkey_mlx_bridge_1024.mlxfn",
        project_root / "models" / "mlx" / "corridorkey_mlx_bridge_1024.mlxfn",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return Path()


def main() -> int:
    if sys.platform != "darwin":
        print("skip: macOS-only regression test")
        return 0

    harness = Path(os.environ["HARNESS_BIN"])
    server = Path(os.environ["SERVER_BIN"])
    project_root = Path(os.environ["PROJECT_ROOT"])

    if not harness.exists():
        print(f"skip: harness binary not found at {harness}", file=sys.stderr)
        return 0
    if not server.exists():
        print(f"skip: server binary not found at {server}", file=sys.stderr)
        return 0
    model = locate_model(project_root)
    if not model.exists():
        print("skip: corridorkey_mlx_bridge_1024.mlxfn not found; "
              "run scripts/package_mac.sh or stage a model first", file=sys.stderr)
        return 0

    port = int(os.environ.get("TEST_ENDPOINT_PORT", "46099"))

    cmd = [
        str(harness),
        "--server-binary", str(server),
        "--model", str(model),
        "--device", "mlx",
        "--resolution", "1024",
        "--frame-width", "1920", "--frame-height", "1080",
        "--iterations", "6",
        "--endpoint-port", str(port),
        "--parent-qos-class", "background",
    ]
    print("[test] " + " ".join(cmd))
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
    if result.returncode != 0:
        print(f"harness exited non-zero: {result.returncode}", file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        return 1

    out = result.stdout
    brace = out.find("{")
    if brace < 0:
        print("no JSON in harness stdout:\n" + out, file=sys.stderr)
        return 1
    report = json.loads(out[brace:])

    steady = [t["roundtrip_ms"] for t in report["per_frame_timings"][2:]]
    if not steady:
        print("harness produced no steady-state samples", file=sys.stderr)
        return 1

    steady_max = max(steady)
    steady_avg = sum(steady) / len(steady)

    # Budget derived from the measured healthy-parent baseline (~1.7s) plus
    # generous headroom for CI jitter. Without the fix, steady latency under
    # BACKGROUND parent QoS holds at 30-120s per frame, so 8s is a wide
    # detection margin that will never false-positive on the fixed build.
    budget_max_ms = 8000.0
    budget_avg_ms = 5000.0
    status = 0
    print(f"[test] parent_qos={report['parent_qos_class']} "
          f"steady_max_ms={steady_max:.0f} steady_avg_ms={steady_avg:.0f} "
          f"per_frame={[round(x) for x in steady]}")
    if steady_max > budget_max_ms:
        print(f"FAIL: steady_max {steady_max:.0f} ms exceeds {budget_max_ms:.0f} ms "
              "budget. Parent QoS inheritance regression is back.", file=sys.stderr)
        status = 1
    if steady_avg > budget_avg_ms:
        print(f"FAIL: steady_avg {steady_avg:.0f} ms exceeds {budget_avg_ms:.0f} ms "
              "budget.", file=sys.stderr)
        status = 1
    if status == 0:
        print("PASS: server recovers to healthy latency despite background parent QoS")
    return status


if __name__ == "__main__":
    sys.exit(main())
