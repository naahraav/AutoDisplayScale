#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <cstdint>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

// ==========================================
// Constants & UI Control IDs
// ==========================================
constexpr wchar_t kWindowClassName[] = L"AutoDisplayScaleGUIClass";
constexpr wchar_t kAppTitle[]        = L"Auto Display Scale";
constexpr wchar_t kStartupRegPath[]  = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kStartupRegName[]  = L"AutoDisplayScale";

constexpr UINT WM_TRAYICON       = WM_APP + 1;
constexpr UINT_PTR TIMER_DISPLAY = 1;
constexpr UINT ID_TRAY_ICON      = 1001;

enum ControlId {
    IDC_LBL_MONITOR_STATUS = 101,
    IDC_LBL_CURRENT_SCALE  = 102,
    IDC_LBL_LAST_ACTION    = 103,
    IDC_COMBO_NO_MON       = 104,
    IDC_COMBO_WITH_MON     = 105,
    IDC_CHK_ENABLE         = 106,
    IDC_CHK_STARTUP        = 107,
    IDC_CHK_CLOSE_TRAY     = 108,
    IDC_BTN_APPLY          = 109,
    IDC_BTN_TRAY           = 110,
    IDC_BTN_EXIT           = 111,

    // Tray Menu IDs
    IDM_TRAY_OPEN          = 201,
    IDM_TRAY_APPLY_NOW     = 202,
    IDM_TRAY_TOGGLE        = 203,
    IDM_TRAY_EXIT          = 204
};

// ==========================================
// Windows Internal DPI Scaling Structures
// ==========================================
static const UINT32 kDpiVals[] = {
    100, 125, 150, 175, 200, 225, 250, 300, 350, 400, 450, 500
};
static const int kDpiCount = sizeof(kDpiVals) / sizeof(kDpiVals[0]);

enum class DISPLAYCONFIG_DEVICE_INFO_TYPE_CUSTOM : int32_t {
    GET_DPI_SCALE = -3,
    SET_DPI_SCALE = -4
};

struct DISPLAYCONFIG_SOURCE_DPI_SCALE_GET {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    int32_t minScaleRel;
    int32_t curScaleRel;
    int32_t maxScaleRel;
};

struct DISPLAYCONFIG_SOURCE_DPI_SCALE_SET {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    int32_t scaleRel;
};

static_assert(sizeof(DISPLAYCONFIG_SOURCE_DPI_SCALE_GET) == 32, "GET struct size must be 32 bytes");
static_assert(sizeof(DISPLAYCONFIG_SOURCE_DPI_SCALE_SET) == 24, "SET struct size must be 24 bytes");

struct DisplayStatusInfo {
    bool hasInternal          = false;
    bool hasExternal          = false;
    LUID internalAdapterId    = {};
    UINT32 internalSourceId   = 0;
    UINT32 currentScale       = 100;
    UINT32 recommendedScale   = 100;
    int minIdx                = 0;
    int maxIdx                = 4; // Fallback: 100% - 200%
};

struct AppConfig {
    UINT32 scaleWithoutMonitor = 150;
    UINT32 scaleWithMonitor    = 100;
    bool autoEnabled           = true;
    bool closeToTray           = true;
};

// Global State
AppConfig g_config;
HWND g_hwndMain       = nullptr;
HFONT g_hFont         = nullptr;
NOTIFYICONDATAW g_nid = {};
bool g_trayAdded      = false;
std::wstring g_exePath;
std::wstring g_iniPath;
std::wstring g_lastActionText = L"Ready and monitoring display changes.";

