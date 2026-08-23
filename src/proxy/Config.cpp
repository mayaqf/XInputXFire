// Config.cpp - ini 設定の遅延読込・キャッシュ
#include "Config.h"
#include "InputTransform.h"
#include "DiagLog.h"
#include <string>
#include <atomic>
#include <cwchar>
#include <cstdio>

static XFireConfig g_cfg;
static SRWLOCK g_loadLock = SRWLOCK_INIT;
static std::atomic<int> g_loaded{0};
// テスト専用: LoadOnce で読む ini パス注入(nullptr=exe同梱の既定)。
static const wchar_t* g_iniPathOverride = nullptr;

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
        std::wstring ini = g_iniPathOverride ? std::wstring(g_iniPathOverride) : (ExeDir() + L"XInputXFire.ini");
        if (GetFileAttributesW(ini.c_str()) != INVALID_FILE_ATTRIBUTES) {
            const wchar_t* sec = L"XFire";
            // 数値項目は文字列で読み wcstol で解析:空(キー不在/空)=既定、非数値=既定+ログ、
            // 範囲外=クランプ+ログ。従来は GetPrivateProfileIntW で非数値/空が 0 -> クランプ最小に
            // 黙って変換されユーザ意図が消失していた(TriggerThreshold= 空が即トリガ発動等)。
            auto readClampedDw = [sec, &ini](const wchar_t* key, const char* label,
                                            DWORD defv, INT lo, INT hi) -> DWORD {
                wchar_t buf[64] = {0};
                GetPrivateProfileStringW(sec, key, L"", buf, 64, ini.c_str());
                if (buf[0] == L'\0') return defv; // キー不在/空 -> 既定
                wchar_t* end = nullptr;
                long v = wcstol(buf, &end, 10);
                if (end == buf) { // 非数値
                    char msg[128];
                    sprintf_s(msg, sizeof(msg), "[CONFIG] %s: non-numeric value -> default", label);
                    DiagLog::Log(msg);
                    return defv;
                }
                if (v < lo) {
                    char msg[128];
                    sprintf_s(msg, sizeof(msg), "[CONFIG] %s=%ld out of range -> clamped to %d", label, v, lo);
                    DiagLog::Log(msg);
                    v = lo;
                } else if (v > hi) {
                    char msg[128];
                    sprintf_s(msg, sizeof(msg), "[CONFIG] %s=%ld out of range -> clamped to %d", label, v, hi);
                    DiagLog::Log(msg);
                    v = hi;
                }
                return (DWORD)v;
            };
            auto readClampedByte = [&](const wchar_t* key, const char* label, BYTE defv) -> BYTE {
                return (BYTE)readClampedDw(key, label, defv, 0, (INT)Config::kByteMax);
            };
            // bool 項目:1/0/true/false/yes/no/on/off を受理。空(キー不在/空)=既定、
            // 不明値=既定+ログ。従来は GetPrivateProfileIntW で "true"/"yes" 等が 0(無効)に黙って変換
            // されていた(EnableL2=true で L2 連射が黙って OFF になる等)。
            auto readBool = [sec, &ini](const wchar_t* key, const char* label, bool defv) -> bool {
                wchar_t b[32] = {0};
                GetPrivateProfileStringW(sec, key, L"", b, 32, ini.c_str());
                if (b[0] == L'\0') return defv;
                if (_wcsicmp(b, L"1") == 0 || _wcsicmp(b, L"true") == 0
                    || _wcsicmp(b, L"yes") == 0 || _wcsicmp(b, L"on") == 0) return true;
                if (_wcsicmp(b, L"0") == 0 || _wcsicmp(b, L"false") == 0
                    || _wcsicmp(b, L"no") == 0 || _wcsicmp(b, L"off") == 0) return false;
                char msg[128];
                sprintf_s(msg, sizeof(msg), "[CONFIG] %s: non-boolean value -> default", label);
                DiagLog::Log(msg);
                return defv;
            };
            g_cfg.onMs             = readClampedDw(L"OnMs",            "OnMs",            g_cfg.onMs,             (INT)Config::kOnMsMin, (INT)Config::kOnMsMax);
            g_cfg.offMs            = readClampedDw(L"OffMs",           "OffMs",           g_cfg.offMs,            (INT)Config::kOnMsMin, (INT)Config::kOnMsMax);
            // FirstOnMs は 0=無効を許可([kFirstOnMsMin,kOnMsMax])。0 のとき最初のON区間は OnMs と同値。
            g_cfg.firstOnMs        = readClampedDw(L"FirstOnMs",       "FirstOnMs",       g_cfg.firstOnMs,        (INT)Config::kFirstOnMsMin, (INT)Config::kOnMsMax);
            g_cfg.triggerThreshold = readClampedByte(L"TriggerThreshold", "TriggerThreshold", g_cfg.triggerThreshold);
            g_cfg.hysteresisLow    = readClampedByte(L"HysteresisLow",    "HysteresisLow",    g_cfg.hysteresisLow);
            // EnableL2/R2 は 1/0/true/false/yes/no/on/off で記録(不明値=既定+ログ)。
            g_cfg.enableL2         = readBool(L"EnableL2", "EnableL2", g_cfg.enableL2);
            g_cfg.enableR2         = readBool(L"EnableR2", "EnableR2", g_cfg.enableR2);
            wchar_t buf[256] = {0};
            GetPrivateProfileStringW(sec, L"TargetButtons", L"", buf, 256, ini.c_str());
            int unk = 0;
            WORD parsed = InputTransform::ParseTargetButtons(buf, &unk);
            if (unk > 0) DiagLog::Log("[CONFIG] TargetButtons: unknown token(s) ignored");
            // 空指定(0)はデフォルト維持。TargetButtons を完全に無効化(0)することは非対応。
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
                int tunk = 0;
                WORD tparsed = InputTransform::ParseTargetButtons(tbuf, &tunk);
                if (tunk > 0) DiagLog::Log("[CONFIG] ToggleButtons: unknown token(s) ignored");
                if (tparsed != 0) g_cfg.toggleButtons = tparsed;
                // 全トークン不明(tparsed=0)は既定維持(既定のトグルコンボが有効なまま)。
            }
            g_cfg.defaultEnabled  = readBool(L"DefaultEnabled",  "DefaultEnabled",  g_cfg.defaultEnabled);
            g_cfg.announceEnabled = readBool(L"AnnounceEnabled", "AnnounceEnabled", g_cfg.announceEnabled);
            // 起動音(1/0 または true/false)。DLL 埋め込み WAVE リソースを PlaySound で再生。
            g_cfg.startupSound    = readBool(L"StartupSound",    "StartupSound",    g_cfg.startupSound);
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
void Config::ResetForTest() {
    AcquireSRWLockExclusive(&g_loadLock);
    ApplyDefaults(g_cfg);
    g_loaded.store(0, std::memory_order_release);
    ReleaseSRWLockExclusive(&g_loadLock);
}
void Config::SetIniPathForTest(const wchar_t* path) {
    AcquireSRWLockExclusive(&g_loadLock);
    g_iniPathOverride = path;
    ReleaseSRWLockExclusive(&g_loadLock);
}
#endif