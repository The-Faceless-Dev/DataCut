# DataCut

**0.7-beta** · Windows 10/11 · native C++23 and Qt 6 video editor.

DataCut provides frame-aware timeline editing, multiple video and audio tracks, captions, text, effects, transitions, collections, and local exports. Optional local generation runtimes are installed separately through the app. Core editing works without them or a network connection.

## Download

Get the portable Windows build from the [0.7-beta release](https://github.com/The-Faceless-Dev/DataCut/releases/tag/v0.7-beta). Extract the complete ZIP, then run `datacut.exe` inside the `DataCut` folder. Keep the DLLs and resource folders beside the executable.

The release ZIP contains the application and its native runtime libraries. It does **not** contain AI model weights, installed Python environments, user projects, generated media, or account data. Optional generation features offer separate setup from Settings.

## Features

- Import video, audio, and images, including drag and drop from Explorer.
- Edit on video, audio, and caption tracks with trimming, splitting, snapping, keyframes, and undo/redo.
- Apply built-in video effects and transitions; edit text and caption appearance.
- Save non-destructive media ranges into collections.
- Export a project, range, clip, frame, audio file, or image sequence.
- Autosave versioned `.datacutproject` files and relink moved source media.
- Use optional local generation for captions, voice conversion, images, video, music, and sound effects after setting up their separate runtimes.

DataCut is beta software. Save copies of important projects and inspect exported results before relying on them.

## Build from source

See [BUILDING.md](BUILDING.md). The source tree contains no installed model runtimes. The packaging script builds a portable folder from a locally compiled executable and downloads the native library packages it needs with SHA-256 verification.

## Project and user data

Project files are readable JSON. Imported media stays at its original location and is referenced by the project. Local generation runtimes and output paths can be changed in Settings. Diagnostic logs and application settings are stored in the user's local DataCut application data directory.

## License

See [LICENSE](LICENSE). Bundled third-party components retain their own licenses, included in the portable package. Source information for the bundled public-domain voice samples is in `assets/voices/`.
