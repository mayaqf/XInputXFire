// dllmain.cpp - DLL エントリポイント
// 初期化は初回エクスポート呼出時の StickyInit で行う。DllMain は何もしない。
// DllMain(ローダーロック保持)内で LoadLibrary 等を呼ぶと再入デッドロックの
// 恐れがあるため(MS 公式 DLL Best Practices で明示禁止)。
#include <windows.h>

extern "C" BOOL WINAPI DllMain(HINSTANCE /*hInst*/, DWORD /*reason*/, LPVOID /*reserved*/) {
    return TRUE;
}