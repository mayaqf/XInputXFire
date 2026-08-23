// Version.h - ビルドバージョン(CMake から XFIRE_VERSION_MAJOR/MINOR/PATCH を数値マクロで注入)。
// 数値を文字列化して "MAJOR.MINOR.PATCH" を構成。文字列マクロのジェネレータ依存の
// クォート問題を回避するため数値で渡してここで組み立てる。未注入時は 0.0.0 フォールバック。
#pragma once
// フォールバックは MAJOR/MINOR/PATCH 個別にガードする。1つだけ定義(部分注入)でも
// 残りが既定0になるため、異常な中途半端バージョンにならず安全。
#ifndef XFIRE_VERSION_MAJOR
  #define XFIRE_VERSION_MAJOR 0
#endif
#ifndef XFIRE_VERSION_MINOR
  #define XFIRE_VERSION_MINOR 0
#endif
#ifndef XFIRE_VERSION_PATCH
  #define XFIRE_VERSION_PATCH 0
#endif
#define XFIRE_VERSION_STR_(a) #a
#define XFIRE_VERSION_STR(a)  XFIRE_VERSION_STR_(a)
#define XFIRE_VERSION \
    XFIRE_VERSION_STR(XFIRE_VERSION_MAJOR) "." \
    XFIRE_VERSION_STR(XFIRE_VERSION_MINOR) "." \
    XFIRE_VERSION_STR(XFIRE_VERSION_PATCH)