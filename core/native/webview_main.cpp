#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <unknwn.h>
#include <WebView2.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <functional>
#include <cstdarg>

#include "engine.h"

#define WM_POST_JSON (WM_APP + 1)
#define IDI_ICON1 101

// ===== WebView2 動的ローダー用の型・COMコールバック(QuickDiskBenchと同一定型) =====
typedef HRESULT (STDAPICALLTYPE *CreateEnvFn)(
    PCWSTR browserExecutableFolder,
    PCWSTR userDataFolder,
    ICoreWebView2EnvironmentOptions* environmentOptions,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* environment_created_handler
);

class EnvCompletedHandler : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
    std::function<HRESULT(HRESULT, ICoreWebView2Environment*)> m_fn;
    std::atomic<ULONG> m_ref{ 1 };
public:
    EnvCompletedHandler(std::function<HRESULT(HRESULT, ICoreWebView2Environment*)> fn) : m_fn(fn) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler) {
            *ppv = static_cast<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --m_ref;
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Environment* env) override {
        return m_fn(result, env);
    }
};

class ControllerCompletedHandler : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
    std::function<HRESULT(HRESULT, ICoreWebView2Controller*)> m_fn;
    std::atomic<ULONG> m_ref{ 1 };
public:
    ControllerCompletedHandler(std::function<HRESULT(HRESULT, ICoreWebView2Controller*)> fn) : m_fn(fn) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler) {
            *ppv = static_cast<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --m_ref;
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Controller* controller) override {
        return m_fn(result, controller);
    }
};

class WebMessageReceivedHandler : public ICoreWebView2WebMessageReceivedEventHandler {
    std::function<HRESULT(ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*)> m_fn;
    std::atomic<ULONG> m_ref{ 1 };
public:
    WebMessageReceivedHandler(std::function<HRESULT(ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*)> fn) : m_fn(fn) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_ICoreWebView2WebMessageReceivedEventHandler) {
            *ppv = static_cast<ICoreWebView2WebMessageReceivedEventHandler*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --m_ref;
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) override {
        return m_fn(sender, args);
    }
};

// ===== グローバル状態 =====
HWND g_hWnd = NULL;
ICoreWebView2Controller* g_controller = NULL;
ICoreWebView2* g_webview = NULL;

std::atomic<bool> g_isRunning(false);
std::atomic<int>  g_stopFlag(0);
std::thread g_workerThread;

std::mutex   g_prevRootMtx;
ScanEntryC*  g_prevRoot = nullptr; // 直前スキャンのルート(mtimeキャッシュとして次回スキャンに渡す)

std::wstring g_logPath;

static void LOG(const char* fmt, ...);

// ===== JSON ヘルパー =====

std::wstring JsonEscape(const std::wstring& value) {
    std::wstring escaped;
    escaped.reserve(value.size());
    for (wchar_t ch : value) {
        switch (ch) {
            case L'\\': escaped += L"\\\\"; break;
            case L'"':  escaped += L"\\\""; break;
            case L'\n': escaped += L"\\n";  break;
            case L'\r': escaped += L"\\r";  break;
            case L'\t': escaped += L"\\t";  break;
            default:    escaped += ch;      break;
        }
    }
    return escaped;
}

// 1階層のJSONオブジェクトから "key":"value"(文字列)を取り出す最小限のパーサ。
// \\ \" \n \r \t \uXXXX のエスケープに対応。ネストしたオブジェクト/配列は扱わない
// (このアプリのWebMessageは常にフラットな文字列プロパティのみで構成されるため)。
bool ExtractJsonString(const std::wstring& json, const std::wstring& key, std::wstring& out) {
    std::wstring needle = L"\"" + key + L"\"";
    size_t pos = json.find(needle);
    if (pos == std::wstring::npos) return false;
    pos = json.find(L':', pos + needle.size());
    if (pos == std::wstring::npos) return false;
    pos = json.find_first_not_of(L" \t\r\n", pos + 1);
    if (pos == std::wstring::npos || json[pos] != L'"') return false;
    ++pos;

    std::wstring result;
    while (pos < json.size() && json[pos] != L'"') {
        wchar_t ch = json[pos];
        if (ch == L'\\' && pos + 1 < json.size()) {
            wchar_t next = json[pos + 1];
            switch (next) {
                case L'"':  result += L'"';  pos += 2; break;
                case L'\\': result += L'\\'; pos += 2; break;
                case L'/':  result += L'/';  pos += 2; break;
                case L'n':  result += L'\n'; pos += 2; break;
                case L'r':  result += L'\r'; pos += 2; break;
                case L't':  result += L'\t'; pos += 2; break;
                case L'u':
                    if (pos + 6 <= json.size()) {
                        wchar_t code = (wchar_t)wcstol(json.substr(pos + 2, 4).c_str(), nullptr, 16);
                        result += code;
                        pos += 6;
                    } else {
                        result += ch; ++pos;
                    }
                    break;
                default: result += next; pos += 2; break;
            }
        } else {
            result += ch;
            ++pos;
        }
    }
    out = result;
    return true;
}

