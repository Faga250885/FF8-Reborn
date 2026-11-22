# Building the installers

Players can use the ready-made installer in [Releases](https://github.com/Faga250885/FF8-Reborn/releases). For a local build, use Windows, PowerShell, a Zig C++ toolchain capable of targeting 32-bit Windows with Clang's `#embed` extension, GLEW headers and the .NET Framework 4.x C# compiler. The published build uses the Zig 0.17.0 development toolchain.

From the repository root, point the script to the include directory containing `GL/glew.h`.

```powershell
.\build.ps1 -GlewInclude 'C:\deps\glew\include' -Zig 'C:\tools\zig\zig.exe'
```

The outputs are `dist/FF8-Reborn-Setup.exe` for Windows, `dist/FF8-Reborn-Deck-Setup` for the SteamOS desktop and `dist/FF8-Reborn-Deck.pyz` for the Linux command line. The SteamOS executable is a static x86-64 Linux launcher with the Python installer archive appended. It opens the bundled installer with the system Python and displays Zenity dialogs. The SteamOS packages embed `dist/deck/libvgmstream.dll`, compiled with `GEO_STEAM_DECK` for Proton. The Windows installer embeds the standard renderer. The original game, Steamworks runtime and original audio library come from the user's Steam installation.

The renderer and filter are in `src/`. The setup window and installation checks are in `installer/`. Steam ownership is checked using the game's initialized Steam session. [Steamworks ownership API](https://partner.steamgames.com/doc/api/ISteamApps#GetAppOwner).

## Publish an in-game update

1. Set the channel's release tag and increase its build number in `src/update_version.h`. Windows uses stable tags such as `v1.4.0`. Steam Deck uses tags such as `v1.4.0-deck.1`. Each channel compares its own build numbers.
2. Run the build script. It creates the regular installers, a renderer for each channel and two small feeds in `dist/updates/`.
3. Upload the installer and the matching `FF8-Reborn-windows.dll` or `FF8-Reborn-deck.dll` to the release for that exact tag. Keep published assets immutable. Verify their SHA-256 digests against the build.
4. After the assets are downloadable, copy the matching `windows.txt` or `deck.txt` from `dist/updates/` into `updates/` and publish it on `main`. This advertises the update to existing installations on that channel.

The menu contacts this repository on demand. After the user chooses to download, the mod verifies the SHA-256 digest, platform and build, writes the renderer alongside the installed base, then atomically selects it for the next launch. The installed base verifies the selection again before loading it. An installer is still required for releases that change the bootstrap format.

## Troubleshooting a Steam Deck black flash

For a pre-display trace of battle interpolation, close FFVIII and create an empty file named `FF8_geometry_output_probe.enabled` next to `FFVIII.exe`. With **30 FPS** selected, the Deck renderer samples small regions of completed battle frames before presentation. Reproduce the flash, close FFVIII and look for `Deck dark output` in `FF8_geometry.log`. Each entry identifies the scene, tick and drawing pass. Gamescope and the physical display process the image after this point. Remove the marker after testing to disable sampling.

## Steam Deck regression tests

The Deck DLL targets the Zen 2 CPU in both Deck models and disables floating-point contraction to preserve the camera calculations. To run the regression tests, open PowerShell at the repository root and set `$GlewInclude` to the GLEW include directory used for the renderer build.

```powershell
$libs = '-lopengl32','-lgdi32','-luser32','-lcomctl32','-lwinhttp','-ladvapi32'
foreach ($file in Get-ChildItem tests -Filter '*.cpp') {
    $test = $file.BaseName
    $defines = @('-DGEO_STEAM_DECK')
    if ($test -eq 'deck_native_fields') { $defines += '-DGEO_NATIVE_CAPTURE' }
    zig c++ -target x86-windows-gnu -march=znver2 -ffp-contract=off -std=c++17 -O2 @defines "-I$GlewInclude" $file.FullName -o "dist/$test.exe" @libs
    if ($LASTEXITCODE) { throw "$test build failed" }
    & "./dist/$test.exe"
    if ($LASTEXITCODE) { throw "$test failed" }
}
zig c++ -target x86-windows-gnu -std=c++17 -O2 "-I$GlewInclude" tests/frame_rate_settings.cpp -o dist/windows-settings.exe @libs
if ($LASTEXITCODE) { throw 'Windows settings test build failed' }
./dist/windows-settings.exe
if ($LASTEXITCODE) { throw 'Windows settings test failed' }
```

## Verify the downloaded installer

Download the installer and the accompanying SHA256SUMS.txt from the same release. In PowerShell, run the following in the download folder and compare the resulting hash with the entry for FF8-Reborn-Setup.exe.

~~~powershell
Get-FileHash .\FF8-Reborn-Setup.exe -Algorithm SHA256
~~~