// ==========================================
// Path, INI & Startup Registry Helpers
// ==========================================
void InitPaths() {
    wchar_t buffer[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    g_exePath = buffer;

    size_t dotPos = g_exePath.find_last_of(L'.');
    if (dotPos != std::wstring::npos) {
        g_iniPath = g_exePath.substr(0, dotPos) + L".ini";
    } else {
        g_iniPath = g_exePath + L".ini";
    }
}

void LoadConfig() {
    g_config.scaleWithoutMonitor = GetPrivateProfileIntW(L"Settings", L"ScaleWithoutMonitor", 150, g_iniPath.c_str());
    g_config.scaleWithMonitor    = GetPrivateProfileIntW(L"Settings", L"ScaleWithMonitor", 100, g_iniPath.c_str());
    g_config.autoEnabled         = GetPrivateProfileIntW(L"Settings", L"AutoEnabled", 1, g_iniPath.c_str()) != 0;
    g_config.closeToTray         = GetPrivateProfileIntW(L"Settings", L"CloseToTray", 1, g_iniPath.c_str()) != 0;
}

void SaveConfig() {
    WritePrivateProfileStringW(L"Settings", L"ScaleWithoutMonitor", std::to_wstring(g_config.scaleWithoutMonitor).c_str(), g_iniPath.c_str());
    WritePrivateProfileStringW(L"Settings", L"ScaleWithMonitor", std::to_wstring(g_config.scaleWithMonitor).c_str(), g_iniPath.c_str());
    WritePrivateProfileStringW(L"Settings", L"AutoEnabled", g_config.autoEnabled ? L"1" : L"0", g_iniPath.c_str());
    WritePrivateProfileStringW(L"Settings", L"CloseToTray", g_config.closeToTray ? L"1" : L"0", g_iniPath.c_str());
}

bool IsStartupEnabled() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kStartupRegPath, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0;
    DWORD size = 0;
    LONG res = RegQueryValueExW(hKey, kStartupRegName, nullptr, &type, nullptr, &size);
    RegCloseKey(hKey);
    return (res == ERROR_SUCCESS && type == REG_SZ);
}

void SetStartupEnabled(bool enable) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kStartupRegPath, 0, KEY_WRITE, &hKey) != ERROR_SUCCESS) {
        return;
    }
    if (enable) {
        std::wstring cmd = L"\"" + g_exePath + L"\" --tray";
        DWORD byteSize = static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t));
        RegSetValueExW(hKey, kStartupRegName, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()), byteSize);
    } else {
        RegDeleteValueW(hKey, kStartupRegName);
    }
    RegCloseKey(hKey);
}

// ==========================================
// Display Detection & DPI Scaling Logic
// ==========================================
bool IsInternalDisplay(DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY tech) {
    switch (tech) {
        case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL:
        case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED:
        case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED:
        case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_LVDS:
            return true;
        default:
            return false;
    }
}

int FindDpiIndex(UINT32 percent) {
    for (int i = 0; i < kDpiCount; ++i) {
        if (kDpiVals[i] == percent) return i;
    }
    return -1;
}

DisplayStatusInfo QueryCurrentDisplayStatus() {
    DisplayStatusInfo info;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    LONG status = ERROR_SUCCESS;

    do {
        UINT32 pathCount = 0;
        UINT32 modeCount = 0;
        status = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount);
        if (status != ERROR_SUCCESS) return info;

        paths.resize(pathCount);
        modes.resize(modeCount);
        status = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(),
                                    &modeCount, modes.data(), nullptr);
        if (status == ERROR_SUCCESS) paths.resize(pathCount);
    } while (status == ERROR_INSUFFICIENT_BUFFER);

    if (status != ERROR_SUCCESS) return info;

    for (const auto& path : paths) {
        if (IsInternalDisplay(path.targetInfo.outputTechnology)) {
            info.hasInternal = true;
            info.internalAdapterId = path.sourceInfo.adapterId;
            info.internalSourceId = path.sourceInfo.id;
        } else {
            info.hasExternal = true;
        }
    }

    if (info.hasInternal) {
        DISPLAYCONFIG_SOURCE_DPI_SCALE_GET getPacket = {};
        getPacket.header.type = static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(
            DISPLAYCONFIG_DEVICE_INFO_TYPE_CUSTOM::GET_DPI_SCALE);
        getPacket.header.size = sizeof(getPacket);
        getPacket.header.adapterId = info.internalAdapterId;
        getPacket.header.id = info.internalSourceId;

        if (DisplayConfigGetDeviceInfo(&getPacket.header) == ERROR_SUCCESS) {
            int recIdx = -getPacket.minScaleRel;
            if (recIdx >= 0 && recIdx < kDpiCount) {
                int curIdx = recIdx + getPacket.curScaleRel;
                int minIdx = recIdx + getPacket.minScaleRel;
                int maxIdx = recIdx + getPacket.maxScaleRel;

                if (minIdx < 0) minIdx = 0;
                if (maxIdx >= kDpiCount) maxIdx = kDpiCount - 1;
                if (curIdx < minIdx) curIdx = minIdx;
                if (curIdx > maxIdx) curIdx = maxIdx;

                info.minIdx = minIdx;
                info.maxIdx = maxIdx;
                info.recommendedScale = kDpiVals[recIdx];
                info.currentScale = kDpiVals[curIdx];
            }
        }
    }
    return info;
}

