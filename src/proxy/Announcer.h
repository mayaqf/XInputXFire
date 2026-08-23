// Announcer.h - トグル切替時の SAPI 音声アナウンス
// 常駐ワーカースレッドで SpVoice を保持し、SPF_ASYNC|SPF_PURGEBEFORESPEAK で発話。
// GetState(ゲーム入力スレッド)はメッセージを置いて即リターン(ブロックしない)。
#pragma once

namespace Announcer {
    // text を SAPI 音声合成で非同期再生する。
    // text は呼出後即座にコピーされる(呼出元のライフタイムは呼出時のみでよい)。
    // 話中の場合は前の発話を止して新しいメッセージを即座に鳴らす(割り込み方式)。
    // COM/SpVoice の生成/再生に失敗しても何もしない(クラッシュしない)。
    void Speak(const wchar_t* text);
    // 起動時に1回、DLL に埋め込んだ WAVE リソースを鳴らす(DLLロード完了の通知)。
    // PlaySound(SND_RESOURCE|SND_ASYNC) で非同期再生するため呼出元をブロックしない。
    // 設定 StartupSound=0 で無効化可能。失敗時は何もしない(クラッシュしない)。
    void StartupBeep();
}