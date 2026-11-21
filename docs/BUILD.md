# Building the Windows installer

Players can use the ready-made installer in [Releases](https://github.com/Faga250885/FF8-Reborn/releases). For a local build, use Windows, PowerShell, a Zig C++ toolchain capable of targeting 32-bit Windows with Clang's `#embed` extension, GLEW headers and the .NET Framework 4.x C# compiler. The published build uses the Zig 0.17.0 development toolchain.

From the repository root, point the script to the include directory containing `GL/glew.h`.

```powershell
.\build.ps1 -GlewInclude 'C:\deps\glew\include' -Zig 'C:\tools\zig\zig.exe'
```

The output is `dist/FF8-Reborn-Setup.exe`. The script builds the x86 renderer, strips debug symbols, compresses the mod DLL, embeds its SHA-256 and builds the Windows Forms installer. The original game, Steamworks runtime and original audio library come from the user's Steam installation.

The renderer and filter are in `src/`. The settings window, installation checks and filter preview are in `installer/`. Steam ownership is checked using the game's initialized Steam session. [Steamworks ownership API](https://partner.steamgames.com/doc/api/ISteamApps#GetAppOwner).