long long FileTimeInt64ToUnixMillis(long long ft) {
    if (ft == 0) return 0;
    // FILETIME: 1601-01-01からの100ns単位。Unixエポックとの差は116444736000000000。
    return (ft - 116444736000000000LL) / 10000LL;
}

void SerializeEntryJson(const ScanEntryC& node, std::wstringstream& ss) {
    ss << L"{\"name\":\"" << JsonEscape(node.name) << L"\","
       << L"\"path\":\"" << JsonEscape(node.path) << L"\","
       << L"\"size\":" << node.size << L","
       << L"\"file_count\":" << node.file_count << L","
       << L"\"mtime_ms\":" << FileTimeInt64ToUnixMillis(node.mtime_raw) << L","
       << L"\"is_accessible\":" << (node.is_accessible ? L"true" : L"false") << L","
       << L"\"is_dir\":" << (node.is_dir ? L"true" : L"false") << L","
       << L"\"children\":[";
    for (int i = 0; i < node.child_count; ++i) {
        if (i > 0) ss << L",";
        SerializeEntryJson(node.children[i], ss);
    }
    ss << L"]}";
}

void PostJsonToWebView(const std::wstring& json) {
    std::wstring* pJson = new std::wstring(json);
    PostMessageW(g_hWnd, WM_POST_JSON, 0, (LPARAM)pJson);
}

// ===== ファイルダイアログ (IFileDialog) =====

std::wstring ShowFolderPickerDialog(HWND owner, const std::wstring& initial_dir) {
    std::wstring result;
    IFileDialog* pfd = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) {
        DWORD opts = 0;
        pfd->GetOptions(&opts);
        pfd->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        if (!initial_dir.empty()) {
            IShellItem* psi = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(initial_dir.c_str(), NULL, IID_PPV_ARGS(&psi)))) {
                pfd->SetFolder(psi);
                psi->Release();
            }
        }
        if (SUCCEEDED(pfd->Show(owner))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(pfd->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    result = path;
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        pfd->Release();
    }
    return result;
}

std::wstring ShowSaveFileDialog(HWND owner, const std::wstring& default_name) {
    std::wstring result;
    IFileDialog* pfd = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileSaveDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) {
        COMDLG_FILTERSPEC filters[] = {
            { L"Markdown Files (*.md)", L"*.md" },
            { L"All Files (*.*)", L"*.*" }
        };
        pfd->SetFileTypes(2, filters);
        pfd->SetDefaultExtension(L"md");
        pfd->SetFileName(default_name.c_str());
        if (SUCCEEDED(pfd->Show(owner))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(pfd->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    result = path;
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        pfd->Release();
    }
    return result;
}

// ===== スキャン実行 =====

struct ScanRunContext {
    std::wstring root_path;
    std::chrono::steady_clock::time_point start_time;
};

void OnChildDone(const ScanEntryC* node, void* user_data) {
    auto* rc = static_cast<ScanRunContext*>(user_data);
    std::wstringstream ss;
    ss << L"{\"type\":\"scan_progress\",\"root\":\"" << JsonEscape(rc->root_path) << L"\",\"data\":";
    SerializeEntryJson(*node, ss);
    ss << L"}";
    PostJsonToWebView(ss.str());
}

void OnFinished(const ScanEntryC* node, void* user_data) {
    auto* rc = static_cast<ScanRunContext*>(user_data);
    bool was_cancelled = (g_stopFlag.load() != 0);

    if (was_cancelled) {
        // ユーザーが別フォルダのスキャンを開始してキャンセルされたケース。
        // Python版と同様、キャンセル時の部分木は使わず破棄する。
        free_scan_tree(const_cast<ScanEntryC*>(node));
    } else {
        double elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - rc->start_time).count();
        std::wstringstream ss;
        ss << std::fixed << std::setprecision(3);
        ss << L"{\"type\":\"scan_finished\",\"root\":\"" << JsonEscape(rc->root_path)
           << L"\",\"elapsed_seconds\":" << elapsed << L",\"data\":";
        SerializeEntryJson(*node, ss);
        ss << L"}";
        PostJsonToWebView(ss.str());

        std::lock_guard<std::mutex> lock(g_prevRootMtx);
        if (g_prevRoot) free_scan_tree(g_prevRoot);
        g_prevRoot = const_cast<ScanEntryC*>(node);
    }

    g_isRunning = false;
    delete rc;
}

