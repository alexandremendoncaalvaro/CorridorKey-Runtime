import type { BrowserModelDefinition } from "../core/image_types";

const DEFAULT_BROWSER_MODEL_BASE_URL =
  "https://pub-d7b1a78244e7440c9b8ca8a98a44e542.r2.dev";

function build_browser_model_url(filename: string): string {
  const base_url =
    import.meta.env.VITE_BROWSER_MODEL_BASE_URL ??
    DEFAULT_BROWSER_MODEL_BASE_URL;
  return `${base_url}/${filename}`;
}

export const DEFAULT_BROWSER_MODEL_ID = "corridorkey_int8_512";

export const BROWSER_MODEL_CATALOG: readonly BrowserModelDefinition[] = [
  {
    id: "corridorkey_int8_512",
    filename: "corridorkey_int8_512.onnx",
    label: "Default browser-safe",
    description:
      "INT8 512. Smallest bundled ONNX variant and the safest default for browser startup.",
    resolution: 512,
    size_label: "76 MB",
    url: build_browser_model_url("corridorkey_int8_512.onnx"),
  },
  {
    id: "corridorkey_int8_768",
    filename: "corridorkey_int8_768.onnx",
    label: "Balanced detail",
    description:
      "INT8 768. Higher detail than 512 while staying in the conservative browser-compatible track.",
    resolution: 768,
    size_label: "84 MB",
    url: build_browser_model_url("corridorkey_int8_768.onnx"),
  },
  {
    id: "corridorkey_int8_1024",
    filename: "corridorkey_int8_1024.onnx",
    label: "Heavy detail",
    description:
      "INT8 1024. Largest fixed browser model in this sandbox. Expect longer load and higher memory pressure.",
    resolution: 1024,
    size_label: "97 MB",
    url: build_browser_model_url("corridorkey_int8_1024.onnx"),
  },
];

export function find_browser_model_definition(
  model_id: string,
): BrowserModelDefinition | null {
  return BROWSER_MODEL_CATALOG.find((model) => model.id === model_id) ?? null;
}
