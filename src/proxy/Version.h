// Version.h - ビルドバージョン(CMake から XFIRE_VERSION_MAJOR/MINOR/PATCH を数値マクロで注入)。
// 数値を文字列化して "MAJOR.MINOR.PATCH" を構成。文字列マクロのジェネレータ依存の
// クォート問題を回避するため数値で渡してここで組み立てる。未注入時は 0.0.0 フォールバック。
#pragma once
#ifndef XFIRE_VERSION_MAJOR
  #define XFIRE_VERSION_MAJOR 0
  #define XFIRE_VERSION_MINOR 0
  #define XFIRE_VERSION_PATCH 0
#endif
#define XFIRE_VERSION_STR_(a) #a
#define XFIRE_VERSION_STR(a)  XFIRE_VERSION_STR_(a)
#define XFIRE_VERSION \
    XFIRE_VERSION_STR(XFIRE_VERSION_MAJOR) "." \
    XFIRE_VERSION_STR(XFIRE_VERSION_MINOR) "." \
    XFIRE_VERSION_STR(XFIRE_VERSION_PATCH)