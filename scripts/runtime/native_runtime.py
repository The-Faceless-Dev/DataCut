"""Install and inspect Windows native libraries used by isolated model runtimes.

Run with the runtime's own Python after installing pefile and zstandard. This
keeps VC/OpenMP binaries beside that runtime, without an admin installer or a
machine-wide PATH change.
"""
from __future__ import annotations

import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import sys
import tarfile
import time
from urllib.request import Request, urlopen
import zipfile

import pefile
import zstandard


PACKAGES = (
    ("vc14_runtime-14.51.36247-habf1de7_41.conda",
     "4e4cb599cdc41bf2109d1464c127b5bcbddf548ce3e322e612afb691338b48f8"),
    ("vcomp14-14.51.36247-habf1de7_41.conda",
     "731e043390c9457299484d39e427221fc868a9249540a498a5a4f6456c7744d1"),
)
OPENMP_NAME = "llvm-openmp-14.0.0-h2d74725_0.tar.bz2"
OPENMP_SHA256 = "32bc9dddcc4a374678557e8fe047091567adbb1efa402f339a2b8c3de73a061c"
_DLL_HANDLE = None


def emit(stage: str, **extra: object) -> None:
    print(json.dumps({"stage": stage, **extra}), flush=True)


def download(url: str, path: Path, digest: str) -> bytes:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_file() and hashlib.sha256(path.read_bytes()).hexdigest() == digest:
        return path.read_bytes()
    last_error: Exception | None = None
    for attempt in range(3):
        try:
            with urlopen(Request(url, headers={"User-Agent": "DataCut-runtime-setup"}), timeout=90) as response:
                data = response.read()
            if hashlib.sha256(data).hexdigest() != digest:
                raise RuntimeError(f"{path.name} failed SHA-256 verification")
            temporary = path.with_name(path.name + ".part")
            temporary.write_bytes(data)
            temporary.replace(path)
            return data
        except Exception as error:
            last_error = error
            if attempt < 2:
                emit(f"Retrying private native package {path.name}")
                time.sleep(attempt + 1)
    raise RuntimeError(f"Could not download verified native package {path.name}: {last_error}") from last_error


def package_members(data: bytes):
    with zipfile.ZipFile(io.BytesIO(data)) as outer:
        for name in outer.namelist():
            if not name.endswith(".tar.zst"):
                continue
            with zstandard.ZstdDecompressor().stream_reader(io.BytesIO(outer.read(name))) as stream:
                with tarfile.open(fileobj=stream, mode="r|") as archive:
                    for member in archive:
                        if member.isfile() and (member.name.startswith("Library/bin/")
                                                or member.name.startswith("info/licenses/")):
                            source = archive.extractfile(member)
                            if source:
                                yield member.name, source.read()


