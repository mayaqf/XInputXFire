# XInputXFire Proxy DLL
cmake_minimum_required(VERSION 3.20)
project(XInputXFireProxy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC 静的CRT(/MT)でDLL依存を最小化
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

# プロキシDLL本体(x86 または x64。ビルド時に -A Win32/-A x64 で切り替え)
add_library(XInputProxy SHARED
    src/proxy/dllmain.cpp
    src/proxy/exports.cpp
    src/proxy/RealXInputLoader.cpp
    src/proxy/XFireEngine.cpp
    src/proxy/Config.cpp
    src/proxy/InputTransform.cpp
    src/proxy/Announcer.cpp
    src/proxy/DiagLog.cpp
    src/proxy/XInputProxy.def
    src/proxy/StartupSound.rc  # 起動音 WAV を DLL リソースとして埋め込み
)
target_include_directories(XInputProxy PRIVATE src/proxy)
target_compile_definitions(XInputProxy PRIVATE
    WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0602 NOMINMAX)
if(MSVC)
    target_compile_options(XInputProxy PRIVATE /W4 /permissive- /utf-8)
endif()
# SAPI 音声合成(Announcer の SpVoice 用) + winmm(PlaySound:埋め込み起動音 WAVE リソース再生)
# xinput.lib は一切リンクしない(本物システムDLLと競合)。.def でエクスポート名固定。
target_link_libraries(XInputProxy PRIVATE sapi winmm)
# ビット別の出力名(xinput1_3_x86.dll / xinput1_3_x64.dll)。配置時にゲーム側でリネーム。
# ローダは自身のモジュール名でロード先システムDLLを切替えるため、同一バイナリを
# XInput9_1_0.dll としても配置可能。post-build で別名コピーを生成。
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_XF_BIT x64)
else()
    set(_XF_BIT x86)
endif()
set_target_properties(XInputProxy PROPERTIES OUTPUT_NAME "xinput1_3_${_XF_BIT}")
add_custom_command(TARGET XInputProxy POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:XInputProxy>"
            "$<TARGET_FILE_DIR:XInputProxy>/XInput9_1_0_${_XF_BIT}.dll"
    COMMENT "Copying XInputProxy -> XInput9_1_0_${_XF_BIT}.dll (XInput 9.1.0 形態)")
# ビルド成果物と同名の小文字 xinput1_3.dll を生成(ベンチ/テストハーネス用 + 某14配置用)。
add_custom_command(TARGET XInputProxy POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:XInputProxy>"
            "$<TARGET_FILE_DIR:XInputProxy>/xinput1_3.dll"
    COMMENT "Copying XInputProxy -> xinput1_3.dll (bench/test harness + 某14配置用)")
# 設定テンプレ ini を成果物フォルダへ配置(test_harness 実行時や某14配置時の staging 用)。
# assets/XInputXFire.ini が唯一の正本。ユーザ固有値は対象ゲームexeと同フォルダの
# 実運用 ini 側で設定すること(この staging コピーは毎ビルドでテンプレに上書きされる)。
# ini は DLL 由来でないため POST_BUILD(DLL 再リンク時のみ実行)ではなく独立 custom target
# とし、DLL が更新されないビルドでも ini 変更を確実に反映する(copy_if_different で実質 no-op)。
set(_XF_INI "${CMAKE_CURRENT_SOURCE_DIR}/assets/XInputXFire.ini")
add_custom_target(copy_xfire_ini ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_XF_INI}"
            "$<TARGET_FILE_DIR:XInputProxy>/XInputXFire.ini"
    DEPENDS "${_XF_INI}"
    COMMENT "Copying XInputXFire.ini -> staging (テンプレ・正本は assets/)")
add_dependencies(copy_xfire_ini XInputProxy)

# ---- 単体テスト(モックQPC時刻注入・コントローラ不要) ----
enable_testing()
add_executable(xfire_unit
    src/test/xfire_unit.cpp
    src/proxy/XFireEngine.cpp
    src/proxy/Config.cpp
    src/proxy/InputTransform.cpp
    src/proxy/Announcer.cpp
    src/proxy/DiagLog.cpp
)
target_include_directories(xfire_unit PRIVATE src/proxy)
target_compile_definitions(xfire_unit PRIVATE
    XFIRE_TEST WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0602 NOMINMAX)
if(MSVC)
    target_compile_options(xfire_unit PRIVATE /W4 /permissive- /utf-8)
endif()
target_link_libraries(xfire_unit PRIVATE sapi winmm)
add_test(NAME xfire_unit COMMAND xfire_unit)

# ---- テストハーネス(プロキシDLLをLoadLibraryしてGetState時系列ダンプ) ----
add_executable(test_harness src/test/TestHarness.cpp)
target_compile_definitions(test_harness PRIVATE
    WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0602 NOMINMAX)
if(MSVC)
    target_compile_options(test_harness PRIVATE /utf-8)
endif()
# 実行時にプロキシDLLを同フォルダに xinput1_3.dll として配置して実行する