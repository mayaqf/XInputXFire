// DiagLog.h - 起動診断ログ共有実装
// %TEMP%\XInputXFire_xinput.log に1行追記する(最善努力・失敗時は静かに無視)。
// exports.cpp の初回診断ログと各モジュール(Config/RealXInputLoader/XFireEngine)の
// 失敗ログで共有する。
// CRT ファイル I/O は早い段階で安全でないため Win32 API のみで実装。
#pragma once

namespace DiagLog {
// msg(ASCII・NUL終端) をログファイルへ1行追記。スレッドセーフ。
void Log(const char* msg);
}