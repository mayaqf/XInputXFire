// RealXInputLoader.cpp - 本物システムDLLの遅延ロード
// 初回エクスポート呼出時に1回だけ(DllMainでは呼ばない=ローダーロック回避)。
//
// プロキシ自身のモジュール名でロード先システムDLLを切り替える。
// 同一バイナリを xinput1_3.dll / xinput1_4.dll / XInput9_1_0.dll のいずれの名前で
// 配置しても、それぞれ対応する System32 の本物DLL をロードする。
//   xinput1_3  → System32\xinput1_3.dll (非存在時 1.4 へフォールバック)
//   xinput1_4  → System32\xinput1_4.dll
//   xinput9_1_0→ System32\XInput9_1_0.dll (Vista 以降必須、フォールバック不要)
//   未知       → 1.3 → 1.4 → 9.1.0 (9.1.0 より 1.4 を優先: GetAudioDeviceIds 実装のため)
//
// 【重要】System32 のフルパスを構築してロードする。
// 単に LoadLibraryExW(L"xinput1_3.dll", LOAD_LIBRARY_SEARCH_SYSTEM32) では、
// プロキシ自身(同名 xinput1_3.dll)が既にプロセスにロード済みの場合、
// フラグに関わらず既存モジュール(=プロキシ自身)のハンドルが返される
// (LoadLibrary は名前ベースで既存モジュールを再利用する仕様)。
// その結果 GetProcAddress がプロキシ自身の XInputGetState を取得し、
// 呼出が無限再帰→スタックオーバーフローする。
// フルパス指定なら正規化パスで既存モジュールを判定するためプロキシ自身と区別され、
// System32 の本物DLLのみがロードされる。
#include "XInputProxy.h"
#include "DiagLog.h"
#include <atomic>
#include <cstdio>

static RealXInput g_real = {};
static SRWLOCK    g_loadLock = SRWLOCK_INIT;
static std::atomic<int> g_loaded{0};

// プロキシ自身のDLLファイル名(basename・小文字化)を取得。
// GetModuleHandleExW(FROM_ADDRESS) にこのファイル内の関数アドレスを渡して
// 自身のモジュールを特定する(GetModuleFileNameW(NULL) は exe パスになるため不可)。
static void GetSelfBaseName(wchar_t* out, size_t outCount) {
    HMODULE hSelf = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCWSTR)&GetSelfBaseName, &hSelf);
    if (!hSelf || outCount == 0) { if (outCount) out[0] = L'\0'; return; }

    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(hSelf, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) { out[0] = L'\0'; return; }

    // basename(最後の \ 以降)
    const wchar_t* base = path;
    for (const wchar_t* p = path; *p; ++p) {
        if (*p == L'\\' || *p == L'/') base = p + 1;
    }
    // 小文字化コピー
    size_t i = 0;
    for (; base[i] && i + 1 < outCount; ++i) {
        wchar_t c = base[i];
        if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
        out[i] = c;
    }
    out[i] = L'\0';
}

// System32 のフルパスを構築して本物DLLをロード。
// 存在しない(非対応ver)場合は nullptr を返し、呼び出し側でフォールバックする。
static HMODULE LoadSystem(const wchar_t* name) {
    wchar_t dir[MAX_PATH];
    UINT len = GetSystemDirectoryW(dir, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return nullptr; // 取得失敗時はフォールバック不可

    // dir + "\\" + name を手動連結(stdio 依存を避ける)
    size_t dl = wcslen(dir);
    size_t nl = wcslen(name);
    if (dl + 1 + nl + 1 > MAX_PATH) return nullptr;
    wchar_t path[MAX_PATH];
    memcpy(path, dir, dl * sizeof(wchar_t));
    path[dl] = L'\\';
    memcpy(path + dl + 1, name, nl * sizeof(wchar_t));
    path[dl + 1 + nl] = L'\0';

    // 存在確認(System32 に非存在verのDLLは無い → ロード試行を省き確実にフォールバック)。
    // INVALID_FILE_ATTRIBUTES はアクセス拒否等でも返るため、ファイル未存在以外はログに残す。
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        DWORD err = GetLastError();
        if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND) {
            DiagLog::Log("[LOADER] GetFileAttributes failed on system DLL path (non file-not-found)");
        }
        return nullptr;
    }

    // フルパス指定: 依存DLL解決は既定の検索パス(System32 等)で行われる。
    // xinput*.dll の依存はシステムDLLのみなので LOAD_WITH_ALTERED_SEARCH_PATH 等は不要。
    HMODULE h = LoadLibraryExW(path, nullptr, 0);
    if (!h) {
        // ファイルは存在してもロード失敗するケース(依存DLL欠落・壊れたイメージ・
        // アーキテクチャ不一致・アクセス拒否)。フォールバック連鎖へ進むが診断のためログ。
        char msg[160];
        sprintf_s(msg, sizeof(msg), "[LOADER] LoadLibraryExW failed err=%lu", GetLastError());
        DiagLog::Log(msg);
    }
    return h;
}

