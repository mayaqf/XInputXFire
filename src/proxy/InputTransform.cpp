// InputTransform.cpp - ボタン名文字列 -> ビットマスク
#include "InputTransform.h"
#include <cstdlib>
#include <cwchar>
#include <string>

namespace {
struct Entry { const wchar_t* name; WORD bit; };
static const Entry kTable[] = {
    {L"DPAD_UP",    XINPUT_GAMEPAD_DPAD_UP},
    {L"DPAD_DOWN",  XINPUT_GAMEPAD_DPAD_DOWN},
    {L"DPAD_LEFT",  XINPUT_GAMEPAD_DPAD_LEFT},
    {L"DPAD_RIGHT", XINPUT_GAMEPAD_DPAD_RIGHT},
    {L"A",          XINPUT_GAMEPAD_A},
    {L"B",          XINPUT_GAMEPAD_B},
    {L"X",          XINPUT_GAMEPAD_X},
    {L"Y",          XINPUT_GAMEPAD_Y},
    {L"LB",         XINPUT_GAMEPAD_LEFT_SHOULDER},
    {L"RB",         XINPUT_GAMEPAD_RIGHT_SHOULDER},
    {L"START",      XINPUT_GAMEPAD_START},
    {L"BACK",       XINPUT_GAMEPAD_BACK},
    {L"LSB",        XINPUT_GAMEPAD_LEFT_THUMB},
    {L"RSB",        XINPUT_GAMEPAD_RIGHT_THUMB},
};
}

WORD InputTransform::ParseTargetButtons(const wchar_t* text, int* unknownCount) {
    if (unknownCount) *unknownCount = 0;
    if (!text) return 0;
    WORD mask = 0;
    // コピーして wcstok で破壊的分割
    std::wstring buf(text);
    wchar_t* ctx = nullptr;
    wchar_t* tok = wcstok_s(&buf[0], L"|", &ctx);
    while (tok) {
        // 前後空白トリム
        while (*tok == L' ' || *tok == L'\t') tok++;
        wchar_t* end = tok + wcslen(tok);
        while (end > tok && (end[-1] == L' ' || end[-1] == L'\t')) { *--end = L'\0'; }
        if (*tok == L'\0') { tok = wcstok_s(nullptr, L"|", &ctx); continue; }
        bool matched = false;
        for (const auto& e : kTable) {
            if (_wcsicmp(tok, e.name) == 0) { mask |= e.bit; matched = true; break; }
        }
        if (!matched && unknownCount) (*unknownCount)++;
        tok = wcstok_s(nullptr, L"|", &ctx);
    }
    return mask;
}

// kTable 順でセット済みビットの名前を "|" 区切りで buf に書き出す。
void InputTransform::FormatButtons(WORD mask, wchar_t* buf, size_t bufCount) {
    if (!buf || bufCount == 0) return;
    buf[0] = L'\0';
    if (mask == 0) return;
    size_t pos = 0;
    for (const auto& e : kTable) {
        if (!(mask & e.bit)) continue;
        if (pos > 0) {
            if (pos + 1 >= bufCount) break;
            buf[pos++] = L'|';
        }
        size_t n = wcslen(e.name);
        for (size_t i = 0; i < n; ++i) {
            if (pos + 1 >= bufCount) { buf[pos] = L'\0'; return; }
            buf[pos++] = e.name[i];
        }
    }
    buf[pos] = L'\0';
}