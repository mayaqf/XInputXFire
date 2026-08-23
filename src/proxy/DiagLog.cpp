// DiagLog.cpp - 起動診断ログ共有実装
#include "DiagLog.h"
#include <windows.h>
#include <cstdio>
#include <cwchar>

namespace {
SRWLOCK g_lock = SRWLOCK_INIT;
const wchar_t* kLogName = L"XInputXFire_xinput.log";

// %TEMP%\<ログ名> を path に構築。バッファに収まらない場合はログ名のみ(フォールバック)。
void BuildLogPath(wchar_t* path, size_t pathCount) {
    if (pathCount == 0) return;
    path[0] = L'\0';
    wchar_t tmp[MAX_PATH] = {0};
    DWORD n = GetTempPathW(MAX_PATH, tmp);
    size_t nameLen = wcslen(kLogName);
    // バッファ境界: n + nameLen + NUL が収まる場合のみ結合(従来は n < MAX_PATH のみで
    // ファイル名追記でオーバーフローしていた)。
    if (n > 0 && (size_t)n + nameLen + 1 <= pathCount) {
        for (DWORD i = 0; i < n; ++i) path[i] = tmp[i];
        for (size_t i = 0; i < nameLen; ++i) path[n + i] = kLogName[i];
        path[n + nameLen] = L'\0';
    } else {
        // TEMP 取得失敗/長すぎる場合はファイル名のみ(カレントへ)。
        size_t copy = (nameLen + 1 < pathCount) ? nameLen : pathCount - 1;
        for (size_t i = 0; i < copy; ++i) path[i] = kLogName[i];
        path[copy] = L'\0';
    }
}
}

void DiagLog::Log(const char* msg) {
    AcquireSRWLockExclusive(&g_lock);
    wchar_t path[MAX_PATH] = {0};
    BuildLogPath(path, MAX_PATH);
    HANDLE h = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        char line[512];
        int len = sprintf_s(line, sizeof(line), "%s\r\n", msg);
        if (len > 0) { DWORD written = 0; WriteFile(h, line, (DWORD)len, &written, nullptr); }
        CloseHandle(h);
    }
    ReleaseSRWLockExclusive(&g_lock);
}