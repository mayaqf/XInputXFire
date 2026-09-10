# XInputXFire Proxy DLL

English | [日本語 (Japanese)](README.ja.md)

**Turbo fire (rapid-fire) for Xbox controllers (XInput) on Windows, as a drop-in proxy DLL.** No resident background app, no driver install. Modeled after XInput Plus's proxy-DLL approach, built from scratch. Two rapid-fire modes:

- **Trigger mode (`[XFire]`)**: While **RT or LT is held**, the D-pad and A/B/X/Y buttons rapid-fire.
- **Button-only mode (`[RapidFire]`)**: No trigger needed — **holding a target button** rapid-fires it (e.g. auto-mashing the confirm button).

> **Download**: prebuilt DLLs (x86/x64) and a sample `XInputXFire.ini` are on the [Releases](https://github.com/mayaqf/XInputXFire/releases) page — no need to build from source.

## How it works (proxy DLL)

Place the proxy DLL (`xinput1_3.dll`) and the config (`XInputXFire.ini`) in the same folder as the target game's exe. Windows' DLL search order loads the local DLL first, so XInput API calls are intercepted. The proxy forwards everything to the genuine system DLL (loaded dynamically), while rewriting the D-pad/A/B/X/Y bits of the `XInputGetState` result with a periodic on/off pattern before returning them to the game. **No resident process required.**

```
game.exe ─(implicit DLL resolution)─▶ local xinput1_3.dll [proxy]
                              │ lazy init on first call (load genuine DLL + read ini)
                              ▼
        genuine System32\xinput1_3.dll (or 1_4) → raw state
                              ▼
    rapid-fire engine: LT/RT check → QPC clock → rewrite DPAD/ABXY
                              ▼ (trigger values untouched)
                  modified state returned to the game
```

## Features (rapid-fire logic)

- **Master toggle (LB+A)**: rapid fire is toggled by pressing **LB+A together (PS: L1+×)**; it switches on the press edge. **Starts OFF** (prevents accidental firing while navigating menus). While OFF, LT/RT inputs pass through untouched.
- **Activation**: LT or RT at/above the press threshold (default 150/255). Either trigger activates. Only while the master is ON.
- **Targets**: D-pad (all four) + A/B/X/Y (individually selectable via ini).
- **Timing**: QPC high-resolution timer. ON window (default 50 ms) = button pressed, OFF window (default 50 ms) = button released. Independent of the game's polling rate (60/120 Hz, etc.).
- **First ON window (`FirstOnMs`)**: only the first ON window right after a target button is pressed can use its own length (default 200 ms; 0 = disabled = same as `OnMs`). The phase clock is referenced to the target button press and resets when the button is released, so the next press starts from `FirstOnMs` again. Making `FirstOnMs` large lets you land a **single press** even while turbo is enabled (release during the long first ON window).
- **Hysteresis**: press threshold 150 / release threshold 140, so trigger chatter near the threshold can't send the turbo into a runaway.
- **4 controllers, independent**: `dwUserIndex` 0–3 each keep their own state.
- **Trigger passthrough**: LT/RT analog values reach the game untouched (turbo only affects D-pad/ABXY).
- **Only physically-held buttons fire**: turbo applies only to target buttons you are physically holding. Release to stop.

### Button-only rapid fire mode (`[RapidFire]`, v1.1.0)

A second mode that rapid-fires while a target button is **held down**, without touching the triggers. Active only while the XFire master is enabled (the master toggle is shared with trigger mode). Intended for auto-mashing the confirm button (A), etc.

- **Activation**: while the XFire master is ON and a button listed in `[RapidFire]` → `TargetButtons` is physically held.
- **Timing**: an independent second phase clock (`[RapidFire]`'s `FirstOnMs`/`OnMs`/`OffMs`), QPC-based, polling-rate independent.
- **Overlap resolution (trigger takes priority; others independent)**: a button present in both modes' `TargetButtons` is driven by **trigger mode while a trigger is held**, and by **button-only mode when no trigger is held**.
  - Buttons only in the trigger list → fire only while a trigger is held.
  - Buttons only in the button-only list → fire regardless of trigger state.
- **Master OFF resets both phase clocks**: when the master toggle goes ON→OFF, both modes' phase clocks reset; after OFF→ON, the next press starts from `FirstOnMs`.

> **Button names**: this tool is built on the XInput (Xbox) API, so Xbox names are the primary notation. Equivalent names on a PlayStation controller:
>
> | Xbox | PS | XInput API |
> |---|---|---|
> | LT / RT | L2 / R2 | `bLeftTrigger` / `bRightTrigger` |
> | LB / RB | L1 / R1 | `XINPUT_GAMEPAD_LEFT_SHOULDER` / `XINPUT_GAMEPAD_RIGHT_SHOULDER` |
> | A / B / X / Y | × / ○ / □ / △ | `XINPUT_GAMEPAD_A/B/X/Y` |

## Configuration file (XInputXFire.ini)

Place it next to the game exe. If missing, defaults are used.

```ini
; XInputXFire v1.1.0
[XFire]
OnMs=50
OffMs=50
; First ON window right after a press (ms; 0 = disabled = same as OnMs; default 200).
; Set it large to allow a single press even while turbo is enabled.
FirstOnMs=200
TriggerThreshold=150
HysteresisLow=140
TargetButtons=DPAD_UP|DPAD_DOWN|DPAD_LEFT|DPAD_RIGHT|A|B|X|Y
EnableLT=1
EnableRT=1
; Master toggle combo (| separated; default LB|A = PS L1+A). Toggles on the press of all keys together. Empty = disabled (always-on).
ToggleButtons=LB|A
; Master state at startup (1 = ON / 0 = OFF; default 0 = OFF). While OFF, inputs pass through untouched (menu-safe).
DefaultEnabled=0
; Voice announcement on toggle (1 = on / 0 = off; default 1). Plays "Enabled/Disabled cross fire." via SAPI.
AnnounceEnabled=1
; Startup sound (1 = play / 0 = mute; default 1). Plays a WAVE resource embedded in the DLL to confirm the proxy loaded.
StartupSound=1

; Button-only rapid fire (no trigger). Separate settings from [XFire] (independent 2nd phase clock). Default: A (confirm mash).
; A button in both lists is "trigger-first" (trigger held = [XFire] drives it / not held = the section below drives it).
[RapidFire]
; First ON window right after a press (ms; 0 = disabled = same as OnMs; default 500). Separate from [XFire]'s FirstOnMs.
FirstOnMs=500
OnMs=50
OffMs=50
; Target buttons (| separated). Same names as [XFire] TargetButtons.
TargetButtons=A
```

Valid names for `TargetButtons` / `ToggleButtons`: `DPAD_UP` `DPAD_DOWN` `DPAD_LEFT` `DPAD_RIGHT` `A` `B` `X` `Y` `LB` `RB` `START` `BACK` `LSB` `RSB` (`|` separated)

## Building the proxy DLL (for developers)

Requires Visual Studio Build Tools 2022 (C++ workload). The bundled CMake/MSBuild are **not on PATH** in ordinary shells (cmd / PowerShell / Git Bash, etc.), so a bare `cmake` won't be found. Launch a **"Developer Command Prompt for VS 2022"** and run from there (that puts the bundled CMake/MSBuild on PATH).

```bash
# 32-bit
cmake -S . -B build-x86 -A Win32
cmake --build build-x86 --config Release
# 64-bit
cmake -S . -B build-x64 -A x64
cmake --build build-x64 --config Release
# Unit tests (no controller needed)
cmake --build build-x64 --config Release --target xfire_unit
ctest --test-dir build-x64 --output-on-failure
```

Artifacts:
- `build-x86/Release/xinput1_3_x86.dll`, `xinput1_3.dll` (a renamed copy of the x86 DLL)
- `build-x64/Release/xinput1_3_x64.dll`, `xinput1_3.dll` (a renamed copy of the x64 DLL)
- `xfire_unit.exe`, `test_harness.exe`

## Installation (manual)

1. Copy the proxy DLL matching the game's bitness (32/64) next to the game exe, renamed to the XInput DLL name the game imports:
   - 1.3-style games: place `xinput1_3.dll` **as-is**.
   - 9.1.0-style: rename `XInput9_1_0_x64.dll` / `XInput9_1_0_x86.dll` to `XInput9_1_0.dll`.
   - 1.4-style: rename `xinput1_3_x64.dll` / `xinput1_3_x86.dll` to `xinput1_4.dll`.
   - **Place the proxy DLL only in the game exe's folder. Never put it in `C:\Windows\System32` / `SysWOW64`** (placing the proxy in the system folder under the genuine DLL's own name can cause infinite recursion via self-reload).
2. Put `XInputXFire.ini` in the same folder.
3. Launch the game normally (no resident app needed).

## Verification

1. **Unit tests** (`xfire_unit`): verifies the rapid-fire logic with mock QPC time injection (ON/OFF periods, hysteresis, trigger passthrough, non-target protection, 4-controller independence, button-only mode, 2-mode overlap resolution, phase reset on master toggle). No controller needed.
2. **Test harness** (`test_harness`): place the proxy DLL in its own folder as `xinput1_3.dll` and run it; a CSV log confirms whether target buttons toggle periodically while RT is physically held (60 s).
   - **On systems with Smart App Control (SAC) enabled, test_harness.exe may fail to launch.** SAC blocks new unsigned binaries with no reputation in Microsoft's cloud as "unverified" (this is not path-based, so copying to Program Files doesn't help, and there is no user-side exception). Check event log `Microsoft-Windows-CodeIntegrity/Operational`, IDs 3118/3033, for "Smart App Control Block".
   - The **production proxy DLL is not blocked under SAC** (a DLL loaded via LoadLibrary by a signed game exe is allowed). SAC only catches standalone unsigned exes like test_harness. If test_harness won't launch, verify on another PC/VM with SAC off, or test through a real game.
