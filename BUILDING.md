# Building DataCut on Windows

DataCut uses C++23, Qt 6 Quick, FFmpeg, and CMake. The supported build path is MSYS2 UCRT64 on Windows 10/11 x64.

## Install the toolchain

Install [MSYS2](https://www.msys2.org/) and open its **UCRT64** shell. Install the compiler, build tools, Qt modules, and FFmpeg:

```sh
pacman -Syu
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qt6-declarative mingw-w64-ucrt-x86_64-qt6-multimedia mingw-w64-ucrt-x86_64-qt6-shadertools mingw-w64-ucrt-x86_64-ffmpeg
```

Install Python 3.11 or newer for the packaging script, then install its one extra dependency:

```powershell
python -m pip install zstandard
```

## Compile

From the UCRT64 shell in this repository:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/ucrt64
cmake --build build --parallel
```

## Package

From PowerShell, set `MSYS2_UCRT_BIN` if MSYS2 is not installed at `C:\msys64`, then run:

```powershell
$env:MSYS2_UCRT_BIN = 'C:\msys64\ucrt64\bin'
python scripts\package_windows.py
```

The output is `dist\DataCut\` and `dist\DataCut-0.7-beta-windows-x64.zip`. The packaging script copies Qt, FFmpeg, and UCRT64 DLL dependencies and their license notices. Optional generation runtimes, models, project files, and generated results are not packaged.

Do not commit `build\`, `dist\`, generated media, private runtimes, or local settings. The `.gitignore` excludes them; releases carry the portable ZIP separately.
