# Building the installers

Players can use the ready-made installer in [Releases](https://github.com/Faga250885/FF8-Reborn/releases). For a local build, use Windows, PowerShell, a Zig C++ toolchain capable of targeting 32-bit Windows with Clang's `#embed` extension, GLEW headers and the .NET Framework 4.x C# compiler. The published build uses the Zig 0.17.0 development toolchain.

From the repository root, point the script to the include directory containing `GL/glew.h`.

```powershell
.\build.ps1 -GlewInclude 'C:\deps\glew\include' -Zig 'C:\tools\zig\zig.exe'
```

The outputs are `dist/FF8-Reborn-Setup.exe` for Windows, `dist/FF8-Reborn-Deck-Setup` for the SteamOS desktop and `dist/FF8-Reborn-Deck.pyz` for the Linux command line. The SteamOS executable is a static x86-64 Linux launcher with the Python installer archive appended. It opens the bundled installer with the system Python and displays Zenity dialogs. All three packages contain the same renderer. The original game, Steamworks runtime and original audio library come from the user's Steam installation.

The renderer and filter are in `src/`. The setup window and installation checks are in `installer/`. Steam ownership is checked using the game's initialized Steam session. [Steamworks ownership API](https://partner.steamgames.com/doc/api/ISteamApps#GetAppOwner).

## Verify the downloaded installer

Download the installer and the accompanying SHA256SUMS.txt from the same release. In PowerShell, run the following in the download folder and compare the resulting hash with the entry for FF8-Reborn-Setup.exe.

~~~powershell
Get-FileHash .\FF8-Reborn-Setup.exe -Algorithm SHA256
~~~