void ScanWorkerThread(std::wstring path) {
    auto* rc = new ScanRunContext{ path, std::chrono::steady_clock::now() };
    const ScanEntryC* cache = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_prevRootMtx);
        cache = g_prevRoot;
    }
    scan_directory(path.c_str(), cache, OnChildDone, OnFinished,
                   reinterpret_cast<const int*>(&g_stopFlag), rc);
}

// フォルダを開いた直後、完全スキャンが終わる前に直下1レベルを即時表示するための
// 軽量な非再帰リスト。UIスレッド上で同期実行する(単一ディレクトリの列挙のみで高速)。
void SendScanPlaceholder(const std::wstring& path) {
    std::wstring search = path;
    if (!search.empty() && search.back() != L'\\') search += L'\\';
    std::wstring pattern = search + L"*";

    std::wstringstream ss;
    ss << std::fixed << std::setprecision(2);
    ss << L"{\"type\":\"scan_placeholder\",\"path\":\"" << JsonEscape(path) << L"\",";

    std::wstring drive_root = (path.size() >= 2 && path[1] == L':') ? path.substr(0, 2) + L"\\" : L"C:\\";
    wchar_t vol_name[256] = {0};
    GetVolumeInformationW(drive_root.c_str(), vol_name, 256, NULL, NULL, NULL, NULL, 0);
    ULARGE_INTEGER free_b{}, total_b{};
    double total_gb = 0, free_gb = 0, used_gb = 0;
    if (GetDiskFreeSpaceExW(drive_root.c_str(), &free_b, &total_b, NULL)) {
        total_gb = total_b.QuadPart / (1024.0 * 1024.0 * 1024.0);
        free_gb  = free_b.QuadPart  / (1024.0 * 1024.0 * 1024.0);
        used_gb  = total_gb - free_gb;
    }
    ss << L"\"drive\":{\"path\":\"" << JsonEscape(drive_root) << L"\",\"label\":\"" << JsonEscape(vol_name)
       << L"\",\"total_gb\":" << total_gb << L",\"free_gb\":" << free_gb << L",\"used_gb\":" << used_gb << L"},";

    ss << L"\"data\":[";
    bool first = true;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..") continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
            bool is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (!first) ss << L",";
            first = false;
            ss << L"{\"name\":\"" << JsonEscape(name) << L"\",\"path\":\""
               << JsonEscape(search + name) << L"\",\"is_dir\":" << (is_dir ? L"true" : L"false") << L"}";
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    ss << L"]}";
    if (g_webview) g_webview->PostWebMessageAsJson(ss.str().c_str());
}

// ===== WebMessage コマンドハンドラ(すべてUIスレッド上で同期実行) =====

void HandleGetDrives() {
    DriveInfoC* drives = nullptr;
    int count = 0;
    get_drives(&drives, &count);

    std::wstringstream ss;
    ss << std::fixed << std::setprecision(2);
    ss << L"{\"type\":\"drives\",\"data\":[";
    for (int i = 0; i < count; ++i) {
        if (i > 0) ss << L",";
        ss << L"{\"path\":\"" << JsonEscape(drives[i].path) << L"\","
           << L"\"label\":\"" << JsonEscape(drives[i].label) << L"\","
           << L"\"total_gb\":" << drives[i].total_gb << L","
           << L"\"free_gb\":" << drives[i].free_gb << L","
           << L"\"used_gb\":" << drives[i].used_gb << L"}";
    }
    ss << L"]}";
    free_drives(drives, count);
    if (g_webview) g_webview->PostWebMessageAsJson(ss.str().c_str());
}