void ApplyAutoScale(bool forceLog = false) {
    DisplayStatusInfo info = QueryCurrentDisplayStatus();
    if (!info.hasInternal) {
        g_lastActionText = L"Laptop display is inactive (Second screen only).";
        return;
    }

    if (!g_config.autoEnabled && !forceLog) {
        g_lastActionText = L"Auto-scale is currently paused.";
        return;
    }

    UINT32 targetScale = info.hasExternal ? g_config.scaleWithMonitor : g_config.scaleWithoutMonitor;
    int targetIdx = FindDpiIndex(targetScale);
    if (targetIdx == -1 || targetIdx < info.minIdx || targetIdx > info.maxIdx) {
        g_lastActionText = L"Target scale (" + std::to_wstring(targetScale) + L"%) is unsupported by this panel.";
        return;
    }

    if (info.currentScale == targetScale) {
        if (forceLog) {
            g_lastActionText = L"Scale already matches target (" + std::to_wstring(targetScale) + L"%).";
        }
        return;
    }

    int recIdx = FindDpiIndex(info.recommendedScale);
    if (recIdx == -1) return;

    DISPLAYCONFIG_SOURCE_DPI_SCALE_SET setPacket = {};
    setPacket.header.type = static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(
        DISPLAYCONFIG_DEVICE_INFO_TYPE_CUSTOM::SET_DPI_SCALE);
    setPacket.header.size = sizeof(setPacket);
    setPacket.header.adapterId = info.internalAdapterId;
    setPacket.header.id = info.internalSourceId;
    setPacket.scaleRel = targetIdx - recIdx;

    LONG res = DisplayConfigSetDeviceInfo(&setPacket.header);
    if (res == ERROR_SUCCESS) {
        g_lastActionText = L"Scale changed automatically to " + std::to_wstring(targetScale) + L"%.";
    } else {
        g_lastActionText = L"Failed to change scale (Error " + std::to_wstring(res) + L").";
    }
}

// ==========================================
// UI & System Tray Helpers
// ==========================================
void PopulateComboScale(HWND hCombo, int minIdx, int maxIdx, UINT32 recScale, UINT32 selectedVal) {
    SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);
    int selectPos = 0;

    for (int i = minIdx; i <= maxIdx; ++i) {
        UINT32 val = kDpiVals[i];
        std::wstring text = std::to_wstring(val) + L"%";
        if (val == recScale) {
            text += L" (Recommended)";
        }
        int idx = static_cast<int>(SendMessageW(hCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str())));
        SendMessageW(hCombo, CB_SETITEMDATA, idx, static_cast<LPARAM>(val));
        if (val == selectedVal) {
            selectPos = idx;
        }
    }
    SendMessageW(hCombo, CB_SETCURSEL, selectPos, 0);
}

