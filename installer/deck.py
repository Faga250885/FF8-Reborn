"""SteamOS installer entry point, bundled with the released Windows renderer."""
import argparse
import fcntl
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import zipfile


ENGINE = "b0097e4edd96fa2c32745b04328939eb8d182c2e89121d51405bc486b13f9108"
AUDIO = "20179b6187e278f07b7373465d49d167a7e298f4bca40c1b28d70d8d984191d9"
RELEASES = {
    "77c1e2fe8e4c9ddf75b17f046d2fa421bd72a9919170c37b16eef075c9cd8715",
    "0b1d0584940091a82517c4a589ecccce099136b1a7066d3e8079763979c26478",
    "4c94343b9e9ae71b43563192bd1cf85b0aac4b1f9f337c5cef87029d8403826f",
    "b94861d2be712ba0aedf0e46678f3622c8864078aae8524623f71c9ede0ed23a",
    "2c529429de01de44f04030b6977fa150c7786b908f9b7f6bcd9478f6a9e56c01",
    "95b0b8cbbcd5436b7b7f734bcac12aaeb66a0095c4c5f70e134e13721186e43d",
    "4850a85cc7c491132d14c4e36e679f1363917cf5ea65e39a3c47868f39a442c3",
}


def libraries(home):
    roots = [home / ".local/share/Steam", home / ".steam/steam",
             home / ".var/app/com.valvesoftware.Steam/data/Steam"]
    result = set()
    for root in roots:
        result.add(root.resolve())
        index = root / "steamapps/libraryfolders.vdf"
        if index.is_file():
            for folder in re.findall(r'"path"\s*"([^"]+)"', index.read_text(encoding="utf-8-sig")):
                if Path(folder).is_absolute():
                    result.add(Path(folder).resolve())
    return sorted(result)


def game_folder(library):
    apps = library / "steamapps"
    manifest = apps / "appmanifest_1026680.acf"
    if not manifest.is_file():
        return None
    values = dict(re.findall(r'"(appid|installdir)"\s*"([^"]+)"', manifest.read_text(encoding="utf-8-sig")))
    name = values.get("installdir", "")
    if values.get("appid") != "1026680" or name in ("", ".", "..") or Path(name).name != name:
        return None
    game = apps / "common" / name
    return game.resolve() if (game / "FFVIII_EFIGS.dll").is_file() else None


def game_running():
    for proc in Path("/proc").glob("[0-9]*"):
        try:
            name = (proc / "comm").read_text(errors="replace").strip().lower()
            if name in ("ffviii.exe", "ffviii_launcher", "ffviii_launcher.exe"):
                return True
            args = (proc / "cmdline").read_bytes().lower().replace(b"\\", b"/").split(b"\0")
            if any(arg.rsplit(b"/", 1)[-1] in (b"ffviii.exe", b"ffviii_launcher.exe") for arg in args):
                return True
        except (FileNotFoundError, ProcessLookupError, PermissionError):
            continue
    return False


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None


def write_atomic(path, data):
    with tempfile.NamedTemporaryFile(dir=path.parent, prefix=".ff8-reborn-", delete=False) as temp:
        tmp = Path(temp.name)
    try:
        tmp.write_bytes(data)
        tmp.chmod(0o644)
        os.replace(tmp, path)
    finally:
        tmp.unlink(missing_ok=True)


