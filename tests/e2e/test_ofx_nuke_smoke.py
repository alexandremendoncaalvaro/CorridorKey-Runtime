"""Headless smoke test for the CorridorKey OFX plugin running inside Foundry Nuke.

Run via:
    Nuke<ver>.exe -t test_ofx_nuke_smoke.py --output <path.exr>

The runner script (scripts/test_nuke_smoke.ps1) wraps this with environment
setup (OFX_PLUGIN_PATH, working directory, Nuke executable discovery, and
output-path bookkeeping) so this file only contains the Nuke-side test logic.

The test exits 0 on success and 1 on any failure. Every failure path prints a
diagnostic line tagged "[smoke]" to stdout so the runner can surface it back.
"""

import argparse
import os
import sys

import nuke


PLUGIN_NODE_CLASS = "OFXcom.corridorkey.resolve_v0_7"


def fail(message: str) -> None:
    print(f"[smoke] FAIL: {message}", flush=True)
    sys.exit(1)


def info(message: str) -> None:
    print(f"[smoke] {message}", flush=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="CorridorKey OFX smoke test inside Nuke.")
    parser.add_argument("--output", required=True, help="Path to the .exr to write.")
    parser.add_argument("--width", type=int, default=512, help="Render width in pixels.")
    parser.add_argument("--height", type=int, default=512, help="Render height in pixels.")
    return parser.parse_args(args=sys.argv[1:])


def assert_plugin_available() -> None:
    # nuke.plugins() returns the loaded native + OFX plugin descriptors. For
    # OFX plugins, Nuke registers them as node classes; the canonical check
    # is whether createNode for the expected class succeeds. Doing a dry probe
    # via nuke.knobDefault() is unreliable across versions, so we rely on the
    # createNode call below to be the load smoke test itself.
    info(f"expected node class: {PLUGIN_NODE_CLASS}")


def build_graph(width: int, height: int):
    constant = nuke.nodes.Constant(channels="rgba", color=[0.0, 1.0, 0.0, 1.0],
                                   format=f"{width} {height} 0 0 {width} {height} 1 smoke_format")
    info(f"created Constant node: {constant.name()}")

    try:
        plugin = nuke.createNode(PLUGIN_NODE_CLASS, inpanel=False)
    except Exception as exc:
        fail(f"createNode('{PLUGIN_NODE_CLASS}') raised: {exc}")
    info(f"created plugin node: {plugin.name()} class={plugin.Class()}")
    plugin.setInput(0, constant)

    return plugin


def render_one_frame(source_node, output_path: str) -> None:
    write = nuke.nodes.Write(file=output_path.replace("\\", "/"),
                             file_type="exr",
                             channels="rgba")
    write.setInput(0, source_node)
    info(f"created Write node -> {output_path}")

    try:
        nuke.execute(write, 1, 1, 1)
    except RuntimeError as exc:
        fail(f"nuke.execute raised: {exc}")
    info("nuke.execute completed without raising")


def assert_output_written(output_path: str) -> None:
    if not os.path.isfile(output_path):
        fail(f"output not created: {output_path}")
    size_bytes = os.path.getsize(output_path)
    if size_bytes <= 0:
        fail(f"output is empty: {output_path}")
    info(f"output OK: {output_path} ({size_bytes} bytes)")


def main() -> int:
    args = parse_args()
    info(f"Nuke version: {nuke.NUKE_VERSION_STRING}")
    info(f"OFX_PLUGIN_PATH={os.environ.get('OFX_PLUGIN_PATH', '<unset>')}")

    assert_plugin_available()
    plugin = build_graph(args.width, args.height)
    render_one_frame(plugin, args.output)
    assert_output_written(args.output)

    info("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
