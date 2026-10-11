# DKC1 Quest VR

**Un proyecto de Sylar.soluciones · Sylars Lab.**

Experimental native Android ARM64/OpenXR port of [DKC1Recomp](https://github.com/elliotttate/DKC1Recomp) for **Meta Quest 3**, without RetroArch. Donkey Kong Country remains a side-view game presented as a large panoramic VR scene; Donkey and Diddy use animated relief generated at runtime from the user's original sprites.

**Source only. Bring your own lawfully obtained USA v1.0 ROM.** This repository contains no ROM, extracted artwork/audio, generated game code, game captures, save states or prebuilt game APK/core. Do not upload any of those to issues or pull requests.

## Current milestone: 0.8.2

- Native OpenXR stereo, GLES3 and AAudio, with Quest Touch controls.
- Original 342 × 224 widescreen presentation, preserving the SNES pixel aspect ratio. Physical scene approximately 3.407 × 1.913 m at 2.55 m.
- Normal terrain, separated far background, animated Donkey/Diddy relief in supported Jungle Hijinxs frames. This is approximate sprite relief, not complete 3D models.
- Optional Reconstruct2 + EPX2 filtering to a 1,368 × 896 texture, with the previous EPX4 mode available for comparison. Original game textures; no new HD assets.
- The tester preferred the new filter and reported that gameplay remained fluid. This is subjective feedback from the first level, not a sustained FPS benchmark or full-campaign validation.

This is an early research port. Menus and unsupported layer/relief captures retain the existing presentation. Broader level coverage, independent clean-machine builds and lifecycle testing of this exact milestone need more work. No affiliation with Nintendo, Rare, Meta or upstream maintainers is implied.

## Build and collaborate

Read [BUILD.md](BUILD.md) for pinned dependencies and private local generation. See [architecture](docs/ARCHITECTURE.md), [validation and limitations](docs/VALIDATION.md), [roadmap](docs/ROADMAP.md) and [CONTRIBUTING.md](CONTRIBUTING.md).

Useful contributions include Quest performance profiling, OpenXR lifecycle fixes, build portability, wider level coverage and texture replacement tooling that does not bundle game assets. Open an issue describing the scope, then submit a focused pull request. Synthetic images and repro instructions are preferred over game captures.

## Controls

| Control | Action |
| --- | --- |
| Left stick | Move |
| A | Jump |
| B | Run/roll |
| X | Switch Donkey/Diddy |
| Y | Start |
| Left stick click | Compare relief/flat |
| Right stick click | Compare filtered/original pixels |
| Right index trigger | Compare Reconstruct/previous EPX4 |

Feature flags default off in the source. Enable them explicitly in your private app data for local testing; see BUILD.md.

## Licensing and game content

Our original host/frontend code is MIT; adapted upstream code retains its notices. The separately obtained engine is **PolyForm Noncommercial 1.0.0**, and upstream structural metadata has GPL-3 provenance. The entire game stack is not relicensed as MIT. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Game names and trademarks belong to their owners. This is an unofficial, noncommercial experiment. Excluding the ROM does not grant rights to Nintendo/Rare artwork or guarantee that a fan project cannot receive a rights complaint.

Project contact: [sylar.soluciones@gmail.com](mailto:sylar.soluciones@gmail.com).
