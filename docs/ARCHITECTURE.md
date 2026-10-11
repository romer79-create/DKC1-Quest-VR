# Architecture

`native-host/` owns a process-lifetime engine worker, ROM verification, SRAM, pacing, passive layer/actor capture and a lock-free stereo PCM ring. AAudio consumes PCM; engine access remains on the worker. NativeActivity attachment is separate from the engine's process lifetime to survive activity recreation.

`android/` contains the OpenXR/GLES frontend. Each eye retains its pose/FOV and recommended resolution; MSAA4 resolves the projected scene. Ground/actor registration stays at 2.55 m and the far background at 3.20 m. A closed height surface gives the current DK/Diddy sprites approximate relief, anchored to their native pixel coordinates. It does not reveal authentic unseen backs/sides or replace every sprite with a 3D model.

The accepted filter runs Reconstruct at 2× and then EPX2 to 4×. It filters opaque original-color neighborhoods and retains EPX handling around transparency. Source colours are converted appropriately for the sRGB textures. Terrain remains the original layered scene; the previously tried slab geometry is not in this export.

Upstream checkout, ROM-derived generated C/config, private PPU copy, object files, libraries, APKs and native frame fixtures stay external or under ignored `.local/`. Only original host/frontend source, procedural build recipes and license texts are published. The backend still models SNES hardware; native compilation does not imply the absence of hardware emulation/modeling.
