# DeusExHRVR

Experimental VR mod for **Deus Ex: Human Revolution — Director's Cut**, using the game's native AMD HD3D stereo renderer. **Both eyes are rendered in the same game frame. No AER.**

Gameplay stereo, headset tracking, HUD alignment, motion-controlled weapon aiming, controller buttons, and selectable interaction/walking directions have been tested in-headset. Automatic widescreen menus and several lighting, shadow and sky corrections are included. This remains an experimental prerelease; weapon visibility at extreme viewing angles and untested missions/effects still need broader testing.

Version 0.3.0 includes per-weapon grip calibration, the SteamVR OpenXR sRGB compatibility fix, controller menu chords, and optional immersive sniper scope. The imported Luma lighting/color work remains experimental and disabled by default; its compiled shaders are included. XeGTAO is disabled following headset flicker and performance regressions. Roomscale body movement is not implemented.

## Download and install

Download the packaged build from [Releases](https://github.com/farmerarmor/DeusExHRVR/releases). GitHub's automatic source ZIP does not contain compiled DLLs.

Supported game: **Steam Director's Cut 2.0.66.0**, executable SHA256:

```text
8266B6B4A5BF25F2F4E8DE068AA3720F6289C962BB1C2BB70A7B1C111BA510A1
```

Other executable versions and the original non-Director's Cut release are not supported by this build. Requires Windows, DirectX 11, a PC-connected headset, and an active OpenXR runtime. The initial test used the Oculus runtime. Installation does not change the system runtime.

1. Extract the release archive.
2. Close Deus Ex and connect the headset.
3. Open PowerShell in the extracted `DeusExHRVR` folder and run the following, replacing the example with your installation path:

   ```powershell
   powershell -ExecutionPolicy Bypass -File .\install.ps1 -GameDirectory "E:\SteamLibrary\steamapps\common\Deus Ex Human Revolution Director's Cut"
   ```

4. Launch `DXHRDC.exe` and load a save. Gameplay enters full VR automatically. Press **F9** to recenter if needed.

For motion-controlled weapons, copy `DeusExHRVR.ini.example` beside `DXHRDC.exe` as `DeusExHRVR.ini`, set `ExperimentalMotionControls=1`, and restart. `MotionControls=1` enables controller buttons; `InteractionAim` and `MovementDirection` independently accept `Mouse`, `Headset`, or `Controller`. The confirmed local setup uses `WorldUnitsPerMetre=300` with both direction settings at `Headset`; adjust scale for comfort. Upgrading replaces both the game DLL and its companion together. Existing INI settings are preserved.

For a ready-made comfort setup, copy `DeusExHRVR.ini.comfort` beside `DXHRDC.exe` as `DeusExHRVR.ini` instead. It turns on the options below that remove imposed head motion (yaw-only camera, stance hold, heading-swing filtering), snap turn at 30°, controller interaction aim and headset walking direction, with `WorldUnitsPerMetre=300`. It keeps the stock button layout. Nothing in it is enabled by default.

Run the installer as the Windows user who plays the game. It backs up replaced files and original graphics settings to `DeusExHRVR-backup` in the game folder. It enables DX11/native stereo and disables VSync and antialiasing for the tested configuration.

Keep the companion in the game's `DeusExHRVR/DeusExHRVRHost.exe` subfolder so it cannot load the game's 32-bit proxy DLLs.

## Headset resolution and refresh rate

Connect the headset before launching. The mod queries the active OpenXR runtime at startup and renders each eye at its recommended resolution, including the runtime's current resolution setting. It does not upscale the old desktop-sized image. Restart the game after changing the headset's resolution setting. If the startup query fails, the mod logs the failure and retains the game's original resolution for that run.

Complete native stereo pairs are paced by OpenXR frame requests. The desktop mirror uses a windowed, nonblocking presentation path so desktop VSync and a 60 Hz monitor do not throttle VR. The headset's selected refresh rate is retained; the mod does not switch it to the highest available rate. Runtime reprojection and GPU/CPU limits can still lower the rate of newly rendered frames.

The initial live check confirmed 1344 × 1600 per eye and approximately 90 native pairs/submissions per second on a headset set to 90 Hz. Gameplay and HUD appearance were confirmed in-headset. This is one tested configuration, not a guarantee of that performance at higher resolutions.

## Controls

| Key | Action |
|---|---|
| F3 | Toggle per-eye projected light/shadow transforms (on by default) |
| F4 | Toggle per-eye shader camera inputs (on by default; requires F7) |
| F6 | Toggle tracked VR and capture a neutral head pose |
| F7 | Toggle the shared lighting-depth correction (on by default) |
| F9 | Recenter |
| F8 | Save both-eye images, camera trace, and any armed rolling recording locally |
| F10 | Toggle the rolling stereo-frame recorder (off by default) |

Full VR is enabled by default and starts automatically when gameplay loads. F6 manually toggles between full VR and the virtual screen. Gameplay accepts keyboard/mouse, physical gamepad, or the motion controllers through the game's Xbox input path.

Two independent `[VR]` settings choose where interaction targeting and walking point:

```ini
InteractionAim=Headset
MovementDirection=Headset
```

Each accepts `Mouse`, `Headset`, or `Controller` (the right controller's aim). Omitted settings default to `Mouse`, retaining native mouse/gamepad look direction. Interaction aim selects doors and items independently of the gun. Walking uses horizontal heading only; looking up/down does not tilt movement. Controller direction works with the gun holstered and does not require the weapon experiment. Menus, virtual-screen modes and unavailable tracking retain native direction. Restart after changing settings. These direction options are experimental and need in-game validation.

Terminal interaction, hacking, the main/pause/game-over menus, sniper scope aiming, and prerecorded video playback automatically use the 16:9 virtual screen. Full VR resumes afterward unless you disabled it with F6. The screen appears in front of your current head position; VR rendering retains the headset resolution and refresh rate.

Set `LockVerticalCamera=1` under `[VR]` in `DeusExHRVR.ini` to keep mouse/gamepad aiming pitch out of the full-VR camera. The gun still aims vertically, while the VR camera uses a level base plus your headset pitch. Horizontal look stays available. Virtual-screen modes, including scoped aiming, retain the native camera. The option defaults to `0` (off); restart the game after changing it.

Set `MotionControls=0` under `[VR]` to disable all motion-controller features, including controller tracking, the weapon experiment, and controller-based interaction/walking. Headset VR and `Headset` direction settings stay available; `Controller` directions fall back to native mouse/gamepad direction. `MotionControls=1` enables controller features selected by the other settings and is the default for compatibility. Restart after changing it.

To adjust a weapon's grip, use full VR with motion-controlled weapons enabled and keep the weapon out of scope mode. Press both thumbsticks and hold for one second, until the gun stops following your hand (a system tone also signals this). Keep holding and move/rotate your right controller into the grip position you want. Release either thumbstick to save, then release the other. Each weapon type keeps its own position and rotation offset in `DeusExHRVR-weapons.ini` beside the game executable, including across restarts. The gun, firing line, muzzle effects and immersive scope share that offset. Repeat to readjust; delete that file while the game is closed to reset all weapons.

The calibration gesture consumes controller movement, turning, firing and button actions until both sticks are released. Left-stick crouch has a 150 ms grace period to allow the two clicks to arrive together; a quick tap still crouches. Tracking loss, menus, recentering or weapon changes cancel an unfinished calibration. Install the matching game DLL and host together: this build uses transport version 5.

`ImmersiveScope=1` optionally keeps native scoped weapons in full VR. With motion-controlled weapons enabled, raise the right controller near your eye and point forward to enter the scope; lower it to exit. The magnified view follows controller aim, using the rifle's firing line as a shared optical viewpoint for both eyes. `ScopeMagnification=4` controls zoom (1-12). This alignment has been tested in-headset with the sniper rifle. The default remains the automatic virtual screen (`ImmersiveScope=0`). Restart after changing these settings.

`ExperimentalMotionControls=1` enables a right-controller weapon experiment when `MotionControls=1`. It moves the equipped gun's render pose and supplies the controller muzzle to the player's firing-direction calculation. Motion-controller buttons use the Xbox bindings below; keyboard/mouse and physical gamepad input also remain available. `ControllerHideArms=1` (default) hides the player's actor/arms mesh during controller aiming. It falls back to normal weapon handling in virtual-screen modes or when controller tracking is unavailable. `ControllerMuzzleForwardMetres` sets the muzzle distance ahead of the controller (default `0.25`). Restart after changing these options. This remains experimental; alignment, shot impacts and different weapon models need in-game verification.

Motion-controller buttons emulate Xbox controller 1 when `MotionControls=1`; no virtual-controller driver is required. `ExperimentalMotionControls` controls the weapon pose separately. The mapping uses the game's default Xbox layout with Y and B exchanged:

| Motion controller | Xbox input / default game action |
| --- | --- |
| Left stick / click | Move / crouch |
| Right stick / quick click | Camera turn / iron sight or scope |
| Left trigger | LT / take cover |
| Right trigger | RT / fire |
| Left grip | LB / sprint |
| Right grip | RB / throw grenade |
| Left X | X / interact or reload |
| Left Y | B / non-lethal takedown; hold for lethal takedown |
| Right A | A / jump |
| Right B | Y / holster or draw; hold for quick inventory |
| Hold right-stick click + left stick | D-pad: up cloaking, down smart vision, left move silently, right Typhoon |
| Left menu: release before 1.5 seconds | Back / in-game menu |
| Hold right stick click + left X | Back / in-game menu |
| Hold right stick click + left Y | Start / pause menu |
| Left menu: hold at least 1.5 seconds | Start / pause menu, once per hold |

While right-stick click is held, the left stick sends only D-pad input. Diagonals choose the dominant direction. A quick right-stick click under 350ms still toggles scope on release if no D-pad direction was used; a longer hold sends no scope click. Menu short presses are delayed until release so a long press opens only pause. Tracking/focus loss releases the emulated inputs. `MotionControls=0` disables button emulation together with all other controller features; restart to apply. Native rumble is not yet mapped to VR haptics. F8 includes a private input diagnostic log.

For an intermittent visual problem, press F10 before waiting for it, then F8 immediately after it appears. The recorder retains 360 reduced-size stereo pairs (about four seconds at 90 Hz), together with their tracking and submission data. It uses about 106 MB while armed; saving with F8 can briefly pause playback. Images and camera-history CSV files stay in the local `DeusExHRVR-captures` folder. F10 turns recording off again.

The HUD uses a shared plane projected through the recorded eye poses. Alignment of the health bar, minimap, and item bar has been confirmed in-headset.

Shared lighting-depth reconstruction uses the same eye frusta as world geometry. This fixes the tested hanging yellow lights; other light and shadow effects still have known stereo issues. F7 provides a live comparison while those remaining effects are investigated.

World scale is provisional. Copy `DeusExHRVR.ini.example` to `DeusExHRVR.ini` beside `DXHRDC.exe` to adjust it. Keep the `[VR]` section header; a setting outside that section is ignored. Restart the game after changing it:

```ini
[VR]
WorldUnitsPerMetre=100
```

The accepted range is 10–1000. Higher values make the world appear smaller and increase close-range stereo depth; lower values make it appear larger and reduce depth. Physical head translation uses the same scale. To confirm that your edit was read, check `unitsPerMetre=` in the latest `Camera hooks` line in `DeusExHRVR-camera.log`.

The game's stereo separation/convergence sliders do not calibrate tracked VR: that path uses the headset eye poses and `WorldUnitsPerMetre`. The original stereo settings remain relevant to the untracked screen mode.

`LevelRecenter=1` under `[VR]` (default) keeps only the heading of the head pose captured when tracking starts and on F9. A head tilted slightly up or down at that moment no longer tilts the world for the rest of the session; position, including height, is still taken from the captured pose. Set it to `0` for the previous behavior. The effect is most visible with `LockVerticalCamera=1`, where the native look pitch no longer masks the tilt.

`LevelMenu=1` under `[VR]` (default) levels the in-game menu (map, objectives, inventory) and loading screens. The game stops updating its camera while these are up, so the view freezes at the head pose of that moment, and the screen, drawn in front of that pose, stayed tilted by however far the head was tilted. The frozen pose now keeps only its heading, the same as `LevelRecenter`: the screen comes out level, and the 3D view behind the in-game menu is kept. The in-game menu is leveled as it opens; any other frozen view is leveled once the camera has been stopped for 10 frames, so a brief stall in gameplay is left alone (in testing, every freeze outside the in-game menu lasted over 100 frames; loading screens show their first 10 frames tilted). The headset's recenter now also brings a frozen view in front of you, as it already did for the virtual screen. Set it to `0` for the previous leveling behavior.

`LevelScreen=1` under `[VR]` (default) places the virtual screen used by the title and pause menus, terminals and videos upright, 2 m straight ahead at head height, keeping only the heading of the head pose when it is placed. Set it to `0` to place it tilted with the head, as before.

`YawOnlyCamera=1` under `[VR]` takes the VR rendering base from the game camera's heading and position only, so the headset supplies all pitch and roll. It goes further than `LockVerticalCamera`, which removes look pitch but keeps the camera's own pitch and roll, including the walk animation's tilt. Aiming, the gun and virtual-screen modes are unaffected. Defaults to `0`; restart after changing it.

`StanceHold=1` under `[VR]` takes the VR camera's height from the player's own origin plus a held eye height, rather than following the game camera's vertical motion. It removes the walk animation's bounce and the stance-height steps described below.

```ini
StanceHold=1
StanceHoldTrigger=60    ; game units; gap that counts as a real stance change
```

Measured against the player entity over 9000 frames on the supported build: the camera's X and Y equal the entity's origin to 0.2 units, so there is no lateral bob in this game at all. Its height above that origin is a *stance* height, not a smooth signal - the game raises it about 15 units while crouch-walking and lowers it about 25 while sprinting, then steps back when you stop. Those steps are 3-5 cm of vertical head movement and read as a bounce; averaging can smooth such a step but can never cancel a sustained offset.

Holding the height instead makes gait offsets and walking bob invisible, while a real stance change - crouched and standing differ by about 300 units - is followed at the speed of the game's own crouch/stand transition (about 1000 units/s, measured) until the camera settles. `StanceHoldTrigger` separates the two: gait offsets are 15-25 units and walking bob is about 2. Defaults to `0`. Ladders, cover, vaulting and elevators have not been tested with it yet.

`SideSwayHoldMs` under `[VR]` does the same for sideways motion. While running, the game sways the camera side to side relative to the player's own position, about 1 cm peak to peak, once per step and once per stride, while the body itself travels straight (measured over two 46-second runs; standing, the camera sits exactly on the body, and running leans it about 3 cm forward). With `SideSwayHoldMs=667` - one running stride - the VR camera stays on the player's path at the camera's average offset over the last 667 ms, which cancels the sway without lag because that offset barely changes while moving; in the measured runs it removed about 85-90% of the stride sway and nearly all of the step sway. Offsets larger than 15 cm (camera cuts, cover and similar) pass through untouched. Defaults to `0` (off).

Optional heading-swing filtering smooths the walk animation's left-right swing of the camera heading out of the VR view, without touching the game's own camera. It needs `YawOnlyCamera=1`. Each setting is a time window in milliseconds and defaults to `0` (off):

```ini
HeadSwayYawMs=667       ; heading swing, walking
HeadSwayYawMs2=526      ; heading swing, second rhythm (sprinting)
```

The filter reports the time-average of the heading over the last window, extrapolated by half a window so steady turning is not delayed; a periodic swing whose period divides the window averages out. The correction is limited to `HeadSwayYawLimit` (1.5 degrees); a larger difference means the average no longer describes the camera, so the correction is dropped and averaging restarts. Snap turns and other heading jumps over 3 degrees in one frame pass straight through. Loads, teleports and pauses reset the filter. Only gameplay is filtered; other camera modes pass through.

`BobTrace=1` writes one line per frame to `DeusExHRVR-bob.csv` (camera position and heading, stick input, head pose; capped at 36000 lines) for measuring the walk animation on other hardware. The values above came from such a trace of walking and sprinting in the first hub. Tracing costs frame time - leave it at `0` for play.

Motion-controller buttons can be remapped in `DeusExHRVR.ini` without touching the game's own bindings. `[Buttons]` applies during gameplay and scoped aiming; `[ScreenButtons]` applies to everything else: the title and pause menus, the in-game menu (map, objectives, inventory), the e-reader and news reader, terminals, hacking, videos, game over and loading screens, where one-handed use and a different Select/Back pairing are often easier. Any input not listed keeps the stock layout above. The in-game menu and the readers stay in tracked VR (snap turn still works over a reader); only their buttons follow `[ScreenButtons]`.

```ini
[Buttons]
RightA=A
RightB=Y
LeftX=X
LeftY=B
LeftGrip=LB
RightGrip=RB
LeftStickClick=LS
RightStickClick=RS
LeftTrigger=LT
RightTrigger=RT
```

Inputs are `RightA`, `RightB`, `LeftX`, `LeftY`, `LeftGrip`, `RightGrip`, `LeftStickClick`, `RightStickClick`, `LeftTrigger`, `RightTrigger`. Targets are `A`, `B`, `X`, `Y`, `LB`, `RB`, `LS`/`L3`, `RS`/`R3`, `LT`, `RT`, `Back`, `Start`, `DPadUp`, `DPadDown`, `DPadLeft`, `DPadRight`, or `None`. Triggers may be mapped to buttons and buttons to triggers. `<Input>HoldMs=250` makes that input send its target only after it has been held that long, so accidental taps send nothing; with a hold delay set, `<Input>Tap=A` sends a different target as a 120 ms pulse when the input is released early. Unrecognized names are logged and ignored. Restart after editing.

`SnapTurn=1` under `[VR]` replaces the right stick's smooth camera turn during gameplay with a snap turn. A flick within 60 degrees of horizontal turns the view instantly by `SnapTurnDegrees` (default 30); the stick must return near center before the next flick. Menus, terminals and scoped aiming keep the native right stick.

The turn is one injected relative mouse move, which avoids the stick's acceleration ramp. `SnapTurnMouseCounts` is the size of that move; the mod measures the resulting heading change after each snap and writes a corrected value back to the INI, so it self-calibrates within a few turns from any starting value. The game appears to ignore mouse look while the gamepad left stick is deflected, so walking is released for up to `SnapTurnPauseMs` (default 60) around the injected move and resumed as soon as the camera has visibly turned.

`RightStickUp` and `RightStickDown` under `[Buttons]` can hold a button while the right stick is pushed within 30 degrees of vertical during gameplay (for example `RightStickUp=A` to jump and `RightStickDown=LS` to crouch). This gives up vertical stick aiming, which you don't need when aiming with the controller (`ExperimentalMotionControls=1`). It works with snap turn or smooth turn. With `SnapTurn=0`, setting either key takes the stick's vertical axis for these buttons and leaves the horizontal axis for smooth turning; a push inside the 30-degree cone also holds the turn, so jumping or crouching doesn't drift the view. Menus, terminals and the scope keep the native right stick.

## Restore the original game

Close the game and run `uninstall.ps1` with the same game path:

```powershell
powershell -ExecutionPolicy Bypass -File .\uninstall.ps1 -GameDirectory "E:\SteamLibrary\steamapps\common\Deus Ex Human Revolution Director's Cut"
```

The backup restores original files and graphics settings. Logs, captures, and the backup remain local.

## How it works

The 32-bit game renders a double-height native texture: two complete headset-sized eyes stacked vertically. A D3D11 keyed mutex transfers the entire GPU pair to a 64-bit OpenXR companion on the same graphics adapter, which copies it into a two-slice OpenXR swapchain.

Tracking is attached to the engine scene and carried with the completed native pair. Projection submission requires both eyes to use the same recorded tracking sample. The companion signals the game after `xrWaitFrame`; bounded waits pace the producer and drop a whole pair on timeout. Live transport uses GPU copies; CPU readback is limited to F8 diagnostics. The original render pose remains attached to the image; further prediction and latency optimization remains future work.

## Build from source

Requires Visual Studio 2022 C++ tools, a Windows SDK, CMake 3.24+, and Git. CMake fetches pinned OpenXR and MinHook sources. Use separate developer shells and build directories.

In an **x86** Visual Studio developer shell:

```powershell
cmake -S . -B build-x86 -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build-x86 --target atidxx32 d3d11_proxy atiadlxy DeusExHRVRCameraMathProbe
```

In an **x64** Visual Studio developer shell:

```powershell
cmake -S . -B build-x64 -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build-x64 --target DeusExHRVRHost DeusExHRVRProbe
```

Stage `d3d11.dll`, `atidxx32.dll`, and `atiadlxy.dll` from `build-x86/bin` into `dist`. Stage the x64 host from `build-x64/bin` into `dist/DeusExHRVR`. The installer requires this layout.

Offline builds can set `FETCHCONTENT_SOURCE_DIR_OPENXR` and `FETCHCONTENT_SOURCE_DIR_MINHOOK` to matching dependency checkouts; exact revisions are in CMakeLists.txt.

Validation tools:

- `DeusExHRVRCameraMathProbe`: camera-cache lifetime, tracking mailbox contention, pose conversion, inverse matrices, asymmetric eye frusta, and binocular HUD alignment. Pass a camera-history CSV path to replay scene creation/drawing through the cache.
- `DeusExHRVRProbe`: D3D11 pair bounds and GPU eye-array copies. Optional `--xr` renders red/green diagnostics; use x64 with the tested runtime.
- `DeusExHRVRTransportProbe`: optional x86 target exercising the complete GPU transport. Place the x64 companion in its `DeusExHRVR` subfolder.

Component probes do not replace headset gameplay testing. Logs and F8 images are written to the game folder. Review them before sharing; they are excluded from this repository and release packages.

## Credits and license

The stereo and adapter proxies derive from [effcol/wiz3D](https://github.com/effcol/wiz3D). Thanks also to the [cdcEngineDXHR](https://github.com/rrika/cdcEngineDXHR) reverse-engineering reference, Khronos OpenXR, and MinHook.

Distributed under LGPL 2.1. See [LICENSE](LICENSE), [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), and `licenses/`. No game executable or game content is included.
