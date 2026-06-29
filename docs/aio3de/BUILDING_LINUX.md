# Building aio3de on Linux (Ubuntu 22.04)

These are the exact, verified-from-a-clean-machine steps to configure and build
the engine on Ubuntu 22.04 (Jammy), including the friction points that are not
obvious from the upstream README and how this fork addresses them.

> Verified on: Ubuntu 22.04, clang 14, 8 cores / 31 GB RAM. A full Editor build
> needs **tens of GB of disk** and is CPU-bound for a long time.

## 1. Prerequisites

### System packages
```bash
sudo apt-get update
sudo apt-get install -y \
    binutils clang git git-lfs libglu1-mesa-dev libxcb-xinerama0 \
    libfontconfig1-dev libxcb-xkb-dev libxcb-randr0-dev libxkbcommon-x11-dev \
    libxkbcommon-dev libxcb-xfixes0-dev libxcb-xinput-dev libxcb-xinput0 \
    libxcb-icccm4-dev libxcb-image0-dev libxcb-keysyms1-dev \
    libxcb-render-util0-dev libpcre2-16-0 libunwind-dev libzstd-dev \
    ninja-build python3-pip software-properties-common mesa-common-dev \
    libvulkan1 xdg-utils desktop-file-utils \
    libegl1-mesa-dev libgles2-mesa-dev
```

> **Friction point (EGL):** the prebuilt **Qt 6 `libQt6Gui`** links against EGL,
> so without `libegl1-mesa-dev` / `libgles2-mesa-dev` the build fails late while
> linking Qt tools (e.g. `LuaIDE`) with `undefined reference to 'eglChooseConfig'`
> and `libEGL.so.1 ... not found`. These two packages (not in the upstream
> Dockerfile's list) provide the linkable `libEGL.so` / `libGLESv2.so`.

### CMake ≥ 3.24 (friction point)
O3DE requires **CMake 3.24+**, but Ubuntu 22.04's `apt` ships **3.22.1**.
Install a newer CMake from Kitware's official binaries (do **not** rely on the
apt `cmake`):
```bash
CMV=3.27.9
curl -sSL -o /tmp/cmake.tar.gz \
  "https://github.com/Kitware/CMake/releases/download/v${CMV}/cmake-${CMV}-linux-x86_64.tar.gz"
sudo tar --strip-components=1 -xzf /tmp/cmake.tar.gz -C /usr/local
cmake --version   # should report >= 3.24
```

### Git LFS
This repo stores large binaries in Git LFS:
```bash
git lfs install
git lfs pull
```

## 2. Fetch the O3DE Python runtime
O3DE ships its own Python; fetch it once:
```bash
./python/get_python.sh
```

## 3. Configure
```bash
export CC=clang CXX=clang++
cmake -B build/linux -S . -G "Ninja Multi-Config" \
      -DLY_3RDPARTY_PATH=$HOME/o3de-packages \
      -DLY_DISABLE_TEST_MODULES=ON
```
The first configure downloads the 3rd-party packages (Qt, PhysX, Lua, …) into
`$HOME/o3de-packages` and clones a handful of `FetchContent` dependencies into
`build/linux/_deps`.

### Friction point: tinyusdz / Assimp USD importer (fixed in this fork)
Assimp's USD importer pulls **tinyusdz** through a `FetchContent` declared
*inside Assimp's own* `code/CMakeLists.txt`. When that nested call runs from
within Assimp's `add_subdirectory` scope, CMake treats the dependency as already
populated and **skips the clone**, leaving the hard-coded path
`build/linux/_deps/tinyusdz_repo-src` empty. Configure then fails at the generate
step with:
```
CMake Error at .../_deps/assimp-src/code/CMakeLists.txt:1393 (ADD_LIBRARY):
  Cannot find source file: .../_deps/tinyusdz_repo-src/src/ascii-parser.cc
```
**Fix (already applied here):**
`Code/Tools/SceneAPI/SDKWrapper/3rdParty/Findassimp.cmake` now declares and
populates `tinyusdz_repo` at the engine level — the same scope every other O3DE
3rd-party dependency is fetched from, which populates reliably — *before*
Assimp is added. This wins the first `FetchContent_Declare` and guarantees the
sources exist at the path Assimp expects. No manual step is required.

If you ever hit a similar empty-`_deps` clone, you can also work around it by
hand:
```bash
git clone https://github.com/lighttransport/tinyusdz \
    build/linux/_deps/tinyusdz_repo-src
git -C build/linux/_deps/tinyusdz_repo-src checkout \
    6050eef932f7d2788656d63297aa488fb0961ed1
# then re-run the configure command WITHOUT deleting build/linux
```

## 4. Build
Build the editor and asset processor (profile config):
```bash
export CC=clang CXX=clang++
cmake --build build/linux --target Editor AssetProcessor --config profile
```
Binaries land in `build/linux/bin/profile/`.

Other useful targets: `AutomatedTesting.GameLauncher` (the sample project
runtime), `AssetBuilder`.

## 5. Running the Editor (headless / CI machines)
The Editor is a Qt/OpenGL+Vulkan GUI app. On a machine without a physical
display, run it under a virtual display so it can create a window and render:
```bash
sudo apt-get install -y xvfb
xvfb-run -s "-screen 0 1920x1080x24" \
    ./build/linux/bin/profile/Editor
```
The first launch processes the `AutomatedTesting` project's assets via Asset
Processor, which can take a while.
