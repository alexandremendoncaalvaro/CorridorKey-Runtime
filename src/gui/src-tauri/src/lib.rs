use tauri::{Manager, Emitter, AppHandle};
use std::process::{Command, Stdio};
use std::env;
use std::io::{BufRead, BufReader};
use std::thread;

fn get_engine_path() -> Result<std::path::PathBuf, String> {
    let mut exe_path = env::current_exe().map_err(|e| format!("Failed to get current exe path: {}", e))?;
    exe_path.pop(); // Remove the executable name, leaving the directory
    Ok(exe_path.join("ck-engine.exe"))
}

#[tauri::command]
async fn get_engine_status() -> Result<String, String> {
    let engine_path = get_engine_path()?;

    let output = Command::new(&engine_path)
        .args(["info", "--json"])
        .current_dir(engine_path.parent().unwrap())
        .output()
        .map_err(|e| format!("Process Spawn Error: Could not start {}. Details: {}", engine_path.display(), e))?;
    
    if output.status.success() {
        Ok(String::from_utf8_lossy(&output.stdout).to_string())
    } else {
        Err(String::from_utf8_lossy(&output.stderr).to_string())
    }
}

#[tauri::command]
async fn start_processing(app: AppHandle, input: String, output: String, hint: Option<String>) -> Result<(), String> {
    let engine_path = get_engine_path()?;
    let current_dir = engine_path.parent().unwrap().to_path_buf();

    let mut args = vec!["process".to_string(), "--input".to_string(), input, "--output".to_string(), output, "--json".to_string()];
    
    if let Some(h) = hint {
        if !h.is_empty() {
            args.push("--alpha-hint".to_string());
            args.push(h);
        }
    }

    let mut child = Command::new(&engine_path)
        .args(args)
        .current_dir(current_dir)
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .map_err(|e| format!("Process Spawn Error: {}", e))?;

    let stdout = child.stdout.take().unwrap();
    
    // Spawn a standard thread to read stdout and emit to Tauri frontend
    thread::spawn(move || {
        let reader = BufReader::new(stdout);
        for line in reader.lines() {
            if let Ok(l) = line {
                let _ = app.emit("engine-event", l);
            }
        }
        let _ = child.wait();
    });

    Ok(())
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_fs::init())
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_opener::init())
        .setup(|app| {
            if let Some(window) = app.get_webview_window("main") {
                window.show().unwrap();
                window.set_focus().unwrap();
            }
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![get_engine_status, start_processing])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
