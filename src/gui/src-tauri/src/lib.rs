use tauri::{Manager, Emitter, AppHandle};
use tauri_plugin_shell::ShellExt;
use tauri_plugin_shell::process::CommandEvent;

#[tauri::command]
async fn get_engine_status(app: AppHandle) -> Result<String, String> {
    let sidecar_command = app.shell().sidecar("ck-engine")
        .map_err(|e| format!("Sidecar Resolution Error: {}", e))?
        .args(["info", "--json"]);

    let output = sidecar_command.output().await
        .map_err(|e| format!("Sidecar Execution Error: {}", e))?;
    
    if output.status.success() {
        Ok(String::from_utf8_lossy(&output.stdout).to_string())
    } else {
        Err(String::from_utf8_lossy(&output.stderr).to_string())
    }
}

#[tauri::command]
async fn start_processing(app: AppHandle, input: String, output: String, hint: Option<String>) -> Result<(), String> {
    let mut args = vec!["process".to_string(), "--input".to_string(), input, "--output".to_string(), output, "--json".to_string()];
    
    if let Some(h) = hint {
        if !h.is_empty() {
            args.push("--alpha-hint".to_string());
            args.push(h);
        }
    }

    let (mut rx, _child) = app.shell().sidecar("ck-engine")
        .map_err(|e| format!("Sidecar Resolution Error: {}", e))?
        .args(args)
        .spawn().map_err(|e| format!("Sidecar Spawn Error: {}", e))?;

    tauri::async_runtime::spawn(async move {
        while let Some(event) = rx.recv().await {
            if let CommandEvent::Stdout(line) = event {
                let _ = app.emit("engine-event", String::from_utf8_lossy(&line).to_string());
            }
        }
    });

    Ok(())
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_fs::init())
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_shell::init())
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
