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

- **Windows:** install **Visual Studio** with the *"Desktop development with
  C++"* workload, and **CMake** (anywhere, as long as it's on `PATH`).
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
third-party path, free disk space, engine registration, and (on Linux) the
runtime libraries the Editor needs — each with a one-line fix. Resolve any
`FAIL` before continuing; `WARN`s are advisory.

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
# Windows (Visual Studio generator)
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
