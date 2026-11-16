"""Verify the runtime files in CPack ZIP and tar.gz archives without extracting."""

import argparse
from pathlib import Path, PurePosixPath
import tarfile
import zipfile


SOURCE_ROOT = Path(__file__).resolve().parent.parent
SOURCES = {
    "api.py": SOURCE_ROOT / "scripts" / "api.py",
    "MANUAL.md": SOURCE_ROOT / "MANUAL.md",
    "RECIPES.md": SOURCE_ROOT / "RECIPES.md",
}


def check_archive(path):
    if path.suffix == ".zip":
        with zipfile.ZipFile(path) as archive:
            files = {entry.filename: archive.read(entry) for entry in archive.infolist()
                     if not entry.is_dir()}
            modes = {entry.filename: entry.external_attr >> 16 for entry in archive.infolist()}
    else:
        with tarfile.open(path, "r:gz") as archive:
            files = {entry.name: archive.extractfile(entry).read() for entry in archive
                     if entry.isfile()}
            modes = {entry.name: entry.mode for entry in archive if entry.isfile()}

    # CPack may wrap all files in a directory named after the package.
    executables = [name for name in files if PurePosixPath(name).name in {"app", "app.exe"}]
    if len(executables) != 1:
        raise ValueError(f"{path}: expected one app or app.exe, found {executables}")
    executable = PurePosixPath(executables[0])
    root = executable.parent
    expected = {str(root / name) for name in SOURCES} | {str(executable)}
    missing = expected - files.keys()
    if missing:
        raise ValueError(f"{path}: missing files: {sorted(missing)}")
    for name in files.keys() - expected:
        relative = PurePosixPath(name).relative_to(root)
        if not relative.parts or relative.parts[0] != "licenses":
            raise ValueError(f"{path}: unexpected file: {name}")
    for name, source in SOURCES.items():
        if files[str(root / name)] != source.read_bytes():
            raise ValueError(f"{path}: {name} does not match the current source")
    if not files[str(executable)]:
        raise ValueError(f"{path}: executable is empty")
    if executable.name == "app" and not modes[str(executable)] & 0o111:
        raise ValueError(f"{path}: app is not executable")
    print(f"Verified {path.name}: {executable.name}, api.py, MANUAL.md, RECIPES.md")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="CPack packages directory")
    args = parser.parse_args()
    archives = sorted(args.directory.glob("*.zip")) + sorted(args.directory.glob("*.tar.gz"))
    if not archives:
        parser.error(f"No ZIP or tar.gz packages in {args.directory}")
    for archive in archives:
        check_archive(archive)


if __name__ == "__main__":
    main()
