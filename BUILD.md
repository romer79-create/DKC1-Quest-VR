# Local builds with external private inputs

Un proyecto de Sylar.soluciones. This repository supplies source only. Keep the ROM outside Git and generate the game code locally. The exact pinned engine has noncommercial conditions; read its license before using it.

## Prerequisites

- Git, Python 3.10+, Rust/Cargo 1.85+ and a working host compiler (Visual Studio Build Tools developer shell on Windows).
- CMake 3.22+, Ninja, Android SDK platform/build-tools 36 and **NDK 27.0.12077973**.
- JDK 21 and **Gradle 8.13** on PATH. There is no wrapper JAR or bundled toolchain in this source-only repository.
- Quest 3 with developer mode enabled and an authorized local ADB connection for your own builds.
- Your lawfully obtained headerless USA v1.0 ROM: 4,194,304 bytes, SHA-256 `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15`.

Use short paths on Windows; the private generator can produce many translation units. All paths below are examples for a fresh local setup, not paths from the tester's machine.

## Get upstream without vendoring it

Place the upstream checkout outside this repository:

```sh
git clone https://github.com/elliotttate/DKC1Recomp.git ../dkc1-upstream
git -C ../dkc1-upstream checkout 3eb9a10c49cb210df2bd63addafe2a78cf9f4e22
git -C ../dkc1-upstream submodule update --init --recursive
```

`upstream.lock.json` records both exact revisions. Do not copy the checkout, its metadata or generated output into a pull request.

## Generate and build your private core

The script verifies the external ROM and pinned revisions. It copies only upstream config into your private work directory, creates a passive instrumented PPU copy there, and never copies the ROM or changes the upstream checkout.

```sh
python scripts/private_core.py prepare --upstream ../dkc1-upstream --rom /external/path/your-game.sfc --work ../dkc1-private
python scripts/private_core.py generate --upstream ../dkc1-upstream --rom /external/path/your-game.sfc --work ../dkc1-private
python scripts/private_core.py build --upstream ../dkc1-upstream --rom /external/path/your-game.sfc --work ../dkc1-private --ndk /path/to/android-sdk/ndk/27.0.12077973
```

The resulting `../dkc1-private/core/libsylar_dkc1.so` contains private generated game code. **Do not distribute it, its object files, generated C, logs containing generated snippets or the resulting game APK.** The experimental source export is for collaborators to build their own inputs locally.

An advanced incremental path accepts `--reuse-compile-commands /private/original/core-build/compile_commands.json`. It expects the original 2,230 selected Android PIC objects, excluding the desktop/headless host and original PPU object. Reusing trusted existing generation was tested separately; see docs/VALIDATION.md. A full clean generation/build on a new machine is still a contribution area.

## Build the Android frontend

Set `SYLAR_CORE_LIBRARY` to your private core's absolute path. Configure SDK discovery with `ANDROID_HOME`/`ANDROID_SDK_ROOT` or an ignored `android/local.properties` containing your own SDK path. Set JAVA_HOME to JDK 21, then run:

```sh
gradle -p android --no-daemon --dependency-verification strict :app:assembleDebug :app:lintDebug
```

Gradle fetches the pinned Android plugin/OpenXR dependency using committed verification metadata. Add `--offline` only after those dependencies are already cached. Public debug signing uses your own local Android debug key, not the tester's private key.

The APK is written under ignored `.local/android-app/outputs/apk/debug/`. Install your own build on your Quest with `adb install -r /path/to/app-debug.apk`. It will use `soluciones.sylar.dkc1questvr`; if another build has a different signing key, use a separate test device/profile or manage your own install and data backup. Do not uninstall the tester's accepted app as part of source validation.

## Local ROM import and features

The host reads only `files/game.sfc` in its own Android private data directory and checks the hash. This early frontend has no file-picker UI. With your locally signed debug build, import your own external ROM into that private app directory through `adb shell run-as soluciones.sylar.dkc1questvr`; this device import is separate from Git. Never put the ROM in Android assets or the source repository. SRAM belongs in the app's private `files/saves/save.srm`.

For testing, the private text files `files/panorama.enabled`, `files/relief-gameplay.enabled` and `files/reconstruction.enabled` contain `1` to enable the respective opt-in capabilities; absence or `0` keeps them off. The published frontend has no terrain slab code. The right index trigger compares reconstruction/previous EPX4 once enabled.

## Publication checks

```sh
python scripts/check_public_source.py
python scripts/check_public_source.py --tracked --history
```

The second command requires Git initialization/commits. This allowlist guard rejects binary artifacts, private directories, obvious credential patterns and tester identifiers. Public CI uses only source and synthetic host tests; it never receives ROMs or private frame fixtures. Review every tracked file as well as the guard result before sharing.