void HandleBrowse(const std::wstring& initial) {
    std::wstring path = ShowFolderPickerDialog(g_hWnd, initial);
    std::wstringstream ss;
    ss << L"{\"type\":\"browse_result\",\"path\":\"" << JsonEscape(path) << L"\"}";
    if (g_webview) g_webview->PostWebMessageAsJson(ss.str().c_str());
}

void HandleScan(const std::wstring& path_in) {
    std::wstring path = path_in;
    if (!path.empty() && path.back() != L'\\') path += L'\\';

    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        std::wstringstream ss;
        ss << L"{\"type\":\"scan_error\",\"path\":\"" << JsonEscape(path_in) << L"\"}";
        if (g_webview) g_webview->PostWebMessageAsJson(ss.str().c_str());
        return;
    }

    if (g_isRunning) {
        g_stopFlag = 1;
        if (g_workerThread.joinable()) g_workerThread.join();
    }

    SendScanPlaceholder(path);

    g_stopFlag = 0;
    g_isRunning = true;
    if (g_workerThread.joinable()) g_workerThread.join();
    g_workerThread = std::thread(ScanWorkerThread, path);
}

void HandleNavExpand(const std::wstring& path) {
    DirEntryC* entries = nullptr;
    int count = 0;
    list_subdirectories(path.c_str(), &entries, &count);

    std::wstringstream ss;
    ss << L"{\"type\":\"nav_children\",\"path\":\"" << JsonEscape(path) << L"\",\"data\":[";
    for (int i = 0; i < count; ++i) {
        if (i > 0) ss << L",";
        ss << L"{\"name\":\"" << JsonEscape(entries[i].name) << L"\",\"path\":\""
           << JsonEscape(entries[i].path) << L"\"}";
    }
    ss << L"]}";
    free_dir_entries(entries, count);
    if (g_webview) g_webview->PostWebMessageAsJson(ss.str().c_str());
}

void HandleExportReport(const std::wstring& content, const std::wstring& default_name) {
    std::wstring name = default_name.empty() ? L"report.md" : default_name;
    std::wstring path = ShowSaveFileDialog(g_hWnd, name);
    if (path.empty()) return; // キャンセル時はPython版と同様に何もしない

    bool ok = false;
    std::ofstream out(path.c_str(), std::ios::binary);
    if (out.is_open()) {
        int needed = WideCharToMultiByte(CP_UTF8, 0, content.c_str(), (int)content.size(), NULL, 0, NULL, NULL);
        std::string utf8(needed, '\0');
        if (needed > 0) {
            WideCharToMultiByte(CP_UTF8, 0, content.c_str(), (int)content.size(), &utf8[0], needed, NULL, NULL);
        }
        out.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
        ok = out.good();
        out.close();
    }

    std::wstringstream ss;
    ss << L"{\"type\":\"export_result\",\"success\":" << (ok ? L"true" : L"false")
       << L",\"path\":\"" << JsonEscape(path) << L"\"}";
    if (g_webview) g_webview->PostWebMessageAsJson(ss.str().c_str());
}

// ===== ウィンドウプロシージャ =====

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE:
        if (g_controller) {
            RECT bounds;
            GetClientRect(hWnd, &bounds);
            g_controller->put_Bounds(bounds);
        }
        break;

    case WM_POST_JSON: {
        std::wstring* pJson = (std::wstring*)lParam;
        if (pJson) {
            if (g_webview) g_webview->PostWebMessageAsJson(pJson->c_str());
            delete pJson;
        }
        break;
    }

    case WM_DESTROY:
        g_stopFlag = 1;
        if (g_workerThread.joinable()) g_workerThread.detach();
        {
            std::lock_guard<std::mutex> lock(g_prevRootMtx);
            if (g_prevRoot) { free_scan_tree(g_prevRoot); g_prevRoot = nullptr; }
        }
        if (g_controller) {
            g_controller->Close();
            g_controller->Release();
            g_controller = NULL;
        }
        if (g_webview) {
            g_webview->Release();
            g_webview = NULL;
        }
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

