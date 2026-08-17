// スキャンエンジン本体。
//
// 設計は python/scanner.py の「単一共有プール・非ブロッキングfan-out」方式を
// そのままC++へ移植したもの: 固定サイズのスレッドプールを全階層で共有し、各タスクは
// 「自分の分を処理して即返る」か「子ディレクトリをプールへ再投入して即返る」かのどちらか
// しかしない。同じプールに投入したタスクの完了を待ってブロックすることはないため、
// ツリーの深さに関わらずデッドロックしない。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "engine.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <queue>
#include <algorithm>

namespace {

constexpr size_t kMaxWorkers = 32;

// ---- 汎用スレッドプール ----------------------------------------------------
class ThreadPool {
public:
    explicit ThreadPool(size_t n) {
        workers_.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            workers_.emplace_back([this] { WorkerLoop(); });
        }
    }
    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& t : workers_) {
            if (t.joinable()) t.join();
        }
    }

    void Submit(std::function<void()> fn) {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            tasks_.push(std::move(fn));
        }
        cv_.notify_one();
    }

private:
    void WorkerLoop() {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mtx_);
                cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                if (stop_ && tasks_.empty()) return;
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mtx_;
    std::condition_variable cv_;
    bool stop_ = false;
};

// ---- ツリー構築用の中間表現 -------------------------------------------------
struct EntryBuilder {
    std::wstring name;
    std::wstring path;
    unsigned long long size = 0;
    unsigned long long file_count = 0;
    long long mtime_raw = 0;
    bool is_accessible = true;
    bool is_dir = true;
    std::vector<EntryBuilder> children;
};

struct ScanCtx {
    ThreadPool* pool;
    const int* stop_flag;
    const std::unordered_map<std::wstring, const ScanEntryC*>* cache; // nullable
};

bool Stopped(const ScanCtx& ctx) { return ctx.stop_flag && *ctx.stop_flag != 0; }