bool RealXInputLoader::LoadOnce() {
    if (g_loaded.load(std::memory_order_acquire)) return g_real.hDll != nullptr;
    AcquireSRWLockExclusive(&g_loadLock);
    if (!g_loaded.load(std::memory_order_relaxed)) {
        // プロキシ自身の名前でロード先システムDLLを選択
        wchar_t self[64];
        GetSelfBaseName(self, 64);
        if (self[0] == L'\0') DiagLog::Log("[LOADER] self module name unknown -> fallback chain 1.3->1.4->9.1.0");
        HMODULE h = nullptr;
        if (wcsstr(self, L"xinput9_1_0")) {
            h = LoadSystem(L"XInput9_1_0.dll");
        } else if (wcsstr(self, L"xinput1_4")) {
            h = LoadSystem(L"xinput1_4.dll");
        } else { // xinput1_3 or 未知 → 1.3 優先、非存在時は 1.4 を優先フォールバック。
            // 【重要】某14 は XInputGetAudioDeviceIds を大量に呼び(実測4767回)コントローラ↔HID
            // 紐付けに利用。GetAudioDeviceIds をエクスポートするのは xinput1_4 のみ
            // (XInput9_1_0 は未実装 → NOT_SUPPORTED 連発で HID 読み取りが始まらない)。
            // よって 9.1.0 より 1.4 を優先する。9.1.0 は最終保険。
            // 【実環境】Win8+ の System32 には xinput1_3.dll は存在せず、実質常に 1.4 が
            // ロードされる(1.4 は序数2-8 を 1.3 と同一関数でエクスポートするため序数整合も保たれる)。
            // 古い環境(DirectX 入れで 1.3 実在)では 1.3 が選ばれるが、その場合 GetAudioDeviceIds
            // は NULL となり NOT_SUPPORTED が返る(某14 は代替経路で動くことを実機確認済み想定)。
            h = LoadSystem(L"xinput1_3.dll");
            if (!h) h = LoadSystem(L"xinput1_4.dll");
            if (!h) h = LoadSystem(L"XInput9_1_0.dll");
        }
        if (h) {
            g_real.hDll = h;
            g_real.GetState                  = (PFN_XInputGetState)GetProcAddress(h, "XInputGetState");
            g_real.SetState                  = (PFN_XInputSetState)GetProcAddress(h, "XInputSetState");
            g_real.GetCapabilities           = (PFN_XInputGetCapabilities)GetProcAddress(h, "XInputGetCapabilities");
            g_real.Enable                    = (PFN_XInputEnable)GetProcAddress(h, "XInputEnable");
            g_real.GetKeystroke              = (PFN_XInputGetKeystroke)GetProcAddress(h, "XInputGetKeystroke");
            g_real.GetBatteryInformation     = (PFN_XInputGetBatteryInformation)GetProcAddress(h, "XInputGetBatteryInformation");
            g_real.GetDSoundAudioDeviceGuids = (PFN_XInputGetDSoundAudioDeviceGuids)GetProcAddress(h, "XInputGetDSoundAudioDeviceGuids");
            g_real.GetAudioDeviceIds         = (PFN_XInputGetAudioDeviceIds)GetProcAddress(h, "XInputGetAudioDeviceIds");
            // 必須エクスポートが欠けていればログ(GetAudioDeviceIds/GetDSoundAudioDeviceGuids は
            // バージョン依存で optional なので対象外)。ゲームは exports.cpp で ERROR_DEVICE_NOT_CONNECTED
            // 等を受け取るが、ログで「コントローラ未接続」とプロキシ束縛失敗を区別できるようにする。
            if (!g_real.GetState || !g_real.SetState || !g_real.GetCapabilities) {
                char msg[160];
                sprintf_s(msg, sizeof(msg),
                    "[LOADER] essential export missing (GetState=%p SetState=%p Cap=%p)",
                    (void*)g_real.GetState, (void*)g_real.SetState, (void*)g_real.GetCapabilities);
                DiagLog::Log(msg);
            }
        }
        g_loaded.store(1, std::memory_order_release); // g_real 書き込み完了を可視化
    }
    ReleaseSRWLockExclusive(&g_loadLock);
    return g_real.hDll != nullptr;
}

const RealXInput& RealXInputLoader::Get() {
    return g_real;
}