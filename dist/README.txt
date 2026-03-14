=====================================================
CorridorKey Runtime - DaVinci Resolve OFX Plugin
Distribution Release - Windows (NVIDIA RTX 30xx/40xx)
=====================================================

The OpenFX Module has been stabilized, tested, and packaged for Windows.
This bundle already includes the integrated AI libraries (TensorRT and ONNX Runtime) to avoid any conflicts with DaVinci Resolve's native engine.

HOW TO INSTALL:
1. Extract and place this folder in a safe location (e.g., Documents).
2. Right-click the "Instalar_Plugin_DaVinci.bat" file (or create your own installer script).
3. Choose "Run as administrator".
4. The script will automatically install the plugin into C:\Program Files\Common Files\OFX and clear DaVinci's cache to ensure it loads properly.

HOW TO USE:
1. Open DaVinci Resolve.
2. Navigate to the Color or Fusion page.
3. Drag and drop the "CorridorKey" effect (from the OpenFX panel) onto a Node.
4. In the Inspector Menu (Right panel), you can control:
   - Despill Strength (Removes green spill)
   - Auto Despeckle (Reduces AI noise)
   - Input is Linear (Color space management)

HOW DOES THE AI WORK IN DAVINCI?
The engine utilizes your NVIDIA RTX graphics card on the very first frame. This means the FIRST time you connect the video, DaVinci will briefly freeze while the TensorRT model compiles into the VRAM. Once compiled, scrubbing through the timeline will become extremely fast (Real-Time whenever possible).

Enjoy!
