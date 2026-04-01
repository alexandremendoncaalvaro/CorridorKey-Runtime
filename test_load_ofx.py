import ctypes
import os
import sys

ofx_path = r"C:\Program Files\Common Files\OFX\Plugins\CorridorKey.ofx.bundle\Contents\Win64\CorridorKey.ofx"

try:
    print(f"Loading {ofx_path}...")
    lib = ctypes.WinDLL(ofx_path)
    print("Success!")
except Exception as e:
    print(f"Failed to load: {e}")