def provision(runtime: Path, bundled: Path | None = None) -> Path:
    global _DLL_HANDLE
    native = runtime / "native"
    native.mkdir(parents=True, exist_ok=True)
    bootstrap = runtime / "bootstrap"
    bundled_ready = bundled is not None and all(
        (bundled / name).is_file() for name in
        ("vcruntime140.dll", "msvcp140.dll", "vcomp140.dll", "libomp140.x86_64.dll"))
    if bundled_ready:
        emit("Installing native libraries shipped with DataCut")
        for source in bundled.glob("*.dll"):
            target = native / source.name
            if not target.is_file() or target.stat().st_size != source.stat().st_size:
                temporary = target.with_name(target.name + ".part")
                shutil.copyfile(source, temporary)
                temporary.replace(target)
        notices = bundled.parent / "licenses" / "native"
        if notices.is_dir():
            bootstrap.mkdir(parents=True, exist_ok=True)
            for source in notices.glob("*.TXT"):
                shutil.copyfile(source, bootstrap / source.name)
    else:
        for filename, digest in PACKAGES:
            emit(f"Preparing private {filename.split('-')[0]} libraries")
            url = f"https://conda.anaconda.org/conda-forge/win-64/{filename}"
            for name, data in package_members(download(url, bootstrap / filename, digest)):
                if name.startswith("Library/bin/") and name.lower().endswith(".dll"):
                    target = native / Path(name).name
                    if not target.is_file() or target.read_bytes() != data:
                        temporary = target.with_name(target.name + ".part")
                        temporary.write_bytes(data)
                        temporary.replace(target)
                elif name.startswith("info/licenses/") and name.lower().endswith("license.txt"):
                    (bootstrap / f"{filename}-LICENSE.txt").write_bytes(data)

    torch_lib = Path(sys.prefix) / "Lib/site-packages/torch/lib"
    if (torch_lib / "fbgemm.dll").is_file() and not (torch_lib / "libomp140.x86_64.dll").is_file():
        emit("Preparing private LLVM OpenMP library")
        target = torch_lib / "libomp140.x86_64.dll"
        temporary = target.with_name(target.name + ".part")
        if bundled_ready:
            shutil.copyfile(bundled / "libomp140.x86_64.dll", temporary)
        else:
            url = f"https://conda.anaconda.org/conda-forge/win-64/{OPENMP_NAME}"
            data = download(url, bootstrap / OPENMP_NAME, OPENMP_SHA256)
            with tarfile.open(fileobj=io.BytesIO(data), mode="r:bz2") as archive:
                library = archive.extractfile("Library/bin/libomp.dll")
                license_file = archive.extractfile("info/licenses/openmp/LICENSE.TXT")
                if library is None or license_file is None:
                    raise RuntimeError("OpenMP archive lacks its library or license")
                temporary.write_bytes(library.read())
                (bootstrap / "llvm-openmp-LICENSE.txt").write_bytes(license_file.read())
        temporary.replace(target)

    site_packages = Path(sys.prefix) / "Lib/site-packages"
    # A .pth import runs before model packages are imported by every private
    # Python process. PATH alone is ignored for extension dependencies in 3.8+.
    (site_packages / "datacut_native.pth").write_text("import datacut_native\n", encoding="utf-8")
    (site_packages / "datacut_native.py").write_text(
        "import os\nfrom pathlib import Path\n"
        "_root = Path(__file__).resolve().parents[3] / 'native'\n"
        "_handle = os.add_dll_directory(str(_root)) if os.name == 'nt' and _root.is_dir() else None\n",
        encoding="utf-8")
    if os.name == "nt":
        _DLL_HANDLE = os.add_dll_directory(str(native))
    return native


def scan(runtime: Path, native: Path) -> None:
    torch_lib = Path(sys.prefix) / "Lib/site-packages/torch/lib"
    targets = [torch_lib / name for name in ("fbgemm.dll", "torch_cpu.dll", "torch_cuda.dll", "c10.dll")]
    targets += [Path(sys.prefix) / name for name in (
        "Lib/site-packages/torch/_C.pyd",
        "Lib/site-packages/torchaudio/lib/_torchaudio.pyd",
        "Lib/site-packages/torchvision/_C.pyd")]
    windows = Path(os.environ.get("SystemRoot", "C:/Windows"))
    roots = [torch_lib, native, Path(sys.prefix) / "Scripts", Path(sys.base_prefix),
             windows / "System32", windows]
    pending = [target for target in targets if target.is_file()]
    visited: set[Path] = set()
    missing: set[str] = set()
    while pending:
        binary = pending.pop()
        if binary in visited:
            continue
        visited.add(binary)
        if len(visited) > 500:
            raise RuntimeError("Native dependency graph is unexpectedly large")
        try:
            image = pefile.PE(str(binary), fast_load=True)
            image.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"]])
            imports = [entry.dll.decode("ascii", errors="replace")
                       for entry in getattr(image, "DIRECTORY_ENTRY_IMPORT", [])]
            image.close()
        except pefile.PEFormatError:
            continue
        for name in imports:
            lower = name.lower()
            if lower.startswith(("api-ms-win-", "ext-ms-win-")):
                continue
            resolved = next((folder / name for folder in [binary.parent, *roots]
                             if (folder / name).is_file()), None)
            if resolved is None:
                missing.add(f"{name} (required by {binary.name})")
            elif resolved.is_relative_to(runtime) and resolved not in visited:
                pending.append(resolved)
    if missing:
        raise RuntimeError("Missing private native libraries: " + ", ".join(sorted(missing)[:12]))


def main() -> None:
    runtime = Path(sys.argv[1]).resolve()
    bundled = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else None
    native = provision(runtime, bundled)
    emit("Checking native model libraries")
    scan(runtime, native)
    (runtime / "native" / "ready.json").write_text(
        json.dumps({"schemaVersion": 1, "packages": [name for name, _ in PACKAGES],
                    "openmp": OPENMP_NAME}), encoding="utf-8")
    emit("Native libraries ready")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        emit("Failed", error=str(error))
        raise SystemExit(1)