std::wstring ReadUtf8FileToWString(const std::wstring& path) {
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file.is_open()) return L"";
    std::string str((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (str.empty()) return L"";
    int count = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), NULL, 0);
    std::wstring wstr(count, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), &wstr[0], count);
    return wstr;
}

static FILE* g_log = nullptr;
static void LOG(const char* fmt, ...) {
    if (!g_log) {
        if (g_logPath.empty()) {
            wchar_t exePath[MAX_PATH] = {0};
            GetModuleFileNameW(NULL, exePath, MAX_PATH);
            g_logPath = exePath;
            size_t slash = g_logPath.find_last_of(L"\\/");
            if (slash != std::wstring::npos) g_logPath.resize(slash + 1);
            g_logPath += L"QuickFolderSize_debug.log";
        }
        char path[MAX_PATH * 3] = {0};
        WideCharToMultiByte(CP_UTF8, 0, g_logPath.c_str(), -1, path, sizeof(path), NULL, NULL);
        g_log = fopen(path, "w");
    }
    if (!g_log) return;
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fprintf(g_log, "\n"); fflush(g_log);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    LOG("[1] WinMain entered");

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    OleInitialize(NULL);
    SetProcessDPIAware();
    LOG("[2] COM initialized");

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"QuickFolderSizeNativeWebView2Class";
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_ICON1));
    wc.hIconSm = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_ICON1));

    if (!RegisterClassExW(&wc)) { LOG("[ERR] RegisterClassExW failed: %lu", GetLastError()); return 1; }
    LOG("[3] Window class registered");

    g_hWnd = CreateWindowExW(
        0, wc.lpszClassName,
        L"QuickFolderSize",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1200, 720,
        NULL, NULL, hInstance, NULL
    );
    if (!g_hWnd) { LOG("[ERR] CreateWindowExW failed: %lu", GetLastError()); return 1; }
    LOG("[4] Window created HWND=%p", (void*)g_hWnd);

    BOOL dark = TRUE;
    DwmSetWindowAttribute(g_hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);
    LOG("[5] Window shown");

    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring appDir = exePath;
    size_t lastSlash = appDir.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) appDir = appDir.substr(0, lastSlash);

    std::wstring htmlFile = appDir + L"\\index.html";
    if (GetFileAttributesW(htmlFile.c_str()) == INVALID_FILE_ATTRIBUTES) {
        htmlFile = appDir + L"\\templates\\index.html";
    }
    LOG("[6] HTML file: %S (exists=%d)", htmlFile.c_str(),
        GetFileAttributesW(htmlFile.c_str()) != INVALID_FILE_ATTRIBUTES ? 1 : 0);

    std::wstring htmlContent = ReadUtf8FileToWString(htmlFile);
    LOG("[7] htmlContent size: %zu chars", htmlContent.size());

    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring userDataFolder = std::wstring(tempPath) + L"QuickFolderSize_WVData";
    CreateDirectoryW(userDataFolder.c_str(), NULL);
    LOG("[8] User data folder: %S", userDataFolder.c_str());

    std::wstring loaderDll = appDir + L"\\WebView2Loader.dll";
    HMODULE hLoader = LoadLibraryW(loaderDll.c_str());
    if (!hLoader) {
        hLoader = LoadLibraryW(L"C:\\tools\\webview2\\build\\native\\x64\\WebView2Loader.dll");
    }
    if (!hLoader) {
        LOG("[ERR] WebView2Loader.dll not found");
        MessageBoxW(g_hWnd, L"WebView2Loader.dll が見つかりません。\nQuickFolderSize.exe と同じフォルダに配置してください。",
                    L"QuickFolderSize Error", MB_ICONERROR);
        return 1;
    }
    LOG("[9] WebView2Loader.dll loaded");

    CreateEnvFn createEnv = (CreateEnvFn)GetProcAddress(hLoader, "CreateCoreWebView2EnvironmentWithOptions");
    if (!createEnv) {
        LOG("[ERR] CreateCoreWebView2EnvironmentWithOptions not found in DLL");
        MessageBoxW(g_hWnd, L"WebView2: CreateCoreWebView2EnvironmentWithOptions not found", L"QuickFolderSize Error", MB_ICONERROR);
        return 1;
    }
    LOG("[10] createEnv proc found, calling...");

    createEnv(
        nullptr, userDataFolder.c_str(), nullptr,
        new EnvCompletedHandler(
            [htmlContent](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                LOG("[11] EnvCompletedHandler called, HRESULT=0x%08X, env=%p", (unsigned)result, (void*)env);
                if (FAILED(result) || !env) {
                    wchar_t msg[512];
                    swprintf_s(msg, L"WebView2 Runtimeを初期化できませんでした。\n\n"
                                   L"WebView2 Runtimeがインストールされていないか、破損している可能性があります。\n"
                                   L"Microsoft Edge WebView2 Runtime (Evergreen) をインストールしてから再実行してください。\n\n"
                                   L"エラーコード: 0x%08X",
                               (unsigned)result);
                    MessageBoxW(g_hWnd, msg, L"QuickFolderSize Error", MB_ICONERROR);
                    return result;
                }

                env->CreateCoreWebView2Controller(g_hWnd,
                    new ControllerCompletedHandler(
                        [htmlContent](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                            LOG("[12] ControllerCompletedHandler called, HRESULT=0x%08X, ctrl=%p", (unsigned)result, (void*)controller);
                            if (FAILED(result) || !controller) {
                                wchar_t msg[256];
                                swprintf_s(msg, L"WebView2 controller creation failed: 0x%08X", (unsigned)result);
                                MessageBoxW(g_hWnd, msg, L"QuickFolderSize Error", MB_ICONERROR);
                                return result;
                            }

                            g_controller = controller;
                            g_controller->AddRef();
                            g_controller->get_CoreWebView2(&g_webview);

                            RECT bounds;
                            GetClientRect(g_hWnd, &bounds);
                            g_controller->put_Bounds(bounds);
                            g_controller->put_IsVisible(TRUE);

                            ICoreWebView2Settings* settings = NULL;
                            g_webview->get_Settings(&settings);
                            if (settings) {
                                settings->put_AreDefaultContextMenusEnabled(FALSE);
                                settings->put_IsStatusBarEnabled(FALSE);
                                settings->Release();
                            }

                            EventRegistrationToken token;
                            g_webview->add_WebMessageReceived(
                                new WebMessageReceivedHandler(
                                    [](ICoreWebView2* webview, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                        LPWSTR jsonRaw = NULL;
                                        args->get_WebMessageAsJson(&jsonRaw);
                                        std::wstring msg = jsonRaw ? jsonRaw : L"";
                                        if (jsonRaw) CoTaskMemFree(jsonRaw);

                                        std::wstring cmd;
                                        if (!ExtractJsonString(msg, L"cmd", cmd)) return S_OK;

                                        if (cmd == L"get_drives") {
                                            HandleGetDrives();
                                        } else if (cmd == L"browse") {
                                            std::wstring initial;
                                            ExtractJsonString(msg, L"initial", initial);
                                            HandleBrowse(initial);
                                        } else if (cmd == L"scan") {
                                            std::wstring path;
                                            if (ExtractJsonString(msg, L"path", path)) HandleScan(path);
                                        } else if (cmd == L"nav_expand") {
                                            std::wstring path;
                                            if (ExtractJsonString(msg, L"path", path)) HandleNavExpand(path);
                                        } else if (cmd == L"export_report") {
                                            std::wstring content, default_name;
                                            ExtractJsonString(msg, L"content", content);
                                            ExtractJsonString(msg, L"default_name", default_name);
                                            HandleExportReport(content, default_name);
                                        } else if (cmd == L"quit") {
                                            PostMessageW(g_hWnd, WM_CLOSE, 0, 0);
                                        }
                                        return S_OK;
                                    }
                                ), &token
                            );

                            LOG("[13] Calling NavigateToString, htmlContent.size=%zu", htmlContent.size());
                            if (htmlContent.empty()) {
                                MessageBoxW(g_hWnd, L"index.html の読み込みに失敗しました。\nQuickFolderSize.exe と同じフォルダに配置してください。",
                                            L"QuickFolderSize Error", MB_ICONERROR);
                            } else {
                                g_webview->NavigateToString(htmlContent.c_str());
                            }
                            return S_OK;
                        }
                    )
                );
                return S_OK;
            }
        )
    );

    LOG("[14] Entering message loop");
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    OleUninitialize();
    CoUninitialize();
    LOG("[15] Exiting WinMain");
    if (g_log) fclose(g_log);
    return (int)msg.wParam;
}
