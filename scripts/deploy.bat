git checkout -b release/v0.4.6
git add .
git commit -m "feat: release v0.4.6 Memory Safety and OFX Exceptions"
git push -u origin release/v0.4.6
gh pr create --title "Release v0.4.6: Memory Safety & OFX Exceptions" --body "Bumps to 0.4.6. Implements zero-allocation post-processing and try/catch boundaries around OFX native calls."

gh release create resolve-v0.4.6 dist\CorridorKey_Resolve_v0.4.6_Windows_RTX_Installer.exe dist\CorridorKey_Resolve_v0.4.6_Windows_RTX.zip dist\CorridorKey_Resolve_v0.4.6_Windows_DirectML_Installer.exe dist\CorridorKey_Resolve_v0.4.6_Windows_DirectML.zip --title "CorridorKey Resolve OFX v0.4.6 (Windows)" --notes-file docs\release_notes_0.4.6.md