3. **Integration**: place it in a permitted offline game and verify live behavior.

## Game compatibility

This tool only works with games that read controller input through **XInput's `XInputGetState`**. Most XInput-compatible games qualify.

**Unsupported cases (games that don't read input via XInput):**

- Games that read controllers directly from HID (`SetupDi*` + `CreateFileW` + `ReadFile` + `HidP_*`) or via RawInput never call `XInputGetState`, so the proxy DLL loads but cannot turbo-fire.
- For such games, use a tool that actively inline-hooks the HID path, such as **XInput Plus** (a proxy DLL plus Mhook-based hooking).
- If you're unsure whether a game uses XInput: if the controller goes unresponsive after installing the proxy, the game is likely unsupported.

**A certain "14" MMO (dx11 build):** it imports `XINPUT1_3.dll` **by ordinals (2, 3, 5)** through its own XInput wrapper (`XInputXIV3.dll`) to call `XInputGetState`, so this tool's XInput path works (verified on real hardware). Deployment is a **single** `xinput1_3.dll` (do not also place an `XInput9_1_0.dll` proxy — two proxies loaded into one process crash at startup). Ordinal imports are supported by explicitly pinned ordinals in `.def` (`@2` = `GetState`, etc.).

## Troubleshooting (diagnostic log)

If the controller is unresponsive or turbo fire doesn't kick in, the proxy DLL writes its startup diagnostics (at the first export call) to **`%TEMP%\XInputXFire_xinput.log`**, one line at a time.

- `[STICKYINIT] version=<build version> LoadOnce=1 hDll=... GetState=...` → the proxy loaded normally and obtained function pointers into the genuine DLL (`hDll=0000000000000000` means the genuine DLL failed to load).
- `[LOADER] ...` → genuine DLL load failure, missing required exports, etc. (shows fallback DLL switching).
- `[CONFIG] ...` → an ini value was non-numeric / out of range / an unknown token and was replaced with the default; or a removed key (`EnableL2`/`EnableR2`) was detected (useful for finding unintended behavior).
- `[XFIRE] QueryPerformanceFrequency returned 0 ...` → high-resolution timer unavailable (rapid fire disabled; passthrough only).

**The log does not grow while you play.** Diagnostic lines are written only at startup; the per-frame `XInputGetState` / rapid-fire hot path writes no log lines (no bloat on long sessions). If the log contains just one `[STICKYINIT]` line and nothing else, the proxy itself is working fine — an unresponsive controller then most likely means the game doesn't read input via XInput (unsupported) → see [Game compatibility](#game-compatibility).

## ⚠️ Disclaimer (use at your own risk)

- Like XInput Plus / x360ce, the proxy-DLL approach **can be detected by anti-cheat**. **Using it in online games is at your own risk.**
- A misconfigured setup can make a game misbehave. Verify with the test harness beforehand.
- This tool is intended for personal learning/research and use in permitted environments.

## Design notes

- **Do nothing in DllMain**: the genuine DLL load, ini read, and QPC init all happen in a lazy init at the first export call. `LoadLibrary` inside DllMain deadlocks on the loader lock (per Microsoft's official DLL Best Practices).
- **Self-reload prevention**: the genuine DLL is loaded by building its **full path** with `GetSystemDirectoryW` (SysWOW64 for 32-bit processes). `LOAD_LIBRARY_SEARCH_SYSTEM32` alone is not enough: the same-named module already loaded into the process (the proxy itself) gets reused by name, causing self-recursion (verified). A full path is distinguished from the proxy by its normalized path.
- **1.3→1.4 fallback**: on systems without `xinput1_3.dll` (Win8+), `xinput1_4.dll` is used.
- **No linking against xinput.lib**: `xinput.h` is not included; the structures are defined locally. Avoids link conflicts with the genuine DLL.
- **Fixed export names**: `.def` pins undecorated names (avoids the x86 stdcall decorated-name problem).
- **Statically linked CRT (`/MT`)**: minimizes DLL dependencies.

## License

This project's code is released under the **MIT License**. See [LICENSE](LICENSE) for details.