UINT32 GetSelectedComboValue(HWND hCombo, UINT32 fallback) {
    int sel = static_cast<int>(SendMessageW(hCombo, CB_GETCURSEL, 0, 0));
    if (sel == CB_ERR) return fallback;
    return static_cast<UINT32>(SendMessageW(hCombo, CB_GETITEMDATA, sel, 0));
}

void RefreshUI(HWND hwnd, bool reloadCombos = false) {
    DisplayStatusInfo info = QueryCurrentDisplayStatus();

    std::wstring monText = info.hasExternal ? L"Connected" : L"Disconnected (Laptop Only)";
    SetDlgItemTextW(hwnd, IDC_LBL_MONITOR_STATUS, monText.c_str());

    std::wstring scaleText;
    if (info.hasInternal) {
        scaleText = std::to_wstring(info.currentScale) + L"% (Recommended: " + std::to_wstring(info.recommendedScale) + L"%)";
    } else {
        scaleText = L"Laptop Display Inactive";
    }
    SetDlgItemTextW(hwnd, IDC_LBL_CURRENT_SCALE, scaleText.c_str());

    SetDlgItemTextW(hwnd, IDC_LBL_LAST_ACTION, g_lastActionText.c_str());

    if (reloadCombos) {
        HWND hComboNoMon   = GetDlgItem(hwnd, IDC_COMBO_NO_MON);
        HWND hComboWithMon = GetDlgItem(hwnd, IDC_COMBO_WITH_MON);
        PopulateComboScale(hComboNoMon, info.minIdx, info.maxIdx, info.recommendedScale, g_config.scaleWithoutMonitor);
        PopulateComboScale(hComboWithMon, info.minIdx, info.maxIdx, info.recommendedScale, g_config.scaleWithMonitor);
    }

    SendDlgItemMessageW(hwnd, IDC_CHK_ENABLE, BM_SETCHECK, g_config.autoEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendDlgItemMessageW(hwnd, IDC_CHK_STARTUP, BM_SETCHECK, IsStartupEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);
    SendDlgItemMessageW(hwnd, IDC_CHK_CLOSE_TRAY, BM_SETCHECK, g_config.closeToTray ? BST_CHECKED : BST_UNCHECKED, 0);
}

void AddTrayIcon(HWND hwnd) {
    if (g_trayAdded) return;
    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = hwnd;
    g_nid.uID = ID_TRAY_ICON;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    HICON hTrayIcon = static_cast<HICON>(LoadImage(
        GetModuleHandle(nullptr), MAKEINTRESOURCE(101), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    g_nid.hIcon = hTrayIcon ? hTrayIcon : LoadIcon(nullptr, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"Auto Display Scale");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    g_trayAdded = true;
}

void RemoveTrayIcon() {
    if (!g_trayAdded) return;
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    g_trayAdded = false;
}

void ShowTrayContextMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_OPEN, L"Open Settings");
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_APPLY_NOW, L"Apply Scale Now");
    AppendMenuW(hMenu, MF_STRING | (g_config.autoEnabled ? MF_CHECKED : MF_UNCHECKED),
                IDM_TRAY_TOGGLE, L"Auto-Scale Enabled");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_EXIT, L"Exit");

    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
}

