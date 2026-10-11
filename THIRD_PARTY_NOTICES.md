# Third-party notices

The root MIT license covers original Sylar.soluciones source. It does not license the game or override upstream dependency terms.

| Component | Source / license | Use |
| --- | --- | --- |
| DKC1Recomp host integration, SRAM adaptation and Reconstruct | Pinned revision in upstream.lock.json; MIT | Host APIs, private save handling and filter adaptation |
| DKC2Recomp graphics provenance | MIT notice reproduced in the upstream notices shipped below | Reconstruct/graphics lineage |
| snesrecomp | Separately obtained pinned source; PolyForm Noncommercial 1.0.0 | Native engine built locally |
| Structural metadata in upstream recomp/*.cfg | Upstream declares GPL-3 disassembly provenance | Not vendored here; private generation inputs |
| Khronos OpenXR Android loader | Maven 1.1.53; Apache-2.0 OR MIT and component notices | XR loader fetched by Gradle |
| Android native_app_glue | Android NDK notices | Compiled from the user's installed NDK |

Original license texts and notices are retained under `android/app/src/main/assets/licenses/`, including the DKC1 and DKC2 MIT notices, snesrecomp's exact pinned license/attributions and Android/Khronos notices. The exact pinned snesrecomp license includes the author's clarifications; do not replace it with a generic PolyForm text. Engine copies are subject to their own terms, including noncommercial use. No game assets, disassembly, structural config files or generated game code are distributed in this repository.

References:

- https://github.com/elliotttate/DKC1Recomp/blob/3eb9a10c49cb210df2bd63addafe2a78cf9f4e22/LICENSE
- https://github.com/elliotttate/DKC1Recomp/blob/3eb9a10c49cb210df2bd63addafe2a78cf9f4e22/THIRD_PARTY_NOTICES.md
- https://github.com/elliotttate/snesrecomp/blob/de94587464524ebac8dcd62e14618bf0d3953644/LICENSE
- https://polyformproject.org/licenses/noncommercial/1.0.0

Donkey Kong Country, its characters, artwork, sounds and trademarks remain the property of their respective rights holders. The project supplies neither authorization nor a download source for the game. Please use this experiment without commercial exploitation of the restricted engine.
