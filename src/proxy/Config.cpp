// Config.cpp - ini 設定の遅延読込・キャッシュ
#include "Config.h"
#include "InputTransform.h"
#include <string>
#include <atomic>

static XFireConfig g_cfg;
static SRWLOCK g_loadLock = SRWLOCK_INIT;
static std::atomic<int> g_loaded{0};

void Config::ApplyDefaults(XFireConfig& cfg) {
    // ヘッダのメンバ初期値を唯一のデフォルト定義源とし、ここでは再適用のみ行う。
    cfg = XFireConfig{};
}

// ゲームプロセスの exe があるディレクトリ(末尾バックスラッシュ付き)
static std::wstring ExeDir() {
    wchar_t path[MAX_PATH] = {0};
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (n == 0) return L"";
    std::wstring p(path, n);
    size_t pos = p.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? p.substr(0, pos + 1) : L"";
}

void Config::LoadOnce() {
    if (g_loaded.load(std::memory_order_acquire)) return;
    AcquireSRWLockExclusive(&g_loadLock);
    if (!g_loaded.load(std::memory_order_relaxed)) {
        ApplyDefaults(g_cfg);
        std::wstring ini = ExeDir() + L"XInputXFire.ini";
        if (GetFileAttributesW(ini.c_str()) != INVALID_FILE_ATTRIBUTES) {
            const wchar_t* sec = L"XFire";
            // 範囲クランプ(値域は Config.h の kOnMsMin/kOnMsMax/kFirstOnMsMin/kByteMax が唯一ソース)。
            auto clampDw = [](INT v, INT lo, INT hi) -> DWORD {
                if (v < lo) v = lo; if (v > hi) v = hi; return (DWORD)v;
            };
            auto clampByte = [](INT v) -> BYTE {
                if (v < 0) v = 0; if (v > (INT)Config::kByteMax) v = (INT)Config::kByteMax; return (BYTE)v;
            };
            g_cfg.onMs             = clampDw(GetPrivateProfileIntW(sec, L"OnMs",            (INT)g_cfg.onMs,             ini.c_str()), (INT)Config::kOnMsMin, (INT)Config::kOnMsMax);
            g_cfg.offMs            = clampDw(GetPrivateProfileIntW(sec, L"OffMs",           (INT)g_cfg.offMs,            ini.c_str()), (INT)Config::kOnMsMin, (INT)Config::kOnMsMax);
            // FirstOnMs は 0=無効を許可([kFirstOnMsMin,kOnMsMax])。0 のとき最初のON区間は OnMs と同値。
            g_cfg.firstOnMs        = clampDw(GetPrivateProfileIntW(sec, L"FirstOnMs",       (INT)g_cfg.firstOnMs,        ini.c_str()), (INT)Config::kFirstOnMsMin, (INT)Config::kOnMsMax);
            g_cfg.triggerThreshold = clampByte(GetPrivateProfileIntW(sec, L"TriggerThreshold", (INT)g_cfg.triggerThreshold, ini.c_str()));
            g_cfg.hysteresisLow    = clampByte(GetPrivateProfileIntW(sec, L"HysteresisLow",    (INT)g_cfg.hysteresisLow,    ini.c_str()));
            // EnableL2/R2 は "1"/"0" で記録("true"/"false" は GetPrivateProfileInt で 0 になるため非推奨)
            g_cfg.enableL2         = GetPrivateProfileIntW(sec, L"EnableL2", g_cfg.enableL2 ? 1 : 0, ini.c_str()) != 0;
            g_cfg.enableR2         = GetPrivateProfileIntW(sec, L"EnableR2", g_cfg.enableR2 ? 1 : 0, ini.c_str()) != 0;
            wchar_t buf[256] = {0};
            GetPrivateProfileStringW(sec, L"TargetButtons", L"", buf, 256, ini.c_str());
            WORD parsed = InputTransform::ParseTargetButtons(buf);
            // 空指定(0)はデフォルト維持。明示的に無効化したい場合は TargetButtons= を空にする運用は非対応。
            if (parsed != 0) g_cfg.targetButtons = parsed;

            // --- 連射マスタートグル設定 ---
            // ToggleButtons: |区切り名前(LB|A 等)。ParseTargetButtons/FormatButtons でビットマスクと相互変換。
            //   デフォルト文字列はヘッダのビットマスク既定(toggleButtons)から FormatButtons で導出し、
            //   「キー不在」=既定、「キー存在・値空」(ToggleButtons=)=トグル無効化(常時ON) を区別する
            //   (GetPrivateProfileStringW はキー不在時デフォルト、キー空時は空文字列を返す)。
            wchar_t tdef[256] = {0};
            InputTransform::FormatButtons(g_cfg.toggleButtons, tdef, 256); // 直前 ApplyDefaults で LB|A 既定が設定済み
            wchar_t tbuf[256] = {0};
            GetPrivateProfileStringW(sec, L"ToggleButtons", tdef, tbuf, 256, ini.c_str());
            if (tbuf[0] == L'\0') {
                g_cfg.toggleButtons = 0; // 明示的無効化(常時ON運用)
            } else {
                WORD tparsed = InputTransform::ParseTargetButtons(tbuf);
                if (tparsed != 0) g_cfg.toggleButtons = tparsed;
            }
            g_cfg.defaultEnabled  = GetPrivateProfileIntW(sec, L"DefaultEnabled",  g_cfg.defaultEnabled  ? 1 : 0, ini.c_str()) != 0;
            g_cfg.announceEnabled = GetPrivateProfileIntW(sec, L"AnnounceEnabled", g_cfg.announceEnabled ? 1 : 0, ini.c_str()) != 0;
            // 起動音(1=再生 / 0=無効)。DLL 埋め込み WAVE リソースを PlaySound で再生。
            g_cfg.startupSound = GetPrivateProfileIntW(sec, L"StartupSound", g_cfg.startupSound ? 1 : 0, ini.c_str()) != 0;
        }
        g_loaded.store(1, std::memory_order_release); // g_cfg 書き込み完了を可視化
    }
    ReleaseSRWLockExclusive(&g_loadLock);
}

const XFireConfig& Config::Get() {
    return g_cfg;
}

#ifdef XFIRE_TEST
void Config::SetForTest(const XFireConfig& cfg) {
    AcquireSRWLockExclusive(&g_loadLock);
    g_cfg = cfg;
    g_loaded.store(1, std::memory_order_release);
    ReleaseSRWLockExclusive(&g_loadLock);
}
#endif