void CreateControls(HWND hwnd) {
    HINSTANCE hInst = GetModuleHandleW(nullptr);

    g_hFont = CreateFontW(
        -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );

    auto makeCtrl = [&](const wchar_t* cls, const wchar_t* text, DWORD style,
                        int x, int y, int w, int h, int id) -> HWND {
        HWND hCtrl = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                                     x, y, w, h, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                     hInst, nullptr);
        SendMessageW(hCtrl, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
        return hCtrl;
    };

    // Group 1: Display Status
    makeCtrl(L"BUTTON", L"Current Display Status", BS_GROUPBOX, 16, 12, 378, 86, 0);
    makeCtrl(L"STATIC", L"External Monitor :", 0, 28, 34, 110, 18, 0);
    makeCtrl(L"STATIC", L"-",                  0, 142, 34, 240, 18, IDC_LBL_MONITOR_STATUS);

    makeCtrl(L"STATIC", L"Laptop Scale     :", 0, 28, 54, 110, 18, 0);
    makeCtrl(L"STATIC", L"-",                  0, 142, 54, 240, 18, IDC_LBL_CURRENT_SCALE);

    makeCtrl(L"STATIC", L"Status           :", 0, 28, 74, 110, 18, 0);
    makeCtrl(L"STATIC", L"-",                  0, 142, 74, 240, 18, IDC_LBL_LAST_ACTION);

    // Group 2: Auto-Scale Settings
    makeCtrl(L"BUTTON", L"Auto-Scale Settings", BS_GROUPBOX, 16, 108, 378, 178, 0);
    makeCtrl(L"STATIC", L"Scale without monitor (Laptop only):", 0, 28, 134, 205, 20, 0);
    makeCtrl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 240, 130, 140, 220, IDC_COMBO_NO_MON);

    makeCtrl(L"STATIC", L"Scale with external monitor:", 0, 28, 168, 205, 20, 0);
    makeCtrl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 240, 164, 140, 220, IDC_COMBO_WITH_MON);

    makeCtrl(L"BUTTON", L"Enable automatic display scaling", BS_AUTOCHECKBOX | WS_TABSTOP, 28, 202, 350, 22, IDC_CHK_ENABLE);
    makeCtrl(L"BUTTON", L"Run automatically in tray at Windows startup", BS_AUTOCHECKBOX | WS_TABSTOP, 28, 228, 350, 22, IDC_CHK_STARTUP);
    makeCtrl(L"BUTTON", L"Minimize to System Tray when closing (X)", BS_AUTOCHECKBOX | WS_TABSTOP, 28, 254, 350, 22, IDC_CHK_CLOSE_TRAY);

    // Action Buttons
    makeCtrl(L"BUTTON", L"Apply && Save", BS_DEFPUSHBUTTON | WS_TABSTOP, 16, 300, 135, 32, IDC_BTN_APPLY);
    makeCtrl(L"BUTTON", L"Minimize to Tray", BS_PUSHBUTTON | WS_TABSTOP, 160, 300, 135, 32, IDC_BTN_TRAY);
    makeCtrl(L"BUTTON", L"Exit", BS_PUSHBUTTON | WS_TABSTOP, 304, 300, 90, 32, IDC_BTN_EXIT);
}

