# CorridorKey Resolve OFX v0.4.4 (Windows)

## Performance Improvements & Bug Fixes

This patch introduces crucial fixes for VRAM allocation and interactive UX:
- **TensorRT 2048 FP16 Fix**: Increased the maximum TensorRT compiler workspace size from 1GB to 2GB to allow 24GB GPUs (like the RTX 4090) to successfully build and run the 2048 model without throwing an invisible OOM subsystem exception.
- **CPU Bottleneck Prevention**: Locked the CPU backend to exclusively run at `Preview (512)` mode regardless of what the user parameter requests. This completely prevents DaVinci Resolve from 10-minute freezes when falling back to the CPU on high-res footage.
- **UI Error Sync Repair**: Re-architected the OFX parameter update pipeline so that fatal runtime backend errors (like lacking an artifact, or insufficient VRAM for a model) are now properly surfaced directly to the DaVinci Resolve info box when the node turns red, instead of silently pretending to succeed and freezing the UI on the prior state.

## Notes
- To test the 2048 "Maximum" mode, ensure you have an RTX 4090 (24GB VRAM) or similar professional card. 10GB/12GB cards (like the RTX 3080/4070) will hit a pure Out of Memory limit due to DaVinci Resolve overhead vs the 2048 Unet activation buffers, as correctly documented internally. Use `High (1024)` or `Ultra (1536)` on >10GB cards.
