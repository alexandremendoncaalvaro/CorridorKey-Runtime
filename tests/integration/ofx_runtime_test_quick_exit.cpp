// Tiny test helper that exits immediately after launch.
//
// The early-child-death regression test for issue #56 spawns this binary via
// OfxRuntimeClient::launch_server() to verify that the client detects the
// dead PID through is_process_alive() and surfaces an actionable error
// instead of polling Health for the full launch_timeout_ms. Real sidecar
// startup paths use windows-subsystem entry points; this helper uses the
// console subsystem because the regression is platform-portable and the
// test only cares that CreateProcessW / posix_spawn observe a successful
// spawn followed by an immediate exit.
int main() {
    return 0;
}
