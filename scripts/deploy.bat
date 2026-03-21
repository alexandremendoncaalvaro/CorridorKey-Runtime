git checkout -b release/v0.4.4
git add -A
git commit -m "feat: release v0.4.4 fixing TensorRT vram crashes and UI error logic"
git push -u origin release/v0.4.4
gh pr create --title "Release v0.4.4: TensorRT Fixes & CPU Hanging" --body "Bumps version to 0.4.4, fixes the 2048 runtime server execution crashing without VRAM workspace room, implements true 512 mode for CPUs, and forces UI parameters to flush to screen upon exception throw."
gh pr merge --merge --admin --delete-branch
gh release create resolve-v0.4.4 dist\CorridorKey_Resolve_v0.4.4_Windows_Installer.exe dist\CorridorKey_Resolve_v0.4.4_Windows.zip dist\CorridorKey_Resolve_v0.4.4_Windows_DirectML_Installer.exe dist\CorridorKey_Resolve_v0.4.4_Windows_DirectML.zip --title "CorridorKey Resolve OFX v0.4.4 (Windows)" --notes-file docs\release_notes_0.4.4.md
