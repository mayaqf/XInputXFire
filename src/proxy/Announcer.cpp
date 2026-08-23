// Announcer.cpp - SAPI SpVoice による音声アナウンス
// 常駐ワーカースレッドで1つの SpVoice を保持し、SPF_ASYNC|SPF_PURGEBEFORESPEAK で
// 発話する。話中に新メッセージが来ると前の発話を即座に止めて新しいメッセージを
// 鳴らす(割り込み方式)。連射トグルを連打しても最後の状態が確実に音声化される。
// 初期化時に en-US 音声を列挙し "Natural" 名を優先選択(機械音声を改善)。
// GetState(ゲーム入力スレッド)はメッセージを置いて SetEvent で即リターン(ブロックしない)。
// COM apartment 衝突回避: GetState スレッド上では COM を触らない。
#include "Announcer.h"
#include <windows.h>
#include <mmsystem.h> // PlaySound・SND_RESOURCE/SND_ASYNC
#include <atomic>
#include <string>
#include <sapi.h>
#include "Config.h"
#include "StartupSound.h" // IDR_STARTUP_WAV(埋め込み起動音リソース)

namespace {
// SAPI 音声カテゴリ(レジストリパス・sphelper.h の SPCAT_VOICES と同じ)。
const wchar_t* kSpCatVoices = L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Speech\\Voices";
std::atomic<int> g_initializing{0}; // 1=起動担当決定(排他)
std::atomic<int> g_ready{0};         // 1=準備完了(g_event 設定済み)
HANDLE  g_event = nullptr;    // auto-reset: ワーカーへの発話要求通知
SRWLOCK g_msgLock = SRWLOCK_INIT;
std::wstring g_pending;       // 最新メッセージ(空=要求なし)
ISpVoice* g_voice = nullptr;

// en-US 音声を列挙し、名前に "Natural" を含むものを優先選択。
// Win11 の自然音声が SAPI5 に登録されていればそれ、なければ従来の en-US 音声。
// 日本語環境でデフォルト音声が日本語音声だと英語テキストが機械的に読まれるのを改善。
void PickNaturalEnglishVoice(ISpVoice* voice) {
    // SpEnumTokens(sphelper.h)は ATL 依存のため ISpObjectTokenCategory で直接列挙。
    ISpObjectTokenCategory* pCat = nullptr;
    if (FAILED(CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL,
                    IID_ISpObjectTokenCategory, reinterpret_cast<void**>(&pCat))) || !pCat) return;
    if (FAILED(pCat->SetId(kSpCatVoices, FALSE))) { pCat->Release(); return; }
    IEnumSpObjectTokens* pEnum = nullptr;
    if (FAILED(pCat->EnumTokens(L"Language=409", nullptr, &pEnum)) || !pEnum) { pCat->Release(); return; }
    pCat->Release();
    ULONG count = 0;
    pEnum->GetCount(&count);
    ISpObjectToken* pPick = nullptr;
    for (ULONG i = 0; i < count; ++i) {
        ISpObjectToken* pTok = nullptr;
        if (FAILED(pEnum->Item(i, &pTok)) || !pTok) continue;
        LPWSTR name = nullptr;
        bool natural = false;
        if (SUCCEEDED(pTok->GetStringValue(L"Name", &name)) && name) {
            natural = (wcsstr(name, L"Natural") != nullptr);
            CoTaskMemFree(name);
        }
        if (natural) {
            if (pPick) pPick->Release();
            pPick = pTok; // Natural を採用
            break;
        }
        if (!pPick) pPick = pTok; // 最初を候補保持
        else pTok->Release();
    }
    if (pPick) {
        voice->SetVoice(pPick);
        pPick->Release();
    }
    pEnum->Release();
}

DWORD WINAPI WorkerMain(void*) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr)) {
        if (SUCCEEDED(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL,
                         IID_ISpVoice, reinterpret_cast<void**>(&g_voice))) && g_voice) {
            PickNaturalEnglishVoice(g_voice); // より自然な英語音声を選択
        }
        for (;;) {
            WaitForSingleObject(g_event, INFINITE);
            // 最新メッセージを取得(古い要求は上書きで破棄済み)。
            AcquireSRWLockExclusive(&g_msgLock);
            std::wstring msg = g_pending;
            g_pending.clear();
            ReleaseSRWLockExclusive(&g_msgLock);
            if (!msg.empty() && g_voice) {
                // ASYNC: 即リターン。PURGEBEFORESPEAK: 前の発話を破棄して新発話を開始。
                g_voice->Speak(const_cast<wchar_t*>(msg.c_str()),
                               SPF_ASYNC | SPF_PURGEBEFORESPEAK, nullptr);
            }
        }
        // 通常プロセス終了時はここへ到達しない(OS がスレッドを強制終了)。
        // SpVoice/COM は OS が解放するため、DllMain での明示的解放は行わない(ローダーロック回避)。
        if (g_voice) { g_voice->Release(); g_voice = nullptr; }
        CoUninitialize();
    }
    return 0;
}

void EnsureStarted() {
    if (g_ready.load(std::memory_order_acquire)) return;
    int expected = 0;
    if (g_initializing.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) {
        // g_event とワーカーを作成 → g_ready(release) で g_event を可視化して準備完了。
        // これで複数スレッドが同時に初回 Speak しても、g_ready==1(acquire) 観測時には
        // g_event も可視(従来は g_started.store 後に g_event 設定で競合していた)。
        HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr); // auto-reset
        if (ev) {
            HANDLE h = CreateThread(nullptr, 0, &WorkerMain, nullptr, 0, nullptr);
            if (h) {
                CloseHandle(h); // ハンドル不要(常駐・終了を待たない)
                g_event = ev;
                g_ready.store(1, std::memory_order_release); // g_event 可視化して準備完了
                return;
            }
            CloseHandle(ev);
        }
        // 起動失敗: 担当を解放し次回再試行可能にする(g_ready は 0 のまま)
        g_initializing.store(0, std::memory_order_release);
    }
}
} // namespace

namespace {
// DLL 自身のモジュールハンドルを取得(PlaySound SND_RESOURCE がリソースを探す先)。
// GetModuleHandleEx に本 TU の関数アドレスを渡すことで、グローバル状態を持たず
// DllMain に依存せずに「この DLL のモジュール」を特定できる(REFCOUNT 変更なし)。
HMODULE ThisModule() {
    HMODULE hMod = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&Announcer::StartupBeep),
        &hMod);
    return hMod;
}
} // namespace

void Announcer::StartupBeep() {
    const XFireConfig& cfg = Config::Get();
    if (!cfg.startupSound) return; // 無効時は鳴らさない
    HMODULE hMod = ThisModule();
    if (!hMod) return;
    // 埋め込み WAVE リソースを非同期再生(即リターン・呼出元をブロックしない)。
    // SND_RESOURCE: hMod 内の WAVE 型リソースを再生。失敗時は何もしない(仕様)。
    PlaySoundW(MAKEINTRESOURCEW(IDR_STARTUP_WAV), hMod, SND_RESOURCE | SND_ASYNC);
}

void Announcer::Speak(const wchar_t* text) {
    if (!text || !*text) return;
    EnsureStarted();
    if (!g_ready.load(std::memory_order_acquire)) return; // 準備未完は静かに無視(音声なしが仕様)
    AcquireSRWLockExclusive(&g_msgLock);
    g_pending = text; // 最新で上書き(話中なら前の要求は次ループで purge 対象)
    ReleaseSRWLockExclusive(&g_msgLock);
    SetEvent(g_event);
}