// ==========================================
// Window Procedure
// ==========================================
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            CreateControls(hwnd);
            AddTrayIcon(hwnd);
            ApplyAutoScale(false);
            RefreshUI(hwnd, true);
            return 0;

        case WM_DISPLAYCHANGE:
            SetTimer(hwnd, TIMER_DISPLAY, 800, nullptr);
            return 0;

        case WM_TIMER:
            if (wParam == TIMER_DISPLAY) {
                KillTimer(hwnd, TIMER_DISPLAY);
                ApplyAutoScale(false);
                RefreshUI(hwnd, true);
            }
            return 0;

        case WM_TRAYICON:
            if (lParam == WM_LBUTTONDBLCLK) {
                ShowWindow(hwnd, SW_RESTORE);
                ShowWindow(hwnd, SW_SHOW);
                SetForegroundWindow(hwnd);
                RefreshUI(hwnd, true);
            } else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
                ShowTrayContextMenu(hwnd);
            }
            return 0;

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            switch (id) {
                case IDC_BTN_APPLY: {
                    HWND hComboNoMon   = GetDlgItem(hwnd, IDC_COMBO_NO_MON);
                    HWND hComboWithMon = GetDlgItem(hwnd, IDC_COMBO_WITH_MON);

                    g_config.scaleWithoutMonitor = GetSelectedComboValue(hComboNoMon, g_config.scaleWithoutMonitor);
                    g_config.scaleWithMonitor    = GetSelectedComboValue(hComboWithMon, g_config.scaleWithMonitor);
                    g_config.autoEnabled = (SendDlgItemMessageW(hwnd, IDC_CHK_ENABLE, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    g_config.closeToTray = (SendDlgItemMessageW(hwnd, IDC_CHK_CLOSE_TRAY, BM_GETCHECK, 0, 0) == BST_CHECKED);

                    bool startupChecked = (SendDlgItemMessageW(hwnd, IDC_CHK_STARTUP, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    SetStartupEnabled(startupChecked);

                    SaveConfig();
                    ApplyAutoScale(true);
                    RefreshUI(hwnd, false);
                    break;
                }

                case IDC_BTN_TRAY:
                    ShowWindow(hwnd, SW_HIDE);
                    break;

                case IDC_BTN_EXIT:
                case IDM_TRAY_EXIT:
                    DestroyWindow(hwnd);
                    break;

                case IDM_TRAY_OPEN:
                    ShowWindow(hwnd, SW_RESTORE);
                    ShowWindow(hwnd, SW_SHOW);
                    SetForegroundWindow(hwnd);
                    RefreshUI(hwnd, true);
                    break;

                case IDM_TRAY_APPLY_NOW:
                    ApplyAutoScale(true);
                    RefreshUI(hwnd, false);
                    break;

                case IDM_TRAY_TOGGLE:
                    g_config.autoEnabled = !g_config.autoEnabled;
                    SaveConfig();
                    if (g_config.autoEnabled) ApplyAutoScale(true);
                    RefreshUI(hwnd, false);
                    break;
            }
            return 0;
        }

        case WM_CLOSE:
            if (g_config.closeToTray) {
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            RemoveTrayIcon();
            if (g_hFont) DeleteObject(g_hFont);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ==========================================
// Windows Application Entry Point
// ==========================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    // Prevent multiple instances
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"Local\\AutoDisplayScaleSingleInstanceMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(kWindowClassName, nullptr);
        if (existing) {
            ShowWindow(existing, SW_RESTORE);
            ShowWindow(existing, SW_SHOW);
            SetForegroundWindow(existing);
        }
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    SetProcessDPIAware();

    INITCOMMONCONTROLSEX icex = {};
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icex);

    InitPaths();
    LoadConfig();

    // Check for --tray startup flag
    bool startInTray = false;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (wcscmp(argv[i], L"--tray") == 0) {
                startInTray = true;
            }
        }
        LocalFree(argv);
    }

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    HICON hBigIcon = LoadIcon(hInstance, MAKEINTRESOURCE(101));
    HICON hSmallIcon = static_cast<HICON>(LoadImage(
        hInstance, MAKEINTRESOURCE(101), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));

    wc.hIcon   = hBigIcon ? hBigIcon : LoadIcon(nullptr, IDI_APPLICATION);
    wc.hIconSm = hSmallIcon ? hSmallIcon : wc.hIcon;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kWindowClassName;

    if (!RegisterClassExW(&wc)) {
        MessageBoxW(nullptr, L"Failed to register window class.", kAppTitle, MB_ICONERROR);
        return 1;
    }

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rc = { 0, 0, 410, 348 };
    AdjustWindowRectEx(&rc, style, FALSE, 0);

    int winW = rc.right - rc.left;
    int winH = rc.bottom - rc.top;
    int posX = (GetSystemMetrics(SM_CXSCREEN) - winW) / 2;
    int posY = (GetSystemMetrics(SM_CYSCREEN) - winH) / 2;

    g_hwndMain = CreateWindowExW(
        0, kWindowClassName, kAppTitle, style,
        posX, posY, winW, winH,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!g_hwndMain) {
        MessageBoxW(nullptr, L"Failed to create application window.", kAppTitle, MB_ICONERROR);
        return 1;
    }

    if (!startInTray) {
        ShowWindow(g_hwndMain, nCmdShow);
        UpdateWindow(g_hwndMain);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(g_hwndMain, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (hMutex) CloseHandle(hMutex);
    return static_cast<int>(msg.wParam);
}