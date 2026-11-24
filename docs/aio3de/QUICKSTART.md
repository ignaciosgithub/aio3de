# Quick start: set up an engine and a project

This is the short version of "set up O3DE from source and build your first
project". It works the same way on **Windows and Linux** — only the paths and
the system prerequisites differ.

The whole flow is five steps:

1. [Install prerequisites](#1-install-prerequisites)
2. [Check your machine](#2-check-your-machine-o3de-hub-doctor)
3. [Register the engine](#3-register-the-engine)
4. [Create a project](#4-create-a-project)
5. [Build it](#5-build-it)

> **Prefer a GUI?** Steps 2–4 are also available as a small desktop window that
> runs on **Windows and Linux** before you've built anything (it uses your
> *system* Python, so it works on a fresh clone):
>
> ```bash
> # Windows
> scripts\o3de_hub.bat
> # Linux / macOS
> scripts/o3de_hub.sh
> ```
>
> It shows the preflight checks as green/red rows (with one-click fix links,
> including the Windows Smart App Control / VC++ gotchas below) and lets you
> create a project with an explicit **name** + location, so you never end up with
> an accidental "Default Project". The CLI steps below do exactly the same thing.

## Prebuilt SDK (no engine compile)

The prebuilt SDK contains the Editor, Project Manager, asset tools, prebuilt
engine libraries, and the FPS, Platformer, and Racing templates. Projects build
only their own code; a C++ compiler is still required because all three starter
templates include C++ code.

From an engine checkout, install and register the newest compatible SDK with:

```bash
scripts/o3de.sh hub sdk install
```

Linux SDK archives use zstd compression. Install the extractor first if needed:

```bash
sudo apt install zstd
```

You can also install without cloning the repository by downloading the
standalone, standard-library-only installer:

```bash
# Linux
curl -fsSLO https://raw.githubusercontent.com/ignaciosgithub/aio3de/development/scripts/o3de/o3de/sdk.py
python3 sdk.py install
```

```powershell
# Windows PowerShell
iwr https://raw.githubusercontent.com/ignaciosgithub/aio3de/development/scripts/o3de/o3de/sdk.py -OutFile sdk.py
py sdk.py install
```

The installer registers the SDK and prints its next-step commands. You can
create a Racing project from the installed SDK's own script:

```bash
~/O3DE/Engines/aio3de-sdk-<tag>/scripts/o3de.sh create-project --project-path ~/O3DEProjects/MyRacing --template-name RacingProject
cd ~/O3DEProjects/MyRacing
cmake -B build/linux -S . -G "Ninja Multi-Config"
cmake --build build/linux --config profile --target MyRacing.GameLauncher Editor
```

On Windows Command Prompt, use the installed SDK's batch script:

```bat
%USERPROFILE%\O3DE\Engines\aio3de-sdk-<tag>\scripts\o3de.bat create-project --project-path %USERPROFILE%\O3DEProjects\MyRacing --template-name RacingProject
cd %USERPROFILE%\O3DEProjects\MyRacing
cmake -B build\windows -S . -G "Visual Studio 17 2022"
cmake --build build\windows --config profile --target MyRacing.GameLauncher Editor
```

The GUI alternative is Project Manager: `bin/Linux/profile/Default/o3de` on
Linux or `bin\Windows\profile\Default\o3de.exe` on Windows. Choose **New Project**,
select **FPS**, **Platformer**, or **Racing**, then click **Build**.

Linux SDKs target Ubuntu 22.04 or newer (glibc ≥ 2.35). Windows pre-alpha
binaries are unsigned. If you download a ZIP manually in a browser, right-click
it → Properties → **Unblock** before extracting; this is not needed when the
installer downloads the SDK. If SmartScreen still appears, click **More info →
Run anyway**.

> **Mental model:** the **engine** is the source tree you cloned. A **project**
> is a separate folder that *points at* an engine. You register the engine once,
> then create as many projects as you like against it. `o3de hub status` shows
> you which project points at which engine at any time.

Throughout, replace the placeholders:

| Placeholder | Meaning | Windows example | Linux example |
|---|---|---|---|
| `<engine>` | where you cloned this repo | `C:\o3de` | `~/o3de` |
| `<3rdParty>` | writable cache for downloaded packages | `C:\o3de-packages` | `~/o3de-packages` |
| `<project>` | where your new project will live | `C:\my-project` | `~/my-project` |

Pick a `<3rdParty>` folder once and reuse it for everything — it caches large
downloadable third-party packages (and any other SDKs you want to keep). **No
trailing slashes** on these paths.

---

## 1. Install prerequisites

**Both platforms need:** Git **+ Git LFS**, CMake **≥ 3.24**, a C++ toolchain,
and Python 3.10+ (the engine ships its own — see step 3).

- **Windows:** install **Visual Studio 2022 or 2026** with the *"Desktop
  development with C++"* workload, and **CMake** (anywhere, as long as it's on
  `PATH`; the `Visual Studio 18 2026` generator needs **CMake >= 4.1**). Also
  install the **[Microsoft Visual C++ Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe)** —
  the engine's bundled Python needs it, and without it the first `o3de` command
  fails while creating the Python venv (a crash with no error detail). Note that
  a working *system* Python on your `PATH` does **not** satisfy this; the engine
  always uses its own bundled interpreter.
- **Windows — Smart App Control / Device Guard:** if a command fails with
  *"blocked by your organization's Device Guard policy"* (or the bundled
  `python.exe` crashes on launch with no detail), Windows is blocking *unsigned*
  executables. Turn off **Smart App Control** (Windows Security → *App & browser
  control* → *Smart App Control settings* → **Off**). This is required to run the
  bundled Python **and the engine binaries you build**. Note the switch is
  one-way: re-enabling it needs a Windows reinstall. This is a personal-machine
  default in Windows 11 — it is not literally an employer policy.
- **Linux (Ubuntu):** install clang + the system libraries. The exact,
  verified apt list is in [`BUILDING_LINUX.md`](./BUILDING_LINUX.md).

Don't hand-verify all of that — let the engine tell you what's missing in the
next step.

## 2. Check your machine (`o3de hub doctor`)

From the engine folder, run the built-in preflight check:

```bash
# Windows
scripts\o3de.bat hub doctor
# Linux / macOS
scripts/o3de.sh hub doctor
```

It reports `OK` / `WARN` / `FAIL` for Python, CMake (against the version this
engine actually requires), your C++ compiler, Ninja, Git, Git LFS, the
third-party path, free disk space, engine registration, on **Windows** the VC++
redistributable and whether a code-integrity policy (Smart App Control / Device
Guard) is blocking unsigned binaries, and on **Linux** the runtime libraries the
Editor needs — each with a one-line fix. Resolve any `FAIL` before continuing;
`WARN`s are advisory.

To fix everything automatically instead of by hand, run:

```bash
# Windows (installs missing tools via winget)
scripts\o3de.bat hub install
# Linux / macOS (installs system packages via sudo/pkexec)
scripts/o3de.sh hub install
```

`hub install` installs the missing system packages (Ninja, Git, Git LFS,
compiler, Editor runtime libraries; on apt distros it runs `apt-get update`
first), runs `git lfs install` + `git lfs pull` (in git checkouts), creates the
3rdParty folder, bootstraps the engine's Python environment and registers the
engine — then re-runs the checks. Add `--dry-run` to only see the
plan. The GUI hub (`scripts/o3de_hub.sh` / `.bat`) has the same thing as an
**Install missing** button on the Preflight tab, plus a **Build & Run** tab
that configures and builds a project's Editor and launches the Asset
Processor/Editor with one click each.

- **CMake** on Linux comes from Kitware's official release (verified against
  its published SHA-256) into `~/.o3de/tools/cmake`, linked into
  `~/.local/bin` — no root needed, and it works on distros whose packaged
  CMake is older than the engine requires (e.g. Ubuntu 22.04). The `o3de`
  scripts put it first on `PATH` automatically, and the first-run Python
  setup (`python/get_python.sh`) installs it itself when needed, so even
  `scripts/o3de.sh hub install` works on a machine with no usable CMake.
- **Compiler** on Windows: when no MSVC toolset is found, it installs Visual
  Studio 2022 Build Tools with the C++ workload + Windows SDK (several GB).
  Packages that are already installed are skipped, and tools installed earlier
  in the run are usable by the later steps without opening a new terminal. If
  `winget` itself is missing, the doctor says so (install "App Installer" from
  the Microsoft Store).
- **The GUI hub launchers** find a real Python 3.10+ with Tkinter (skipping the
  Windows Store `python.exe` stub) and install one if needed: Python 3.12 for
  the current user via winget on Windows, `python3` + Tkinter via
  apt/dnf/pacman on Linux.

> Avoid engine paths containing spaces or non-ASCII characters (e.g.
> `~/Área de trabalho/...`) — the doctor warns about them because assorted
> build tools mishandle such paths. Prefer plain paths like `~/o3de`.

## 3. Register the engine

Registration is what lets projects find this engine. Run it once:

```bash
# Windows
scripts\o3de.bat register --this-engine
# Linux / macOS
scripts/o3de.sh register --this-engine
```

> The `o3de` CLI uses the engine's own bundled Python, not your system Python.
> The **first** `o3de` command on a fresh clone downloads and sets up that
> Python automatically (a few minutes) before running — no separate `get_python`
> step needed. Set `O3DE_AUTO_PYTHON_SETUP=0` to opt out and run
> `python/get_python.bat` (Windows) / `python/get_python.sh` (Linux/macOS)
> yourself.

## 4. Create a project

```bash
# Windows
scripts\o3de.bat create-project --project-path <project>
# Linux / macOS
scripts/o3de.sh create-project --project-path <project>
```

This makes a new project folder that references the engine you just registered.
You can confirm the link any time with `o3de hub status`, or check a single
project (and find its compatible engine) with:

```bash
scripts/o3de.sh hub resolve --project-path <project>
```

## 5. Build it

Projects are **project-centric**: you configure and build from the *project*
folder, not the engine folder.

**Configure** (point CMake at the project, with your third-party cache):

```bash
# Windows (Visual Studio generator; VS 2022 shown - use "Visual Studio 18 2026"
# for VS 2026, which requires CMake >= 4.1)
cmake -B <project>/build/windows -S <project> -G "Visual Studio 17 2022" -DLY_3RDPARTY_PATH=<3rdParty>
# Linux (Ninja Multi-Config)
cmake -B <project>/build/linux -S <project> -G "Ninja Multi-Config" -DLY_3RDPARTY_PATH=<3rdParty>
```

**Build** the Editor and your game launcher (`profile` is the optimized,
debuggable config):

```bash
# Windows
cmake --build <project>/build/windows --target <ProjectName>.GameLauncher Editor --config profile -- /m
# Linux
cmake --build <project>/build/linux  --target <ProjectName>.GameLauncher Editor --config profile
```

`<ProjectName>` is the project's directory name. Binaries land in
`<project>/build/<platform>/bin/profile`. First build downloads third-party
packages and compiles the engine, so it takes a while; later builds are
incremental.

---

## 6. Collaborating: Git LFS and branch switching

Large binary assets (textures, models, audio — 130+ patterns in
`.gitattributes`) are stored in **Git LFS**. A plain `git clone`/`git checkout`
may leave them as small *pointer* text files instead of real content, and
switching branches can fail on untracked local files. The hub gives you a
pipeline for both:

```bash
# What is LFS-tracked, which files are still undownloaded pointers,
# and (with --ref) what a branch would need to download:
scripts/o3de.sh hub lfs --ref feature/new-map

# Branch-switch preflight + switch: reports untracked-file collisions and the
# LFS downloads needed, then does "git checkout" + "git lfs pull" in one step:
scripts/o3de.sh hub checkout feature/new-map
scripts/o3de.sh hub checkout feature/new-map --dry-run   # report only
```

(Windows: `scripts\o3de.bat hub lfs` / `hub checkout`.) Both accept
`--repo-path` to run against a project repo instead of the engine. If a
teammate reports grey/missing assets after a pull, `hub lfs` will show the
pointer-only files and the fix (`git lfs pull`).

## Prefer a GUI?

On Windows you can configure with **`cmake-gui`** instead of the command line:
set the source to `<project>`, the build folder to `<project>/build/windows`,
add a `STRING` cache entry `LY_3RDPARTY_PATH=<3rdParty>`, then **Configure** →
**Generate**.

## If something goes wrong

- Re-run **`o3de hub doctor`** — most setup failures are a missing prerequisite
  it will name with a fix.
- Engine/project not lining up? **`o3de hub status`** shows every registered
  engine and project and how they map.
- Linux build/link/runtime errors → [`BUILDING_LINUX.md`](./BUILDING_LINUX.md)
  documents the non-obvious friction points and their fixes.
- For the full reference, see the upstream
  [Setting up O3DE from GitHub](https://www.docs.o3de.org/docs/welcome-guide/setup/setup-from-github/)
  guide.
