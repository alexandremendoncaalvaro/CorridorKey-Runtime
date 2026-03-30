export type AppErrorCode =
  | "dom_unavailable"
  | "missing_input"
  | "invalid_state"
  | "source_load_failed"
  | "source_permission_denied"
  | "model_load_failed"
  | "inference_failed"
  | "recording_failed";

export interface AppError {
  code: AppErrorCode;
  message: string;
  cause?: unknown;
}

export function app_error(
  code: AppErrorCode,
  message: string,
  cause?: unknown,
): AppError {
  return { code, message, cause };
}
