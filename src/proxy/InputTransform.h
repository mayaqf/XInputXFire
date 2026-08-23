// InputTransform.h - XINPUT_STATE ビット操作ヘルパ
#pragma once
#include "XInputProxy.h"

namespace InputTransform {
    // wButtons 内の mask ビットをセット(押下)
    inline void SetButtons(XINPUT_STATE& s, WORD mask) { s.Gamepad.wButtons |= mask; }
    // wButtons 内の mask ビットをクリア(離)
    inline void ClearButtons(XINPUT_STATE& s, WORD mask) { s.Gamepad.wButtons &= static_cast<WORD>(~mask); }
    // wButtons 内の mask ビットの現在押下状態を取得
    inline WORD GetButtons(const XINPUT_STATE& s, WORD mask) { return static_cast<WORD>(s.Gamepad.wButtons & mask); }
    // 連射対象ボタン名文字列 -> ビットマスク。不明な名前は無視。
    // 例: "DPAD_UP|A|B" -> XINPUT_GAMEPAD_DPAD_UP|XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_B
    // unknownCount(非null)に不明トークン数を返す(空トークンは除外)。
    WORD ParseTargetButtons(const wchar_t* text, int* unknownCount = nullptr);
    // ビットマスク -> ボタン名文字列(ParseTargetButtons の逆・kTable 順で "|" 区切り)。
    // mask==0 のときは空文字列。buf には必ず NUL 終端を書く(長さ不足時は切り詰め)。
    void FormatButtons(WORD mask, wchar_t* buf, size_t bufCount);
}