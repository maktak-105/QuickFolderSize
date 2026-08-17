#pragma once
// スキャンエンジンのC互換API。DLL(engine_x64.dll)としても、GUI/CLI実行ファイルへの
// 静的リンクとしても同じソースからビルドされる(QuickDiskBenchと同一方式)。

#ifdef __cplusplus
extern "C" {
#endif

// ツリーの1ノード(ファイル or ディレクトリ)。
// children は再帰的に所有される配列。free_scan_tree() でまとめて解放する。
typedef struct ScanEntryC {
    const wchar_t* name;
    const wchar_t* path;
    unsigned long long size;
    unsigned long long file_count;   // is_dir==1 のときのみ意味を持つ(再帰合計)
    long long mtime_raw;             // FILETIME を64bit整数化したもの
    int is_accessible;               // 0/1
    int is_dir;                      // 0/1
    int child_count;
    struct ScanEntryC* children;     // child_count 個の配列。file の場合は NULL
} ScanEntryC;

// on_child_done: スキャン対象ルート直下の子フォルダが1つ完了するたびに呼ばれる。
//   渡されるポインタは呼び出し中のみ有効(借用)。呼び出し元で保持してはいけない。
// on_finished: ルート全体のスキャンが完了したときに一度だけ呼ばれる。
//   所有権は呼び出し元に移る。不要になったら free_scan_tree() で解放すること
//   (次回スキャンの mtime キャッシュとして保持し続けてよい)。
typedef void (*ScanNodeCallback)(const ScanEntryC* node, void* user_data);

// root_path を並列スキャンする。prev_cache_root は直前のスキャン結果のルート
// (なければ NULL)。ディレクトリの mtime が一致する場合はそのディレクトリの
// scandir 相当処理を省略し、キャッシュされた内容を再利用する(ファイルは無条件
// 再利用、サブディレクトリは個別に mtime を再検証して再帰する)。
// stop_flag は他スレッドから非0を書き込むとキャンセルできる。
// 戻り値は 0 固定(将来のエラーコード用に予約)。呼び出し元スレッドをブロックする。
int scan_directory(
    const wchar_t* root_path,
    const ScanEntryC* prev_cache_root,
    ScanNodeCallback on_child_done,
    ScanNodeCallback on_finished,
    const int* stop_flag,
    void* user_data
);

void free_scan_tree(ScanEntryC* root);

typedef struct DriveInfoC {
    const wchar_t* path;   // 例: "C:\\"
    const wchar_t* label;  // ボリュームラベル(空文字あり)
    double total_gb;
    double free_gb;
    double used_gb;
} DriveInfoC;

int get_drives(DriveInfoC** out_drives, int* out_count);
void free_drives(DriveInfoC* drives, int count);

typedef struct DirEntryC {
    const wchar_t* name;
    const wchar_t* path;
    int is_accessible;
} DirEntryC;

// path 直下のサブディレクトリを非再帰・サイズ計算なしで列挙する(左ナビペインの
// 遅延展開用)。ジャンクション/シンボリックリンクは除外する。
int list_subdirectories(const wchar_t* path, DirEntryC** out_entries, int* out_count);
void free_dir_entries(DirEntryC* entries, int count);

#ifdef __cplusplus
}
#endif
