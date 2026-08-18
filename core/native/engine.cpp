// スキャンエンジン本体。
//
// 設計は python/scanner.py の「単一共有プール・非ブロッキングfan-out」方式を
// そのままC++へ移植したもの: 固定サイズのスレッドプールを全階層で共有し、各タスクは
// 「自分の分を処理して即返る」か「子ディレクトリをプールへ再投入して即返る」かのどちらか
// しかしない。同じプールに投入したタスクの完了を待ってブロックすることはないため、
// ツリーの深さに関わらずデッドロックしない。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
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
#include <chrono>
#include <queue>
#include <algorithm>
#include <cstdint>
#include <limits>

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
    int scan_mode = 0; // 0: Win32 directory walk, 1: NTFS MFT
    double mft_read_ms = 0.0;
    double mft_tree_ms = 0.0;
    double mft_build_ms = 0.0;
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
    out.scan_mode = b.scan_mode;
    out.mft_read_ms = b.mft_read_ms;
    out.mft_tree_ms = b.mft_tree_ms;
    out.mft_build_ms = b.mft_build_ms;
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

// ---- NTFS MFT 高速スキャン -------------------------------------------------
// NTFS ボリューム直下では、各ディレクトリを個別に開く代わりに $MFT を
// 連続して読み、FILE レコードの親参照からツリーを復元する。管理者権限が
// ない場合や、安全に解釈できない MFT では従来の Win32 列挙へ戻る。

constexpr uint64_t kInvalidMftIndex = std::numeric_limits<uint64_t>::max();
constexpr DWORD kNtfsAttributeStandardInformation = 0x10;
constexpr DWORD kNtfsAttributeFileName = 0x30;
constexpr DWORD kNtfsAttributeData = 0x80;
constexpr DWORD kNtfsAttributeEnd = 0xFFFFFFFF;

enum class MftScanResult { Success, Unavailable, Cancelled };

struct MftRun {
    uint64_t lcn = 0;
    uint64_t clusters = 0;
    bool sparse = false;
};

struct MftNode {
    std::wstring name;
    uint64_t parent = kInvalidMftIndex;
    uint64_t first_child = kInvalidMftIndex;
    uint64_t next_sibling = kInvalidMftIndex;
    unsigned long long size = 0;
    unsigned long long file_count = 0;
    long long mtime_raw = 0;
    uint16_t sequence = 0;
    bool valid = false;
    bool is_dir = false;
    bool reparse = false;
};

template <typename T>
bool ReadField(const uint8_t* data, size_t size, size_t offset, T& out) {
    if (offset > size || sizeof(T) > size - offset) return false;
    memcpy(&out, data + offset, sizeof(T));
    return true;
}

bool ApplyNtfsFixup(uint8_t* record, size_t record_size, DWORD sector_size) {
    uint16_t usa_offset = 0, usa_count = 0;
    if (!ReadField(record, record_size, 4, usa_offset) ||
        !ReadField(record, record_size, 6, usa_count) || usa_count < 2 ||
        usa_offset + static_cast<size_t>(usa_count) * 2 > record_size || sector_size < 2)
        return false;

    uint16_t marker = 0;
    memcpy(&marker, record + usa_offset, sizeof(marker));
    for (uint16_t i = 1; i < usa_count; ++i) {
        size_t tail = static_cast<size_t>(i) * sector_size - 2;
        if (tail + 2 > record_size) return false;
        uint16_t actual = 0;
        memcpy(&actual, record + tail, sizeof(actual));
        if (actual != marker) return false;
        memcpy(record + tail, record + usa_offset + static_cast<size_t>(i) * 2, 2);
    }
    return true;
}