long long FileTimeToInt64(const FILETIME& ft) {
    return (static_cast<long long>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

bool GetDirMTime(const std::wstring& path, long long& out_mtime) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return false;
    out_mtime = FileTimeToInt64(data.ftLastWriteTime);
    return true;
}

bool IsRealDir(const WIN32_FIND_DATAW& fd) {
    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
    // シンボリックリンク/NTFSジャンクションは除外(python版 _is_real_dir と同義)
    return !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

std::wstring BaseName(const std::wstring& path) {
    std::wstring p = path;
    while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
    size_t slash = p.find_last_of(L"\\/");
    std::wstring name = (slash == std::wstring::npos) ? p : p.substr(slash + 1);
    return name.empty() ? path : name;
}

// C互換ツリーへ変換(値コピー、_wcsdupで文字列複製)。out は呼び出し元が確保済み。
void FillCTree(const EntryBuilder& b, ScanEntryC& out) {
    out.name = _wcsdup(b.name.c_str());
    out.path = _wcsdup(b.path.c_str());
    out.size = b.size;
    out.file_count = b.file_count;
    out.mtime_raw = b.mtime_raw;
    out.is_accessible = b.is_accessible ? 1 : 0;
    out.is_dir = b.is_dir ? 1 : 0;
    out.child_count = static_cast<int>(b.children.size());
    if (out.child_count > 0) {
        out.children = new ScanEntryC[out.child_count];
        for (int i = 0; i < out.child_count; ++i) FillCTree(b.children[i], out.children[i]);
    } else {
        out.children = nullptr;
    }
}

void FreeCTreeContents(ScanEntryC& node) {
    free(const_cast<wchar_t*>(node.name));
    free(const_cast<wchar_t*>(node.path));
    for (int i = 0; i < node.child_count; ++i) FreeCTreeContents(node.children[i]);
    delete[] node.children;
    node.children = nullptr;
    node.child_count = 0;
}

void FlattenCache(const ScanEntryC* node, std::unordered_map<std::wstring, const ScanEntryC*>& out) {
    if (!node) return;
    out[node->path] = node;
    for (int i = 0; i < node->child_count; ++i) FlattenCache(&node->children[i], out);
}

using DoneFn = std::function<void(EntryBuilder)>;
using ChildNotifyFn = std::function<void(const EntryBuilder&)>;

void ScanNode(const std::wstring& path, ScanCtx& ctx, DoneFn on_done);

// dir_paths を非ブロッキングでプールへ再投入し、全員完了したら node を完成させて
// on_done を呼ぶ。notify が渡されていれば、各子ディレクトリが完了するたびに
// (親の完成を待たずに)即時通知する — スキャンルート直下の進捗表示に使う。
void FanOut(std::vector<std::wstring> dir_paths,
            std::vector<EntryBuilder> file_children,
            EntryBuilder node,
            ScanCtx& ctx,
            DoneFn on_done,
            ChildNotifyFn notify = nullptr) {
    if (dir_paths.empty() || Stopped(ctx)) {
        std::sort(file_children.begin(), file_children.end(),
                  [](const EntryBuilder& a, const EntryBuilder& b) { return a.size > b.size; });
        for (auto& f : file_children) node.children.push_back(std::move(f));
        on_done(std::move(node));
        return;
    }

    auto shared_node  = std::make_shared<EntryBuilder>(std::move(node));
    auto shared_files = std::make_shared<std::vector<EntryBuilder>>(std::move(file_children));
    auto dir_children = std::make_shared<std::vector<EntryBuilder>>();
    auto pending       = std::make_shared<std::atomic<size_t>>(dir_paths.size());
    auto mtx           = std::make_shared<std::mutex>();
    auto notify_ptr    = std::make_shared<ChildNotifyFn>(std::move(notify));

    for (auto& dp : dir_paths) {
        ctx.pool->Submit([dp, &ctx, shared_node, shared_files, dir_children, pending, mtx, on_done, notify_ptr]() {
            ScanNode(dp, ctx,
                [&ctx, shared_node, shared_files, dir_children, pending, mtx, on_done, notify_ptr](EntryBuilder child) {
                    if (*notify_ptr) (*notify_ptr)(child);
                    bool finished;
                    {
                        std::lock_guard<std::mutex> lock(*mtx);
                        shared_node->size += child.size;
                        shared_node->file_count += child.file_count;
                        dir_children->push_back(std::move(child));
                        finished = (--(*pending) == 0);
                    }
                    if (finished) {
                        std::sort(dir_children->begin(), dir_children->end(),
                                  [](const EntryBuilder& a, const EntryBuilder& b) { return a.size > b.size; });
                        std::sort(shared_files->begin(), shared_files->end(),
                                  [](const EntryBuilder& a, const EntryBuilder& b) { return a.size > b.size; });
                        EntryBuilder finalNode = std::move(*shared_node);
                        for (auto& d : *dir_children) finalNode.children.push_back(std::move(d));
                        for (auto& f : *shared_files) finalNode.children.push_back(std::move(f));
                        on_done(std::move(finalNode));
                    }
                });
        });
    }
}

// path を列挙して EntryBuilder を構築する共通処理(キャッシュを使わない完全スキャン)。
void FullScanAndFanOut(const std::wstring& path, EntryBuilder node, ScanCtx& ctx, DoneFn on_done) {
    std::wstring search = path;
    if (!search.empty() && search.back() != L'\\') search += L'\\';
    std::wstring pattern = search + L"*";

    std::vector<EntryBuilder> file_children;
    std::vector<std::wstring> dir_paths;

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        node.is_accessible = false;
        on_done(std::move(node));
        return;
    }
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        std::wstring child_path = search + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (IsRealDir(fd)) dir_paths.push_back(child_path);
        } else {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue; // シンボリックリンクファイルは除外
            EntryBuilder fe;
            fe.name = name;
            fe.path = child_path;
            ULARGE_INTEGER sz;
            sz.HighPart = fd.nFileSizeHigh;
            sz.LowPart = fd.nFileSizeLow;
            fe.size = sz.QuadPart;
            fe.is_dir = false;
            fe.mtime_raw = FileTimeToInt64(fd.ftLastWriteTime);
            node.size += fe.size;
            node.file_count += 1;
            file_children.push_back(std::move(fe));
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    FanOut(std::move(dir_paths), std::move(file_children), std::move(node), ctx, std::move(on_done));
}

// キャッシュヒット時: このディレクトリ自身の scandir は省略し、キャッシュされた
// ファイル子要素をそのまま採用、サブディレクトリは個別に ScanNode() で再検証する。
void FromCacheAndFanOut(const ScanEntryC& cached, EntryBuilder node, ScanCtx& ctx, DoneFn on_done) {
    std::vector<EntryBuilder> file_children;
    std::vector<std::wstring> dir_paths;

    for (int i = 0; i < cached.child_count; ++i) {
        const ScanEntryC& c = cached.children[i];
        if (c.is_dir) {
            dir_paths.push_back(c.path);
        } else {
            EntryBuilder fe;
            fe.name = c.name;
            fe.path = c.path;
            fe.size = c.size;
            fe.is_dir = false;
            fe.mtime_raw = c.mtime_raw;
            node.size += fe.size;
            node.file_count += 1;
            file_children.push_back(std::move(fe));
        }
    }

    FanOut(std::move(dir_paths), std::move(file_children), std::move(node), ctx, std::move(on_done));
}

void ScanNode(const std::wstring& path, ScanCtx& ctx, DoneFn on_done) {
    EntryBuilder node;
    node.path = path;
    node.name = BaseName(path);

    if (Stopped(ctx)) {
        on_done(std::move(node));
        return;
    }

    long long mtime = 0;
    if (!GetDirMTime(path, mtime)) {
        node.is_accessible = false;
        on_done(std::move(node));
        return;
    }
    node.mtime_raw = mtime;

    const ScanEntryC* cached = nullptr;
    if (ctx.cache) {
        auto it = ctx.cache->find(path);
        if (it != ctx.cache->end() && it->second->mtime_raw == mtime) cached = it->second;
    }

    if (cached) {
        FromCacheAndFanOut(*cached, std::move(node), ctx, std::move(on_done));
    } else {
        FullScanAndFanOut(path, std::move(node), ctx, std::move(on_done));
    }
}

} // namespace

