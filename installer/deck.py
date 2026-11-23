"""SteamOS installer entry point, bundled with the released Windows renderer."""
import argparse
import fcntl
import hashlib
from html import escape
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import textwrap
import zipfile


ENGINE = "b0097e4edd96fa2c32745b04328939eb8d182c2e89121d51405bc486b13f9108"
AUDIO = "20179b6187e278f07b7373465d49d167a7e298f4bca40c1b28d70d8d984191d9"
RELEASES = {
    "911fdf90d7bc79272848b13423a8819f37a8d09de6e3ec93d604155dafb91d09",
    "c77f80dccc0061e7f3a3fa60bbe23ab0fde8ac4de723c99c1df3f34367e1b57a",
    "4930ab8731a76ee99903c26c502f1bf41a7190ed2d3c18e64e3902581ed7d901",
    "2f2407da01db619ff9078ae5d9902f653bb50b959476ec2a25611caf65d0644f",
    "3339f935e77cb8a28f1f2addf614e0a3333fbc3ef0c49d80ec8c9d98e7a0f489",
    "cb8a3db8f9c3c77840b74874d0295f4581299b870f70982304a4992ff414623f",
    "aac40e350972ae7158ac2d72e8bf0f8f239b520268ce3bf9a39da642ccedf5e1",
    "7006067e76701023c8a1786a5179fce87b019e99674b5090b956628f1b090296",
    "03465a50fc67995293c18a51f98e287956751578bd374bf5aa1f0f1e0f6d9e15",
    "c5e9ded60962b80f5bbc164a72be75469b1840bce455500154c63a121d498247",
    "8d7a75297c187304eed79c095b91def40a73c1f10910f3de56936769de07bbb9",
    "7503827d6be2e74e621066488abba60a5ed5d52fea3fd55d2c4deb232d1e921f",
    "2cc4805f16125c1c99ec9677b8a5e6df24804ad4db48a5aeb3c27f6a75643012",
    "1f1630675d267ac23db7537f5b4a88c39ffa4ad3881e9fdc399c2e299cc6f932",
    "c911bb6326455f9a6a4e869e0aa92d91e086571f1b81319195b8610970182435",
    "fed75113fb720842e951ec09bb71ceec7d85aa0e7cf1e9fd8d3a702dd9847720",
    "cf118d4dd2735d0e53b9e8a88097a251023a93327d193a51587fbf7248a50932",
    "b9a0e7957ac4405fc403e99414d909bf1c17aa0f5523cba65e354ca97fd163af",
    "4beeef929975a15f13bd85bf1493563b05d217f0a80db66b61dcd698fa182a88",
    "e2c4f3aa88ab33e454112343b93959eb822d7e1b27fa87c9e037b1f172ba3f5c",
    "32029a6cf27875594f4717c77ca3d874cd74af581edf85f716ca6c157623b74b",
    "8096cdaaa1f65daa1eaaa7aa89c76f669729b9afd499a99b8eebc6adf2c29e95",
    "99d7fb6af8a2960299789625e3c6055843462f1b65e680a5486020d472a68b33",
    "001e0c1d4151503df8488277412df534664f9724fa682d15d453aec157e564f2",
    "5c32cd4cc3a73d63d0721fe7b790386ca3cfdb05e4838172c28eed7e0cae6e94",
    "e141730672cdf74a817e54d9571c49186e084f8d50728e27ca54b3b040642eae",
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


def clear_renderer_updates(game):
    receipt = game / "FF8-Reborn-update.txt"
    if receipt.is_symlink() or not receipt.is_file():
        return
    with receipt.open("rb") as stream:
        data = stream.read(257)
    match = re.fullmatch(
        rb"FF8-Reborn-Update-1\ndeck\n([AB])\n([1-9][0-9]{0,9})\n([0-9a-f]{64}|-)\n([0-9a-f]{64}|-)\n",
        data,
    )
    if not match or int(match[2]) > 0xffffffff:
        return
    hashes = {slot: value.decode("ascii") for slot, value in zip("AB", match.groups()[2:])}
    if hashes[match[1].decode("ascii")] == "-":
        return
    for slot, expected in hashes.items():
        path = game / f"FF8-Reborn-{slot}.dll"
        if expected == "-" or path.is_symlink() or not path.is_file():
            continue
        if path.stat().st_size <= 64 * 1024 * 1024 and digest(path) == expected:
            path.unlink()
    receipt.unlink()


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
    clear_renderer_updates(game)


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


def theme_environment(folder):
    # GTK 3 and 4 both read the per-application copy. Fontconfig registers
    # the bundled font for this process only, including on read-only SteamOS.
    assets = ("FF8Menu.ttf", "deck-theme.css")
    if zipfile.is_zipfile(sys.argv[0]):
        with zipfile.ZipFile(sys.argv[0]) as package:
            for name in assets:
                (folder / name).write_bytes(package.read(name))
    else:
        for name in assets:
            (folder / name).write_bytes(Path(__file__).with_name(name).read_bytes())
    style = (folder / "deck-theme.css").read_text(encoding="utf-8")
    for gtk in ("gtk-3.0", "gtk-4.0"):
        theme = folder / gtk
        theme.mkdir()
        (theme / "gtk.css").write_text(style, encoding="utf-8")
    conf = folder / "fonts.conf"
    system_fonts = os.environ.get("FONTCONFIG_FILE", "/etc/fonts/fonts.conf")
    conf.write_text(
        '<fontconfig><include ignore_missing="yes">' + escape(system_fonts) +
        '</include><dir>' + escape(str(folder)) + '</dir><cachedir>' +
        escape(str(folder / "cache")) + '</cachedir></fontconfig>', encoding="utf-8")
    return dict(os.environ, XDG_CONFIG_HOME=str(folder), XDG_CACHE_HOME=str(folder / "cache"),
                FONTCONFIG_FILE=str(conf))


def dialog(*args):
    cmd = ["zenity", "--title=FF8 Reborn", *args]
    # Let the system file picker keep its normal bookmarks, fonts and icons.
    if "--file-selection" in args:
        result = subprocess.run(cmd, stdout=subprocess.PIPE, text=True)
    else:
        with tempfile.TemporaryDirectory(prefix="ff8-reborn-ui-") as temp:
            result = subprocess.run(cmd, env=theme_environment(Path(temp)), stdout=subprocess.PIPE, text=True)
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
        folder = dialog("--list", "--text=Choose a game installation.", "--column=Game folder",
                        "--width=700", *map(str, games))
        if not folder:
            return
        game = Path(folder)

    installed = (game / "FF8_geometry.installed").is_file() or (game / "libvgmstream_orig.dll").is_file()
    heading = '<span size="x-large" foreground="#a2f5ff">FF8 Reborn</span>'
    path = escape("\n".join(textwrap.wrap(str(game), width=50, break_on_hyphens=False,
                                         replace_whitespace=False, drop_whitespace=False)))
    if installed:
        action = dialog("--list", "--radiolist", "--column=", "--column=Action", "--hide-header",
                        "--ok-label=Apply", "--width=600", "--height=350",
                        "--text", heading + "\nFinal Fantasy VIII Remastered\n\n" + path,
                        "TRUE", "Update or repair", "FALSE", "Uninstall")
        if not action:
            return
        uninstall = action == "Uninstall"
    else:
        if dialog("--question", "--width=600", "--ok-label=Install", "--cancel-label=Cancel",
                  "--text", heading + "\nFinal Fantasy VIII Remastered\n\n" + path +
                  "\n\nConfigure the mod in game with F8 or Select + Start.") is None:
            return
        uninstall = False
    install_game(game, uninstall)
    message = ("Original game files restored." if uninstall else
               "Installation complete.\n\nReturn to Gaming Mode and launch the game from Steam.")
    dialog("--info", "--width=600", "--text", heading + "\n\n" + message + "\n\n" + path)


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
