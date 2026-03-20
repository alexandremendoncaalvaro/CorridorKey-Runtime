git checkout -b release/v0.4.3
git add -A
git commit -m "feat: release v0.4.3 with multiple outputs and robust installer"
git push -u origin release/v0.4.3
gh pr create --title "Release v0.4.3: Multiple Outputs & Enhancements" --body "Bumps version to 0.4.3, implements multiple outputs, robust Windows UI validation for TensorRT Intel 8, and auto-restarter installers."
gh pr merge --merge --admin --delete-branch
gh release create resolve-v0.4.3 dist\CorridorKey_Resolve_v0.4.3_Windows_Installer.exe dist\CorridorKey_Resolve_v0.4.3_Windows.zip dist\CorridorKey_Resolve_v0.4.3_Windows_DirectML_Installer.exe dist\CorridorKey_Resolve_v0.4.3_Windows_DirectML.zip --title "CorridorKey Resolve OFX v0.4.3 (Windows)" --notes-file dist\release_notes_0.4.3.md
