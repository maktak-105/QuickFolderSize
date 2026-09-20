// CLI版エントリーポイント。GUIを介さず標準出力へJSONを1個返す(AIエージェント/
// スクリプトからの利用向け)。管理者権限は要求しない(マニフェスト未埋め込み=
// 既定のasInvokerで起動する)。非管理者実行時はengine側が自動的にWin32列挙へ
// フォールバックするため、CLI側に権限分岐のコードは不要。

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <string>

#include "engine.h"

#define APP_VERSION L"3.1.0"

// ===== JSON ヘルパー(webview_main.cpp と同一ロジックの自己完結コピー) =====

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

long long FileTimeInt64ToUnixMillis(long long ft) {
    if (ft == 0) return 0;
    return (ft - 116444736000000000LL) / 10000LL;
}

void SerializeEntryJson(const ScanEntryC& node, std::wstring& out) {
    out += L"{\"name\":\"" + JsonEscape(node.name) + L"\",";
    out += L"\"path\":\"" + JsonEscape(node.path) + L"\",";
    out += L"\"size\":" + std::to_wstring(node.size) + L",";
    out += L"\"file_count\":" + std::to_wstring(node.file_count) + L",";
    out += L"\"mtime_ms\":" + std::to_wstring(FileTimeInt64ToUnixMillis(node.mtime_raw)) + L",";
    out += L"\"is_accessible\":" + std::wstring(node.is_accessible ? L"true" : L"false") + L",";
    out += L"\"is_dir\":" + std::wstring(node.is_dir ? L"true" : L"false") + L",";
    out += L"\"children\":[";
    for (int i = 0; i < node.child_count; ++i) {
        if (i > 0) out += L",";
        SerializeEntryJson(node.children[i], out);
    }
    out += L"]}";
}

// compact なJSON文字列に改行・インデントを入れるだけの汎用整形(木構造を再帰しない)。
// 文字列リテラル内(エスケープ含む)はそのまま素通しする。
std::wstring PrettyPrintJson(const std::wstring& compact) {
    std::wstring out;
    out.reserve(compact.size() * 2);
    int depth = 0;
    bool in_string = false;
    for (size_t i = 0; i < compact.size(); ++i) {
        wchar_t c = compact[i];
        if (in_string) {
            out += c;
            if (c == L'\\' && i + 1 < compact.size()) { out += compact[++i]; continue; }
            if (c == L'"') in_string = false;
            continue;
        }
        switch (c) {
            case L'"':
                in_string = true;
                out += c;
                break;
            case L'{': case L'[':
                out += c;
                if (i + 1 < compact.size() && (compact[i + 1] == L'}' || compact[i + 1] == L']')) {
                    // 空オブジェクト/配列は改行しない
                } else {
                    ++depth;
                    out += L'\n' + std::wstring(depth * 2, L' ');
                }
                break;
            case L'}': case L']':
                if (i > 0 && (compact[i - 1] == L'{' || compact[i - 1] == L'[')) {
                    out += c;
                } else {
                    --depth;
                    out += L'\n' + std::wstring(depth * 2, L' ');
                    out += c;
                }
                break;
            case L',':
                out += c;
                out += L'\n' + std::wstring(depth * 2, L' ');
                break;
            case L':':
                out += c;
                out += L' ';
                break;
            default:
                out += c;
        }
    }
    return out;
}

void WriteUtf8Stdout(const std::wstring& text) {
    int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), NULL, 0, NULL, NULL);
    std::string utf8(needed, '\0');
    if (needed > 0) {
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &utf8[0], needed, NULL, NULL);
    }
    fwrite(utf8.data(), 1, utf8.size(), stdout);
    fwrite("\n", 1, 1, stdout);
}

// ===== スキャン実行(同期・1回限り。GUI版のようなキャッシュ・進捗通知は行わない) =====

static void NoOpChildDone(const ScanEntryC*, void*) {}

static const ScanEntryC* g_result = nullptr;
static void OnScanFinished(const ScanEntryC* node, void*) { g_result = node; }

int DirCount(const ScanEntryC& node) {
    int count = 0;
    for (int i = 0; i < node.child_count; ++i) {
        if (node.children[i].is_dir) ++count;
    }
    return count;
}

int main() {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wstring(argv[i]) == L"--version") {
                wprintf(L"QuickFolderSize CLI v%ls\n", APP_VERSION);
                LocalFree(argv);
                return 0;
            }
        }
    }

    if (!argv || argc < 2) {
        fwprintf(stderr, L"{\"error\":\"usage: QuickFolderSize_cli.exe <path> [--pretty] [--version]\"}\n");
        if (argv) LocalFree(argv);
        return 1;
    }

    std::wstring path = argv[1];
    if (!path.empty() && path.back() != L'\\') path += L'\\';

    bool pretty = false;
    for (int i = 2; i < argc; ++i) {
        if (std::wstring(argv[i]) == L"--pretty") pretty = true;
    }
    LocalFree(argv);

    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        fwprintf(stderr, L"{\"error\":\"path not found or not a directory: %ls\"}\n", path.c_str());
        return 1;
    }

    static int stop_flag = 0;
    scan_directory(path.c_str(), nullptr, NoOpChildDone, OnScanFinished, &stop_flag, nullptr);

    if (!g_result) {
        fwprintf(stderr, L"{\"error\":\"scan failed\"}\n");
        return 1;
    }

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t ts[32];
    swprintf_s(ts, L"%04d-%02d-%02dT%02d:%02d:%02d",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    std::wstring json;
    json += L"{\"path\":\"" + JsonEscape(g_result->path) + L"\",";
    json += L"\"scanned_at\":\"" + std::wstring(ts) + L"\",";
    json += L"\"total_size\":" + std::to_wstring(g_result->size) + L",";
    json += L"\"subfolder_count\":" + std::to_wstring(DirCount(*g_result)) + L",";
    json += L"\"file_count_recursive\":" + std::to_wstring(g_result->file_count) + L",";
    json += L"\"tree\":";
    SerializeEntryJson(*g_result, json);
    json += L"}";

    WriteUtf8Stdout(pretty ? PrettyPrintJson(json) : json);

    free_scan_tree(const_cast<ScanEntryC*>(g_result));
    return 0;
}