bool ParseRunList(const uint8_t* data, size_t size, std::vector<MftRun>& out) {
    int64_t current_lcn = 0;
    size_t pos = 0;
    while (pos < size && data[pos] != 0) {
        uint8_t header = data[pos++];
        unsigned length_bytes = header & 0x0F;
        unsigned offset_bytes = header >> 4;
        if (length_bytes == 0 || length_bytes > 8 || offset_bytes > 8 ||
            pos + length_bytes + offset_bytes > size) return false;

        uint64_t clusters = 0;
        for (unsigned i = 0; i < length_bytes; ++i)
            clusters |= static_cast<uint64_t>(data[pos + i]) << (i * 8);
        pos += length_bytes;
        if (clusters == 0) return false;

        MftRun run;
        run.clusters = clusters;
        run.sparse = (offset_bytes == 0);
        if (!run.sparse) {
            uint64_t raw_delta = 0;
            for (unsigned i = 0; i < offset_bytes; ++i)
                raw_delta |= static_cast<uint64_t>(data[pos + i]) << (i * 8);
            if (offset_bytes < 8 && (data[pos + offset_bytes - 1] & 0x80))
                raw_delta |= (~uint64_t{0}) << (offset_bytes * 8);
            int64_t delta = static_cast<int64_t>(raw_delta);
            if ((delta < 0 && current_lcn < -delta) ||
                (delta > 0 && current_lcn > std::numeric_limits<int64_t>::max() - delta)) return false;
            current_lcn += delta;
            if (current_lcn < 0) return false;
            run.lcn = static_cast<uint64_t>(current_lcn);
        }
        pos += offset_bytes;
        out.push_back(run);
    }
    return !out.empty();
}

bool ReadVolumeAt(HANDLE volume, uint64_t offset, void* buffer, DWORD bytes) {
    LARGE_INTEGER where;
    where.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(volume, where, nullptr, FILE_BEGIN)) return false;
    uint8_t* dst = static_cast<uint8_t*>(buffer);
    DWORD total = 0;
    while (total < bytes) {
        DWORD got = 0;
        if (!ReadFile(volume, dst + total, bytes - total, &got, nullptr) || got == 0) return false;
        total += got;
    }
    return true;
}

int FileNameNamespacePriority(uint8_t ns) {
    if (ns == 3) return 3; // Win32 + DOS
    if (ns == 1) return 2; // Win32
    if (ns == 0) return 1; // POSIX
    return 0;              // DOS 8.3 alias
}