extern "C" int scan_directory(
    const wchar_t* root_path_w,
    const ScanEntryC* prev_cache_root,
    ScanNodeCallback on_child_done,
    ScanNodeCallback on_finished,
    const int* stop_flag,
    void* user_data
) {
    std::wstring root_path(root_path_w);
    if (!root_path.empty() && root_path.back() != L'\\') root_path += L'\\';

    ThreadPool pool(kMaxWorkers);
    std::unordered_map<std::wstring, const ScanEntryC*> cache_map;
    if (prev_cache_root) FlattenCache(prev_cache_root, cache_map);
    ScanCtx ctx{ &pool, stop_flag, prev_cache_root ? &cache_map : nullptr };

    std::mutex done_mtx;
    std::condition_variable done_cv;
    bool done = false;

    EntryBuilder root;
    root.path = root_path;
    root.name = BaseName(root_path);

    long long mtime = 0;
    GetDirMTime(root_path, mtime); // ルート自身は常に新規列挙するが、mtimeは表示用に取得しておく
    root.mtime_raw = mtime;

    auto finalize = [&](EntryBuilder finalRoot) {
        ScanEntryC* out = new ScanEntryC();
        FillCTree(finalRoot, *out);
        if (on_finished) on_finished(out, user_data);
        {
            std::lock_guard<std::mutex> lock(done_mtx);
            done = true;
        }
        done_cv.notify_one();
    };

    auto notify_child = [&](const EntryBuilder& child) {
        ScanEntryC tmp{};
        FillCTree(child, tmp);
        if (on_child_done) on_child_done(&tmp, user_data);
        FreeCTreeContents(tmp);
    };

    std::wstring pattern = root_path + L"*";
    std::vector<EntryBuilder> file_children;
    std::vector<std::wstring> dir_paths;

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        root.is_accessible = false;
    } else {
        do {
            std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..") continue;
            std::wstring child_path = root_path + name;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (IsRealDir(fd)) dir_paths.push_back(child_path);
            } else {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
                EntryBuilder fe;
                fe.name = name;
                fe.path = child_path;
                ULARGE_INTEGER sz;
                sz.HighPart = fd.nFileSizeHigh;
                sz.LowPart = fd.nFileSizeLow;
                fe.size = sz.QuadPart;
                fe.is_dir = false;
                fe.mtime_raw = FileTimeToInt64(fd.ftLastWriteTime);
                root.size += fe.size;
                root.file_count += 1;
                file_children.push_back(std::move(fe));
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    if (!root.is_accessible || dir_paths.empty() || Stopped(ctx)) {
        std::sort(file_children.begin(), file_children.end(),
                  [](const EntryBuilder& a, const EntryBuilder& b) { return a.size > b.size; });
        for (auto& f : file_children) root.children.push_back(std::move(f));
        finalize(std::move(root));
    } else {
        FanOut(std::move(dir_paths), std::move(file_children), std::move(root), ctx,
               finalize, notify_child);
    }

    std::unique_lock<std::mutex> lock(done_mtx);
    done_cv.wait(lock, [&] { return done; });
    return 0;
}

extern "C" void free_scan_tree(ScanEntryC* root) {
    if (!root) return;
    FreeCTreeContents(*root);
    delete root;
}

extern "C" int get_drives(DriveInfoC** out_drives, int* out_count) {
    wchar_t drive_strings[512] = {0};
    GetLogicalDriveStringsW(512, drive_strings);

    std::vector<DriveInfoC> list;
    wchar_t* p = drive_strings;
    while (*p) {
        std::wstring drive = p;
        UINT type = GetDriveTypeW(drive.c_str());
        if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE) {
            wchar_t vol_name[256] = {0};
            GetVolumeInformationW(drive.c_str(), vol_name, 256, NULL, NULL, NULL, NULL, 0);

            ULARGE_INTEGER free_bytes, total_bytes;
            double free_gb = 0.0, total_gb = 0.0, used_gb = 0.0;
            if (GetDiskFreeSpaceExW(drive.c_str(), &free_bytes, &total_bytes, NULL)) {
                free_gb = free_bytes.QuadPart / (1024.0 * 1024.0 * 1024.0);
                total_gb = total_bytes.QuadPart / (1024.0 * 1024.0 * 1024.0);
                used_gb = total_gb - free_gb;
            }

            DriveInfoC d{};
            d.path = _wcsdup(drive.c_str());
            d.label = _wcsdup(vol_name);
            d.total_gb = total_gb;
            d.free_gb = free_gb;
            d.used_gb = used_gb;
            list.push_back(d);
        }
        p += wcslen(p) + 1;
    }

    *out_count = static_cast<int>(list.size());
    if (list.empty()) {
        *out_drives = nullptr;
        return 0;
    }
    DriveInfoC* arr = new DriveInfoC[list.size()];
    for (size_t i = 0; i < list.size(); ++i) arr[i] = list[i];
    *out_drives = arr;
    return 0;
}