def apply(game, payload, version, uninstall):
    audio = game / "libvgmstream.dll"
    backup = game / "libvgmstream_orig.dll"
    marker = game / "FF8_geometry.installed"
    if any(path.is_symlink() for path in (audio, backup, marker)):
        raise ValueError("A managed file is a symbolic link. Check the game installation.")
    if (game / "FF8_geometry.pending").exists() or digest(game / "FFVIII_EFIGS.dll") != ENGINE:
        raise ValueError("Pending installation or unsupported game engine.")
    if hashlib.sha256(payload).hexdigest() != version:
        raise ValueError("The renderer failed its integrity check.")
    known = RELEASES | {version}
    active = digest(audio)
    if active not in known | {AUDIO}:
        raise ValueError("Unrecognized audio DLL. Check for another mod.")
    if backup.exists() or active != AUDIO:
        if digest(backup) != AUDIO:
            raise ValueError("Missing or unexpected original audio backup.")
    if marker.exists() and marker.read_text(encoding="utf-8-sig").strip().lower() not in known:
        raise ValueError("Unrecognized installation marker.")
    if uninstall:
        if active != AUDIO:
            write_atomic(audio, backup.read_bytes())
        if digest(audio) != AUDIO:
            raise ValueError("Could not verify the original audio DLL.")
        marker.unlink(missing_ok=True)
        backup.unlink(missing_ok=True)
    else:
        if not backup.exists():
            write_atomic(backup, audio.read_bytes())
        if active != version:
            write_atomic(audio, payload)
        if digest(audio) != version:
            raise ValueError("Could not verify the installed renderer.")
        write_atomic(marker, version.upper().encode("ascii"))


def install_game(game, uninstall):
    with zipfile.ZipFile(sys.argv[0]) as package:
        payload = package.read("libvgmstream.dll")
        version = package.read("renderer.sha256").decode("ascii").strip().lower()
    fd = os.open(game, os.O_RDONLY | os.O_DIRECTORY)
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        if game_running():
            raise ValueError("Close the game and its launcher first.")
        apply(game, payload, version, uninstall)
    finally:
        os.close(fd)
    return ("Original DLL restored in " if uninstall else "FF8 Reborn installed in ") + str(game)


def dialog(*args):
    result = subprocess.run(["zenity", "--title=FF8 Reborn", *args], stdout=subprocess.PIPE, text=True)
    if result.returncode == 1:
        return None
    result.check_returncode()
    return result.stdout.rstrip("\n")


def graphical_install(games):
    if not games:
        folder = dialog("--file-selection", "--directory", "--title=Choose the Steam library containing steamapps")
        if not folder:
            return
        game = game_folder(Path(folder))
        if not game:
            raise ValueError("FFVIII Remastered was not found in this library. Choose the folder containing steamapps.")
    elif len(games) == 1:
        game = games[0]
    else:
        folder = dialog("--list", "--column=Game folder", "--width=700", *map(str, games))
        if not folder:
            return
        game = Path(folder)

    installed = (game / "FF8_geometry.installed").is_file() or (game / "libvgmstream_orig.dll").is_file()
    if installed:
        action = dialog("--list", "--radiolist", "--column=", "--column=Action", "--no-markup",
                        "--text", str(game), "TRUE", "Update or repair", "FALSE", "Uninstall")
        if not action:
            return
        uninstall = action == "Uninstall"
    else:
        if dialog("--question", "--no-markup", "--ok-label=Install", "--cancel-label=Cancel",
                  "--text", "Install FF8 Reborn here?\n\n" + str(game)) is None:
            return
        uninstall = False
    message = install_game(game, uninstall)
    if not uninstall:
        message += "\n\nReturn to Gaming Mode and launch the game from Steam."
    dialog("--info", "--no-markup", "--text", message)


def main():
    parser = argparse.ArgumentParser(description="Install FF8 Reborn in your Steam library.")
    parser.add_argument("--library", type=Path, help="Steam library folder containing steamapps")
    parser.add_argument("--uninstall", action="store_true", help="restore the original audio DLL")
    parser.add_argument("--gui", action="store_true", help="open the graphical installer")
    args = parser.parse_args()
    try:
        roots = [args.library.expanduser()] if args.library else libraries(Path.home())
        games = sorted({game for root in roots if (game := game_folder(root))})
        if args.gui:
            graphical_install(games)
            return
        if not games:
            raise ValueError("Game not found. Install FFVIII Remastered in Steam or specify --library PATH.")
        if len(games) > 1:
            raise ValueError("Multiple copies found. Run again with --library followed by one of:\n" +
                             "\n".join(str(root) for root in roots if game_folder(root)))
        print(install_game(games[0], args.uninstall))
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as error:
        if args.gui:
            dialog("--error", "--no-markup", "--text", str(error))
        raise


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as error:
        sys.exit(str(error))
