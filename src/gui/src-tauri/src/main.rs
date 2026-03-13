// Prevents additional console window on Windows in release, DO NOT REMOVE!!
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

fn main() {
    corridorkey_gui_lib::run()
}

// Force rebuild for icon update: 03/13/2026 01:59:33