bool ParseMftRecord(uint8_t* record, size_t record_size, DWORD sector_size,
                    uint64_t record_index, MftNode& out) {
    if (record_size < 48 || memcmp(record, "FILE", 4) != 0 ||
        !ApplyNtfsFixup(record, record_size, sector_size)) return false;

    uint16_t sequence = 0, first_attribute = 0, flags = 0;
    uint32_t bytes_in_use = 0;
    uint64_t base_record = 0;
    if (!ReadField(record, record_size, 16, sequence) ||
        !ReadField(record, record_size, 20, first_attribute) ||
        !ReadField(record, record_size, 22, flags) ||
        !ReadField(record, record_size, 24, bytes_in_use) ||
        !ReadField(record, record_size, 32, base_record) ||
        !(flags & 0x0001) || base_record != 0 || first_attribute >= record_size) return false;

    out.sequence = sequence;
    out.is_dir = (flags & 0x0002) != 0;
    int best_name_priority = -1;
    size_t limit = std::min<size_t>(bytes_in_use, record_size);

    for (size_t pos = first_attribute; pos + 16 <= limit;) {
        uint32_t type = 0, attr_length = 0;
        if (!ReadField(record, limit, pos, type) || type == kNtfsAttributeEnd) break;
        if (!ReadField(record, limit, pos + 4, attr_length) ||
            attr_length < 16 || attr_length > limit - pos) return false;

        uint8_t non_resident = record[pos + 8];
        uint8_t attr_name_length = record[pos + 9];
        if (type == kNtfsAttributeStandardInformation && !non_resident) {
            uint32_t value_length = 0;
            uint16_t value_offset = 0;
            if (ReadField(record, limit, pos + 16, value_length) &&
                ReadField(record, limit, pos + 20, value_offset) &&
                value_offset <= attr_length && value_length <= attr_length - value_offset) {
                const uint8_t* value = record + pos + value_offset;
                if (value_length >= 24) memcpy(&out.mtime_raw, value + 16, 8);
                uint32_t file_attributes = 0;
                if (value_length >= 36) memcpy(&file_attributes, value + 32, 4);
                out.reparse = (file_attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
            }
        } else if (type == kNtfsAttributeFileName && !non_resident) {
            uint32_t value_length = 0;
            uint16_t value_offset = 0;
            if (ReadField(record, limit, pos + 16, value_length) &&
                ReadField(record, limit, pos + 20, value_offset) &&
                value_offset <= attr_length && value_length <= attr_length - value_offset &&
                value_length >= 66) {
                const uint8_t* value = record + pos + value_offset;
                uint8_t name_length = value[64];
                uint8_t name_namespace = value[65];
                size_t name_bytes = static_cast<size_t>(name_length) * sizeof(wchar_t);
                int priority = FileNameNamespacePriority(name_namespace);
                if (66 + name_bytes <= value_length && priority > best_name_priority) {
                    uint64_t parent_ref = 0;
                    memcpy(&parent_ref, value, 8);
                    out.parent = parent_ref & 0x0000FFFFFFFFFFFFULL;
                    out.name.assign(reinterpret_cast<const wchar_t*>(value + 66), name_length);
                    uint32_t file_attributes = 0;
                    memcpy(&file_attributes, value + 56, 4);
                    out.reparse = out.reparse ||
                                  ((file_attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0);
                    best_name_priority = priority;
                }
            }
        } else if (type == kNtfsAttributeData && attr_name_length == 0 && !out.is_dir) {
            if (!non_resident) {
                uint32_t value_length = 0;
                if (ReadField(record, limit, pos + 16, value_length)) out.size = value_length;
            } else {
                uint64_t lowest_vcn = 1, data_size = 0;
                if (ReadField(record, limit, pos + 16, lowest_vcn) && lowest_vcn == 0 &&
                    ReadField(record, limit, pos + 48, data_size)) out.size = data_size;
            }
        }
        pos += attr_length;
    }

    if (record_index == 5) {
        out.valid = true;
        out.is_dir = true;
    } else {
        out.valid = best_name_priority >= 0 && !out.name.empty();
    }
    return out.valid;
}

bool ExtractMftLayout(uint8_t* record, size_t record_size, DWORD sector_size,
                      std::vector<MftRun>& runs, uint64_t& data_size) {
    if (record_size < 48 || memcmp(record, "FILE", 4) != 0 ||
        !ApplyNtfsFixup(record, record_size, sector_size)) return false;
    uint16_t first_attribute = 0;
    uint32_t bytes_in_use = 0;
    if (!ReadField(record, record_size, 20, first_attribute) ||
        !ReadField(record, record_size, 24, bytes_in_use)) return false;
    size_t limit = std::min<size_t>(bytes_in_use, record_size);
    for (size_t pos = first_attribute; pos + 64 <= limit;) {
        uint32_t type = 0, attr_length = 0;
        if (!ReadField(record, limit, pos, type) || type == kNtfsAttributeEnd) break;
        if (!ReadField(record, limit, pos + 4, attr_length) ||
            attr_length < 16 || attr_length > limit - pos) return false;
        uint8_t non_resident = record[pos + 8];
        uint8_t name_length = record[pos + 9];
        if (type == kNtfsAttributeData && non_resident && name_length == 0) {
            uint64_t lowest_vcn = 1, highest_vcn = 0;
            uint16_t run_offset = 0;
            if (!ReadField(record, limit, pos + 16, lowest_vcn) || lowest_vcn != 0 ||
                !ReadField(record, limit, pos + 24, highest_vcn) ||
                !ReadField(record, limit, pos + 32, run_offset) ||
                !ReadField(record, limit, pos + 48, data_size) || run_offset >= attr_length ||
                !ParseRunList(record + pos + run_offset, attr_length - run_offset, runs)) return false;
            uint64_t covered_clusters = 0;
            for (const auto& run : runs) covered_clusters += run.clusters;
            // $ATTRIBUTE_LIST に続きがある複雑な MFT は誤読しない。
            return covered_clusters == highest_vcn + 1 && data_size > 0;
        }
        pos += attr_length;
    }
    return false;
}

bool IsNtfsVolumeRoot(const std::wstring& path, std::wstring& drive_root,
                      std::wstring& volume_path) {
    if (path.size() < 2 || path[1] != L':' || path.size() > 3 ||
        (path.size() == 3 && path[2] != L'\\' && path[2] != L'/')) return false;
    drive_root = path.substr(0, 2) + L"\\";
    wchar_t fs_name[32] = {0};
    if (!GetVolumeInformationW(drive_root.c_str(), nullptr, 0, nullptr, nullptr, nullptr,
                               fs_name, _countof(fs_name)) || _wcsicmp(fs_name, L"NTFS") != 0)
        return false;
    volume_path = L"\\\\.\\" + path.substr(0, 2);
    return true;
}

bool BuildEntryFromMft(uint64_t index, const std::wstring& path,
                       const std::vector<MftNode>& nodes, EntryBuilder& out,
                       const int* stop_flag, bool include_paths, size_t depth = 0) {
    if (index >= nodes.size() || depth > 32768 || (stop_flag && *stop_flag != 0)) return false;
    const MftNode& src = nodes[index];
    out.name = src.name;
    out.path = include_paths ? path : L"";
    out.size = src.size;
    out.file_count = src.file_count;
    out.mtime_raw = src.mtime_raw;
    out.is_dir = src.is_dir;
    if (!src.is_dir) return true;

    size_t child_count = 0;
    for (uint64_t child = src.first_child; child != kInvalidMftIndex;
         child = nodes[child].next_sibling) ++child_count;
    out.children.reserve(child_count);
    // 表示側(JS)が現在のソート列に応じて並べ替えるため、MFT経路では
    // ここで全ディレクトリをサイズソートしない。
    for (uint64_t child = src.first_child; child != kInvalidMftIndex;
         child = nodes[child].next_sibling) {
        std::wstring child_path;
        if (include_paths) {
            child_path = path;
            if (!child_path.empty() && child_path.back() != L'\\') child_path += L'\\';
            child_path += nodes[child].name;
        }
        EntryBuilder entry;
        if (!BuildEntryFromMft(child, child_path, nodes, entry, stop_flag, include_paths, depth + 1)) return false;
        out.children.push_back(std::move(entry));
    }
    return true;
}

MftScanResult TryScanNtfsMft(const std::wstring& requested_path, const int* stop_flag,
                             EntryBuilder& root) {
    auto mft_start = std::chrono::steady_clock::now();
    std::wstring drive_root, volume_path;
    if (!IsNtfsVolumeRoot(requested_path, drive_root, volume_path))
        return MftScanResult::Unavailable;

    HANDLE volume = CreateFileW(volume_path.c_str(), GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (volume == INVALID_HANDLE_VALUE) return MftScanResult::Unavailable;

    NTFS_VOLUME_DATA_BUFFER volume_data{};
    DWORD returned = 0;
    if (!DeviceIoControl(volume, FSCTL_GET_NTFS_VOLUME_DATA, nullptr, 0,
                         &volume_data, sizeof(volume_data), &returned, nullptr) ||
        volume_data.BytesPerCluster == 0 || volume_data.BytesPerFileRecordSegment == 0 ||
        volume_data.BytesPerFileRecordSegment > 1024 * 1024) {
        CloseHandle(volume);
        return MftScanResult::Unavailable;
    }

    const DWORD record_size = volume_data.BytesPerFileRecordSegment;
    const DWORD sector_size = volume_data.BytesPerSector;
    const uint64_t cluster_size = volume_data.BytesPerCluster;
    uint8_t* first_record = static_cast<uint8_t*>(VirtualAlloc(nullptr, record_size,
                                                               MEM_COMMIT | MEM_RESERVE,
                                                               PAGE_READWRITE));
    if (!first_record) {
        CloseHandle(volume);
        return MftScanResult::Unavailable;
    }
    uint64_t mft_offset = static_cast<uint64_t>(volume_data.MftStartLcn.QuadPart) * cluster_size;
    if (!ReadVolumeAt(volume, mft_offset, first_record, record_size)) {
        VirtualFree(first_record, 0, MEM_RELEASE);
        CloseHandle(volume);
        return MftScanResult::Unavailable;
    }

    std::vector<MftRun> runs;
    uint64_t mft_data_size = 0;
    bool layout_ok = ExtractMftLayout(first_record, record_size, sector_size, runs, mft_data_size);
    VirtualFree(first_record, 0, MEM_RELEASE);
    if (!layout_ok ||
        mft_data_size / record_size > 100000000ULL) {
        CloseHandle(volume);
        return MftScanResult::Unavailable;
    }

    size_t record_count = static_cast<size_t>(mft_data_size / record_size);
    std::vector<MftNode> nodes(record_count);
    constexpr DWORD kReadChunk = 8 * 1024 * 1024;
    uint8_t* io_buffer = static_cast<uint8_t*>(VirtualAlloc(nullptr, kReadChunk,
                                                            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!io_buffer) {
        CloseHandle(volume);
        return MftScanResult::Unavailable;
    }

    auto read_start = std::chrono::steady_clock::now();
    uint64_t logical_offset = 0;
    bool read_ok = true;
    for (const auto& run : runs) {
        uint64_t run_bytes = run.clusters * cluster_size;
        for (uint64_t within = 0; within < run_bytes && logical_offset < mft_data_size;) {
            if (stop_flag && *stop_flag != 0) {
                VirtualFree(io_buffer, 0, MEM_RELEASE);
                CloseHandle(volume);
                return MftScanResult::Cancelled;
            }
            uint64_t remaining = std::min(run_bytes - within, mft_data_size - logical_offset);
            DWORD bytes = static_cast<DWORD>(std::min<uint64_t>(remaining, kReadChunk));
            if (bytes % record_size != 0) bytes -= bytes % record_size;
            if (bytes == 0 || run.sparse ||
                !ReadVolumeAt(volume, run.lcn * cluster_size + within, io_buffer, bytes)) {
                read_ok = false;
                break;
            }
            size_t first_index = static_cast<size_t>(logical_offset / record_size);
            size_t records = bytes / record_size;
            for (size_t i = 0; i < records && first_index + i < nodes.size(); ++i) {
                ParseMftRecord(io_buffer + i * record_size, record_size, sector_size,
                               first_index + i, nodes[first_index + i]);
            }
            within += bytes;
            logical_offset += bytes;
        }
        if (!read_ok || logical_offset >= mft_data_size) break;
    }
    VirtualFree(io_buffer, 0, MEM_RELEASE);
    CloseHandle(volume);
    double mft_read_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - read_start).count();
    if (!read_ok || logical_offset < mft_data_size || nodes.size() <= 5 || !nodes[5].valid)
        return MftScanResult::Unavailable;

    auto tree_start = std::chrono::steady_clock::now();
    nodes[5].name = BaseName(drive_root);
    for (uint64_t i = 0; i < nodes.size(); ++i) {
        MftNode& node = nodes[i];
        if (i == 5 || !node.valid || node.reparse || node.parent >= nodes.size()) continue;
        MftNode& parent = nodes[node.parent];
        if (!parent.valid || !parent.is_dir || parent.reparse || node.parent == i) continue;
        node.next_sibling = parent.first_child;
        parent.first_child = i;
    }

    // ルートから到達できる部分だけを後順で集計する。
    std::vector<uint8_t> state(nodes.size(), 0);
    std::vector<std::pair<uint64_t, bool>> stack;
    stack.emplace_back(5, false);
    while (!stack.empty()) {
        auto [index, exiting] = stack.back();
        stack.pop_back();
        if (index >= nodes.size()) continue;
        if (exiting) {
            MftNode& node = nodes[index];
            if (node.is_dir) {
                node.size = 0;
                node.file_count = 0;
                for (uint64_t child = node.first_child; child != kInvalidMftIndex;
                     child = nodes[child].next_sibling) {
                    if (state[child] == 2) {
                        node.size += nodes[child].size;
                        node.file_count += nodes[child].file_count;
                    }
                }
            } else {
                node.file_count = 1;
            }
            state[index] = 2;
            continue;
        }
        if (state[index] != 0) continue;
        state[index] = 1;
        stack.emplace_back(index, true);
        if (nodes[index].is_dir) {
            for (uint64_t child = nodes[index].first_child; child != kInvalidMftIndex;
                 child = nodes[child].next_sibling) {
                if (state[child] == 0) stack.emplace_back(child, false);
            }
        }
    }
    double mft_tree_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - tree_start).count();

    auto build_start = std::chrono::steady_clock::now();
    root = EntryBuilder{};
    // MFT JSONでは絶対パスを送らずJS側で復元するため、ネイティブ側でも
    // 125万件分のフルパス文字列を生成・保持しない。
    if (!BuildEntryFromMft(5, drive_root, nodes, root, stop_flag, false))
        return (stop_flag && *stop_flag != 0) ? MftScanResult::Cancelled : MftScanResult::Unavailable;
    root.scan_mode = 1;
    root.mft_read_ms = mft_read_ms;
    root.mft_tree_ms = mft_tree_ms;
    root.mft_build_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - build_start).count();
    return MftScanResult::Success;
}

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

    // NTFS ボリューム直下は、可能ならディレクトリ巡回前に MFT 高速経路を使う。
    // 高速経路では巨大な部分木を進捗通知と完了通知で二重コピーしない。
    EntryBuilder mft_root;
    MftScanResult mft_result = TryScanNtfsMft(root_path, stop_flag, mft_root);
    if (mft_result == MftScanResult::Success || mft_result == MftScanResult::Cancelled) {
        if (mft_result == MftScanResult::Cancelled) {
            mft_root = EntryBuilder{};
            mft_root.path = root_path;
            mft_root.name = BaseName(root_path);
        }
        ScanEntryC* out = new ScanEntryC();
        FillCTree(mft_root, *out);
        if (on_finished) on_finished(out, user_data);
        return 0;
    }

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
