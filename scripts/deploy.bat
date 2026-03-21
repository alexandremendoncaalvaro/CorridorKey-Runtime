git checkout -b release/v0.4.5
git add -A
git commit -m "feat: release v0.4.5 UX improvements and alpha hint graceful fallback"
git push -u origin release/v0.4.5
gh pr create --title "Release v0.4.5: UX Defaults & Alpha Hint Fix" --body "Bumps to 0.4.5. Removes Auto from Quantization/InputColor dropdowns, defaults to Preview/FP16/Linear, removes Refiner Scale, enforces external Alpha Hint with transparent fallback instead of flickering, and fixes version string desync."
gh pr merge --merge --admin --delete-branch
gh release create resolve-v0.4.5 dist\CorridorKey_Resolve_v0.4.5_Windows_Installer.exe dist\CorridorKey_Resolve_v0.4.5_Windows.zip dist\CorridorKey_Resolve_v0.4.5_Windows_DirectML_Installer.exe dist\CorridorKey_Resolve_v0.4.5_Windows_DirectML.zip --title "CorridorKey Resolve OFX v0.4.5 (Windows)" --notes-file docs\release_notes_0.4.5.md
