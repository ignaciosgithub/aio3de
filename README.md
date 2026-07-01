# O3DE (Open 3D Engine)

O3DE (Open 3D Engine) is an open-source, real-time, multi-platform 3D engine that enables developers and content creators to build AAA games, cinema-quality 3D worlds, and high-fidelity simulations without any fees or commercial obligations.

## About this fork (aio3de)

I personally believe this is the most ambitious AI project for non-profit being developed. Through a 4-year cycle of updates and fixes, I expect the engine to become one of the most popular options for game development by the end of it.

### Focus areas

- **Optimized development workflow** — fast and intuitive development, from onboarding to iteration.
- **Built-in core features** — more gems that reproduce the basic features found in most games, such as a save system and pathfinding across surfaces and volumes.
- **AI for NPCs** — NPC behaviour that can be programmed, but also — unlike most modern engines — deep integration with machine-learning models that can be created, trained, optimized, and used in engine through a Python backend. Complex NPC behaviour can rely on neural network models rather than strict behaviour trees, including importing existing open-source models.
- **Cross-platform support with performance in mind** — a customized approach to performant software based on how the target platform is structured.

## Contribute
For information about contributing to Open 3D Engine, visit [https://o3de.org/docs/contributing/](https://o3de.org/docs/contributing/).

## Roadmap
For information about upcoming work and features, please visit [https://o3de.org/roadmap](https://o3de.org/roadmap). Progress against the roadmap is tracked [here](https://github.com/orgs/o3de/projects/56/views/2).

## Download and Install

This repository uses Git LFS for storing large binary files.  

Verify you have Git LFS installed by running the following command to print the version number.
```
git lfs --version 
```

If Git LFS is not installed, download and run the installer from: [https://git-lfs.github.com/](https://git-lfs.github.com/).

### Install Git LFS hooks 
```
git lfs install
```


### Clone the repository 

```shell
git clone https://github.com/o3de/o3de.git
```

## Building the Engine

### Build requirements and redistributables

For the latest details and system requirements, refer to [System Requirements](https://o3de.org/docs/welcome-guide/requirements/) in the documentation.

#### Windows

*   Visual Studio 2019 16.9.2 minimum (All editions supported, including Community): [https://visualstudio.microsoft.com/downloads/](https://visualstudio.microsoft.com/downloads/)
    *   Check [System Requirements](https://o3de.org/docs/welcome-guide/requirements/) for other supported versions.
    *   Install the following workloads:
        *   Game Development with C++
        *   MSVC v142 - VS 2019 C++ x64/x86
        *   C++ 2019 redistributable update
*   CMake 3.24.0 minimum: [https://cmake.org/download/#latest](https://cmake.org/download/#latest) (Release Candidate versions are not supported)

#### Optional

*   Wwise audio SDK
    *   For the latest version requirements and setup instructions, refer to the [Wwise Audio Engine Gem](https://o3de.org/docs/user-guide/gems/reference/audio/wwise/audio-engine-wwise/) reference in the documentation.

### Quick start engine setup

Setting up the engine and your first project is five steps that work the same on
**Windows and Linux**:

1. **Install prerequisites** — Git + Git LFS, CMake ≥ 3.24, a C++ toolchain
   (Visual Studio "Desktop development with C++" on Windows; clang + system libs
   on Linux), and a writable `<3rdParty>` cache folder.
2. **Check your machine** — `scripts/o3de.sh hub doctor` (`.bat` on Windows)
   reports exactly what's missing and how to fix it.
3. **Register the engine** — `scripts/o3de.sh register --this-engine`.
4. **Create a project** — `scripts/o3de.sh create-project --project-path <project>`.
5. **Configure and build** the project (project-centric), e.g. on Linux:
   ```
   cmake -B <project>/build/linux -S <project> -G "Ninja Multi-Config" -DLY_3RDPARTY_PATH=<3rdParty>
   cmake --build <project>/build/linux --target <ProjectName>.GameLauncher Editor --config profile
   ```

**Full, digestible walkthrough (both platforms, GUI option, troubleshooting):
[`docs/aio3de/QUICKSTART.md`](docs/aio3de/QUICKSTART.md).**

Use `scripts/o3de.sh hub status` at any time to see which projects map to which
engine. For the upstream reference see
[Setting up O3DE from GitHub](https://o3de.org/docs/welcome-guide/setup/setup-from-github/).

## Code Contributors

This project exists thanks to all the people who contribute. [[Contribute](CONTRIBUTING.md)].

<a href="https://github.com/o3de/o3de/graphs/contributors"><img src="https://contrib.rocks/image?repo=o3de/o3de&max=200&columns=24" width=850px /></a>

## License

For terms please see the LICENSE*.TXT files at the root of this distribution.
