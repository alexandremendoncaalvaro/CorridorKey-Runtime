import "./style.css";
import { BrowserPocController } from "./app/browser_poc_controller";
import { FrameProcessingService } from "./app/frame_processing_service";
import { DomBrowserPocView } from "./ui/dom_browser_poc_view";

const view = new DomBrowserPocView();
const service = new FrameProcessingService();

new BrowserPocController(view, service);
