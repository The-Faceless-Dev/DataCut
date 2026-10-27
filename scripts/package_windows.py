"""Create a portable Windows folder and ZIP from an MSYS2 UCRT64 build.

Qt's deployment tool handles Qt plugins and QML imports. This script also
follows PE imports so MinGW and FFmpeg DLLs used by those plugins are bundled.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
from urllib.request import urlopen
import zipfile


ROOT = Path(__file__).resolve().parents[1]
DIST = ROOT / "dist"
STAGE = DIST / ".DataCut-staging"
OUTPUT = DIST / "DataCut"
IMPORT_PATTERN = re.compile(r"^\s*DLL Name:\s*(\S+)\s*$", re.IGNORECASE | re.MULTILINE)


def within_workspace(path: Path) -> bool:
    return path.resolve().is_relative_to(ROOT)


def remove_generated_directory(path: Path) -> None:
    if not within_workspace(path):
        raise RuntimeError(f"Refusing to remove a path outside the repository: {path}")
    if path.exists():
        shutil.rmtree(path)


def imports(objdump: Path, binary: Path) -> set[str]:
    result = subprocess.run(
        [str(objdump), "-p", str(binary)], capture_output=True, text=True, errors="replace"
    )
    if result.returncode:
        raise RuntimeError(f"Could not inspect {binary}: {result.stderr.strip()}")
    return {match.group(1) for match in IMPORT_PATTERN.finditer(result.stdout)}


def close_dll_dependencies(stage: Path, ucrt_bin: Path) -> list[str]:
    objdump = ucrt_bin / "objdump.exe"
    system32 = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32"
    copied: list[str] = []
    inspected: set[Path] = set()

    while True:
        binaries = sorted(
            path for path in stage.rglob("*") if path.suffix.lower() in {".exe", ".dll"}
        )
        pending = [path for path in binaries if path not in inspected]
        if not pending:
            break
        for binary in pending:
            inspected.add(binary)
            for name in sorted(imports(objdump, binary), key=str.lower):
                lower = name.lower()
                if lower.startswith(("api-ms-win-", "ext-ms-win-")):
                    continue  # Windows API-set contracts are resolved by the OS loader.
                if (stage / name).is_file() or (binary.parent / name).is_file():
                    continue
                source = ucrt_bin / name
                if source.is_file():
                    shutil.copy2(source, stage / source.name)
                    copied.append(source.name)
                elif not (system32 / name).is_file():
                    raise RuntimeError(f"Missing dependency {name} imported by {binary.relative_to(stage)}")
    return sorted(set(copied), key=str.lower)


def bundle_model_native_libraries(stage: Path) -> None:
    """Ship native prerequisites for the Python-free speech runtime."""
    try:
        import zstandard
    except ImportError as error:
        raise RuntimeError("Packaging needs zstandard: python -m pip install zstandard") from error
    packages = (
        ("vc14_runtime-14.51.36247-habf1de7_41.conda",
         "4e4cb599cdc41bf2109d1464c127b5bcbddf548ce3e322e612afb691338b48f8"),
        ("vcomp14-14.51.36247-habf1de7_41.conda",
         "731e043390c9457299484d39e427221fc868a9249540a498a5a4f6456c7744d1"),
    )
    native = stage / "native"
    notices = stage / "licenses" / "native"
    native.mkdir(parents=True, exist_ok=True)
    notices.mkdir(parents=True, exist_ok=True)
    for filename, digest in packages:
        url = f"https://conda.anaconda.org/conda-forge/win-64/{filename}"
        data = urlopen(url, timeout=90).read()
        if hashlib.sha256(data).hexdigest() != digest:
            raise RuntimeError(f"Native package failed SHA-256 verification: {filename}")
        with zipfile.ZipFile(io.BytesIO(data)) as outer:
            for member_name in outer.namelist():
                if not member_name.endswith(".tar.zst"):
                    continue
                with zstandard.ZstdDecompressor().stream_reader(io.BytesIO(outer.read(member_name))) as stream:
                    with tarfile.open(fileobj=stream, mode="r|") as archive:
                        for member in archive:
                            if not member.isfile():
                                continue
                            if member.name.startswith("Library/bin/") and member.name.lower().endswith(".dll"):
                                source = archive.extractfile(member)
                                if source:
                                    (native / Path(member.name).name).write_bytes(source.read())
                            elif (member_name.startswith("info-") and member.name == "info/licenses/LICENSE.TXT"):
                                source = archive.extractfile(member)
                                if source:
                                    (notices / f"{filename}.LICENSE.TXT").write_bytes(source.read())
    openmp_name = "llvm-openmp-14.0.0-h2d74725_0.tar.bz2"
    openmp_url = f"https://conda.anaconda.org/conda-forge/win-64/{openmp_name}"
    openmp_data = urlopen(openmp_url, timeout=90).read()
    if hashlib.sha256(openmp_data).hexdigest() != "32bc9dddcc4a374678557e8fe047091567adbb1efa402f339a2b8c3de73a061c":
        raise RuntimeError("OpenMP package failed SHA-256 verification")
    with tarfile.open(fileobj=io.BytesIO(openmp_data), mode="r:bz2") as archive:
        library = archive.extractfile("Library/bin/libomp.dll")
        license_file = archive.extractfile("info/licenses/openmp/LICENSE.TXT")
        if library is None or license_file is None:
            raise RuntimeError("OpenMP package is missing its library or license")
        (native / "libomp140.x86_64.dll").write_bytes(library.read())
        (notices / "llvm-openmp.LICENSE.TXT").write_bytes(license_file.read())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ucrt-bin", type=Path,
                        default=Path(os.environ.get("MSYS2_UCRT_BIN", r"C:\msys64\ucrt64\bin")))
    parser.add_argument("--exe", type=Path, default=ROOT / "build" / "datacut.exe")
    args = parser.parse_args()
    ucrt_bin = args.ucrt_bin.resolve()
    executable = args.exe.resolve()
    if not executable.is_file():
        parser.error(f"Build the app first: {executable}")
    if not (ucrt_bin / "windeployqt.exe").is_file() or not (ucrt_bin / "objdump.exe").is_file():
        parser.error(f"Not an MSYS2 UCRT64 binary directory: {ucrt_bin}")

    DIST.mkdir(exist_ok=True)
    remove_generated_directory(STAGE)
    STAGE.mkdir()
    shutil.copy2(executable, STAGE / "datacut.exe")
    shutil.copy2(ROOT / "LICENSE", STAGE / "LICENSE")
    shutil.copy2(ucrt_bin / "ffmpeg.exe", STAGE / "ffmpeg.exe")
    shutil.copytree(ROOT / "scripts" / "runtime", STAGE / "scripts" / "runtime",
                    ignore=shutil.ignore_patterns(
                        "__pycache__", "*.pyc", "voice_change.py",
                        "chatterbox_voice_change.py", "setup_voice_bootstrap.ps1",
                        "setup_chatterbox_bootstrap.ps1"))
    shutil.copytree(ROOT / "assets" / "voices", STAGE / "assets" / "voices")

    environment = os.environ.copy()
    environment["PATH"] = str(ucrt_bin) + os.pathsep + environment.get("PATH", "")
    command = [
        str(ucrt_bin / "windeployqt.exe"),
        "--release", "--no-translations", "--no-compiler-runtime",
        "--skip-plugin-types", "qmltooling",
        "--qmldir", str(ROOT / "ui"),
        str(STAGE / "datacut.exe"),
    ]
    result = subprocess.run(command, capture_output=True, text=True, errors="replace", env=environment)
    if result.returncode:
        raise RuntimeError("windeployqt failed:\n" + result.stdout[-3000:] + result.stderr[-3000:])

    (STAGE / "qt.conf").write_text(
        "[Paths]\nPrefix=.\nPlugins=.\nQmlImports=qml\n", encoding="utf-8"
    )
    demo_dir = ROOT / "demo"
    if demo_dir.is_dir():
        shutil.copytree(demo_dir, STAGE / "demo")
    copied = close_dll_dependencies(STAGE, ucrt_bin)
    license_dir = ucrt_bin.parent / "share" / "licenses"
    if license_dir.is_dir():
        shutil.copytree(license_dir, STAGE / "licenses")
    bundle_model_native_libraries(STAGE)
    (STAGE / "README.txt").write_text(
        "DataCut portable build for Windows x64\n\n"
        "Extract the complete folder, then open datacut.exe. Keep its DLLs and\n"
        "qml, platforms, and multimedia directories beside the executable.\n"
        "No MSYS2 or Qt installation is needed on the destination computer.\n"
        "Core editing does not need an AI runtime. Optional local generation\n"
        "requires a supported NVIDIA GPU and runtime setup in Settings > Generative.\n",
        encoding="utf-8",
    )

    preserved_generations = DIST / ".DataCut-generations-preserved"
    installed_generations = OUTPUT / "generations"
    if not all(within_workspace(path) for path in
               (STAGE, OUTPUT, preserved_generations, installed_generations)):
        raise RuntimeError("Package paths must stay inside the repository")
    if preserved_generations.exists():
        raise RuntimeError(f"Restore preserved generations before packaging: {preserved_generations}")
    if installed_generations.exists():
        installed_generations.rename(preserved_generations)
    try:
        remove_generated_directory(OUTPUT)
        STAGE.rename(OUTPUT)
        archive = Path(shutil.make_archive(str(DIST / "DataCut-0.7-beta.1-windows-x64"),
                                           "zip", DIST, "DataCut"))
        # The development executable remains usable; optional models are never
        # copied into the build tree or included in the portable archive.
        if executable.parent == (ROOT / "build").resolve():
            shutil.copytree(OUTPUT, executable.parent, dirs_exist_ok=True)
    finally:
        if preserved_generations.exists():
            OUTPUT.mkdir(exist_ok=True)
            if installed_generations.exists():
                raise RuntimeError(f"Cannot restore generations over {installed_generations}")
            preserved_generations.rename(installed_generations)
    print(f"Portable folder: {OUTPUT}")
    print(f"Portable ZIP: {archive}")
    if executable.parent == (ROOT / "build").resolve():
        print(f"Development executable deployed: {executable}")
    print(f"Copied {len(copied)} additional runtime DLLs, including: {', '.join(copied[:12])}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError) as error:
        print(f"Packaging failed: {error}", file=sys.stderr)
        sys.exit(1)