extern "C" void free_drives(DriveInfoC* drives, int count) {
    if (!drives) return;
    for (int i = 0; i < count; ++i) {
        free(const_cast<wchar_t*>(drives[i].path));
        free(const_cast<wchar_t*>(drives[i].label));
    }
    delete[] drives;
}

extern "C" int list_subdirectories(const wchar_t* path_w, DirEntryC** out_entries, int* out_count) {
    std::wstring path(path_w);
    if (!path.empty() && path.back() != L'\\') path += L'\\';
    std::wstring pattern = path + L"*";

    std::vector<DirEntryC> list;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        *out_entries = nullptr;
        *out_count = 0;
        return 0;
    }
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        if (!IsRealDir(fd)) continue;
        DirEntryC d{};
        d.name = _wcsdup(name.c_str());
        d.path = _wcsdup((path + name).c_str());
        d.is_accessible = 1;
        list.push_back(d);
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    *out_count = static_cast<int>(list.size());
    if (list.empty()) {
        *out_entries = nullptr;
        return 0;
    }
    DirEntryC* arr = new DirEntryC[list.size()];
    for (size_t i = 0; i < list.size(); ++i) arr[i] = list[i];
    *out_entries = arr;
    return 0;
}

extern "C" void free_dir_entries(DirEntryC* entries, int count) {
    if (!entries) return;
    for (int i = 0; i < count; ++i) {
        free(const_cast<wchar_t*>(entries[i].name));
        free(const_cast<wchar_t*>(entries[i].path));
    }
    delete[] entries;
}
