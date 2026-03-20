use tauri::{Manager, Emitter, AppHandle};
use std::process::{Command, Stdio};
use std::env;
use std::io::{BufRead, BufReader};
use std::thread;
use std::path::PathBuf;

#[cfg(target_os = "windows")]
use std::os::windows::process::CommandExt;

const CREATE_NO_WINDOW: u32 = 0x08000000;

#[cfg(target_os = "windows")]
fn engine_binary_name() -> &'static str {
    "ck-engine.exe"
}

#[cfg(not(target_os = "windows"))]
fn engine_binary_name() -> &'static str {
    "corridorkey"
}

fn push_runtime_root(runtime_roots: &mut Vec<PathBuf>, candidate: PathBuf) {
    if !runtime_roots.iter().any(|root| root == &candidate) {
        runtime_roots.push(candidate);
    }
}

fn candidate_runtime_roots(exe_dir: &std::path::Path, resource_dir: Option<&std::path::Path>) -> Vec<PathBuf> {
    let mut runtime_roots: Vec<PathBuf> = Vec::new();

    push_runtime_root(&mut runtime_roots, exe_dir.to_path_buf());
    push_runtime_root(&mut runtime_roots, exe_dir.join("runtime"));
    push_runtime_root(&mut runtime_roots, exe_dir.join("resources"));
    push_runtime_root(&mut runtime_roots, exe_dir.join("resources").join("runtime"));

    if let Some(resource_dir) = resource_dir {
        push_runtime_root(&mut runtime_roots, resource_dir.to_path_buf());
        push_runtime_root(&mut runtime_roots, resource_dir.join("runtime"));
        push_runtime_root(&mut runtime_roots, resource_dir.join("resources"));
        push_runtime_root(&mut runtime_roots, resource_dir.join("resources").join("runtime"));
    }

    runtime_roots
}

fn get_engine_path(app: Option<&AppHandle>) -> Result<std::path::PathBuf, String> {
    let mut exe_dir = env::current_exe()
        .map_err(|e| format!("Failed to get current exe path: {}", e))?;
    exe_dir.pop();

    let resource_dir = app
        .and_then(|app_handle| app_handle.path().resource_dir().ok());
    let runtime_roots = candidate_runtime_roots(&exe_dir, resource_dir.as_deref());

    let binary_name = engine_binary_name();
    for runtime_root in &runtime_roots {
        let candidate = runtime_root.join(binary_name);
        if candidate.exists() {
            return Ok(candidate);
        }
    }

    let searched_roots = runtime_roots
        .iter()
        .map(|root| root.display().to_string())
        .collect::<Vec<_>>()
        .join("; ");

    Err(format!(
        "Engine binary not found. Looked for {} under: {}",
        binary_name, searched_roots
    ))
}

#[cfg(test)]
mod tests {
    use super::candidate_runtime_roots;
    use std::path::PathBuf;

    #[test]
    fn candidate_runtime_roots_covers_installed_windows_resources_layout() {
        let exe_dir = PathBuf::from("CorridorKey");
        let resource_dir = exe_dir.join("resources");

        let roots = candidate_runtime_roots(&exe_dir, Some(resource_dir.as_path()));

        assert!(roots.contains(&exe_dir.join("resources").join("runtime")));
    }

    #[test]
    fn candidate_runtime_roots_deduplicates_resource_entries() {
        let exe_dir = PathBuf::from("CorridorKey");
        let resource_dir = exe_dir.join("resources");

        let roots = candidate_runtime_roots(&exe_dir, Some(resource_dir.as_path()));
        let runtime_entry = exe_dir.join("resources").join("runtime");
        let count = roots.iter().filter(|path| **path == runtime_entry).count();

        assert_eq!(count, 1);
    }
}

#[tauri::command]
async fn get_engine_status(app: AppHandle) -> Result<String, String> {
    let engine_path = get_engine_path(Some(&app))?;

    let mut command = Command::new(&engine_path);
    command.args(["info", "--json"])
           .current_dir(engine_path.parent().unwrap());

    #[cfg(target_os = "windows")]
    command.creation_flags(CREATE_NO_WINDOW);

    let output = command.output()
        .map_err(|e| format!("Process Spawn Error: Could not start {}. Details: {}", engine_path.display(), e))?;

    if output.status.success() {
        Ok(String::from_utf8_lossy(&output.stdout).to_string())
    } else {
        Err(String::from_utf8_lossy(&output.stderr).to_string())
    }
}

#[tauri::command]
async fn start_processing(app: AppHandle, input: String, output: String, hint: Option<String>, video_encode: Option<String>) -> Result<(), String> {
    let engine_path = get_engine_path(Some(&app))?;
    let current_dir = engine_path.parent().unwrap().to_path_buf();

    let mut args = vec!["process".to_string(), "--input".to_string(), input, "--output".to_string(), output, "--json".to_string()];

    if let Some(h) = hint {
        if !h.is_empty() {
            args.push("--alpha-hint".to_string());
            args.push(h);
        }
    }

    if let Some(mode) = video_encode {
        if !mode.is_empty() {
            args.push("--video-encode".to_string());
            args.push(mode);
        }
    }

    let mut command = Command::new(&engine_path);
    command.args(args)
           .current_dir(current_dir)
           .stdout(Stdio::piped())
           .stderr(Stdio::piped());

    #[cfg(target_os = "windows")]
    command.creation_flags(CREATE_NO_WINDOW);

    let mut child = command.spawn()
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

#[tauri::command]
async fn reveal_in_folder(path: String) -> Result<(), String> {
    let target = PathBuf::from(path);
    if !target.exists() {
        return Err("Output path does not exist.".to_string());
    }

    #[cfg(target_os = "windows")]
    {
        let mut command = Command::new("explorer.exe");
        if target.is_file() {
            let arg = format!("/select,{}", target.display());
            command.arg(arg);
        } else {
            command.arg(target.as_os_str());
        }
        command.creation_flags(CREATE_NO_WINDOW);
        command.spawn().map_err(|e| format!("Failed to open Explorer: {}", e))?;
        return Ok(());
    }

    #[cfg(target_os = "macos")]
    {
        let mut command = Command::new("open");
        command.arg("-R").arg(&target);
        command.spawn().map_err(|e| format!("Failed to reveal in Finder: {}", e))?;
        return Ok(());
    }

    #[cfg(target_os = "linux")]
    {
        let folder = if target.is_dir() {
            target
        } else {
            target.parent().unwrap_or(&target).to_path_buf()
        };
        let mut command = Command::new("xdg-open");
        command.arg(folder);
        command.spawn().map_err(|e| format!("Failed to open folder: {}", e))?;
        return Ok(());
    }
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
        .invoke_handler(tauri::generate_handler![
            get_engine_status,
            start_processing,
            reveal_in_folder
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
