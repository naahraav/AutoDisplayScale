# Auto Display Scale

![Platform](https://img.shields.io/badge/platform-Windows%2010%20%7C%2011-blue)
![Language](https://img.shields.io/badge/language-C%2B%2B%20(Win32%20API)-orange)
![License](https://img.shields.io/badge/license-MIT-green)

A lightweight **C++ (Win32 API)** utility that automatically changes your laptop's internal display DPI scale whenever an external monitor is connected or disconnected on Windows 10 and Windows 11.

No more signing out or manually adjusting display scaling every time you plug in or unplug an HDMI, USB-C, or DisplayPort cable.

## Features

- **Event-Driven Detection (`WM_DISPLAYCHANGE`)**: Listens to native Windows display change events instead of busy polling, keeping idle CPU usage near 0%.
- **Real-Time DPI Scaling**: Applies the new laptop display scale immediately without requiring a sign-out or restart.
- **Lightweight GUI & System Tray**: Native Win32 interface that minimizes cleanly to the Windows System Tray with a quick-action context menu.
- **Hardware-Aware Scale Range**: Automatically populates valid scale percentages based on your laptop panel's minimum, maximum, and recommended DPI values.
- **Run at Windows Startup**: Optional startup toggle that registers the app in the Windows Registry with `--tray` so it launches silently in the background.
- **Single-Instance Protection**: Prevents launching duplicate instances; opening the app again brings the existing window to the front.
- **Zero External Dependencies**: Built purely with standard Windows libraries (`user32`, `gdi32`, `shell32`, `comctl32`, `advapi32`).

## Requirements

- **OS**: Windows 10 or Windows 11
- **Compiler** (to build from source):
  - MinGW-w64 / MSYS2 (`g++`)
  - Microsoft Visual C++ (`cl.exe` / Visual Studio)

## Build Instructions

### Using MinGW-w64 / MSYS2 (`g++`)
```bash
g++ AutoDisplayScaleGUI.cpp -o AutoDisplayScaleGUI.exe -O2 -mwindows -luser32 -lgdi32 -lshell32 -lcomctl32 -ladvapi32
Using MSVC (Developer Command Prompt)
Cmd
cl /EHsc /O2 AutoDisplayScaleGUI.cpp
Usage
Launch AutoDisplayScaleGUI.exe.
Select Scale without monitor (Laptop only) (e.g., 150%).
Select Scale with external monitor (e.g., 100% or 125%).
(Optional) Check Run automatically in tray at Windows startup.
Click Apply & Save — settings are saved in AutoDisplayScale.ini next to the executable.
Click Minimize to Tray (or close with X) to let the app monitor display changes in the background.
System Tray Controls
Double-click the tray icon to reopen the settings window.
Right-click the tray icon for quick actions (Open Settings, Apply Scale Now, Auto-Scale Enabled, Exit).
Command-Line Flags
--tray : Launches the application minimized in the System Tray (used automatically when Startup is enabled).
How It Works
The application queries active display paths via QueryDisplayConfig (QDC_ONLY_ACTIVE_PATHS) to distinguish the internal laptop panel (INTERNAL, DISPLAYPORT_EMBEDDED, UDI_EMBEDDED, LVDS) from external monitors. It then reads and updates the source DPI scaling via DisplayConfigGetDeviceInfo and DisplayConfigSetDeviceInfo using Windows' internal DPI scale device info packets (-3 and -4).
License
This project is licensed under the MIT License.