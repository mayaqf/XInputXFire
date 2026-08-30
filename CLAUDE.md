# XInputXFire — プロジェクト運用メモ

README.md が仕様・手順の正本。**作業前に必ず README を通読すること**（連射ロジック・配置手順・SAC 注意・診断ログの意味は README に詳しい）。ここには README に書かれていない運用上の落とし穴だけを残す。

## ビルド

- **cmake が PATH に無い**: VS Build Tools 2022 同梱。通常シェル(Git Bash / cmd / PowerShell)から `cmake` を打っても見つからない。README は「Developer Command Prompt for VS 2022」を使えと書く。Claude が bash から直接叩く場合の解決済みフルパス:
  ```
  /c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe
  ```
- **正規ビルドディレクトリは `build-x86` / `build-x64`**（README 準拠）。`.gitignore` は `/build-*/` と `/build/` 両方を無視。
- **clean-all ターゲット**: 標準 `clean` は POST_BUILD の staging コピーを残すため `clean-all` で一括削除。VS マルチコンフィグなので `--config` が必須（無いと現行コンフィグの成果物が消えない）。
  ```
  cmake --build build-x64 --target clean-all --config Release
  ```

## ini の運用

- **staging コピーは毎ビルド上書き**: `build*/Release/XInputXFire.ini` は `assets/XInputXFire.ini`（正本テンプレ）で毎ビルド上書きされる。テスト用に編集しても再ビルドで消えるので、デフォルト値を恒久変更したい場合は `assets/XInputXFire.ini` を編む。
- **ini は exe と同フォルダから読む（DLL と同フォルダではない）**: `Config::ExeDir()` が `GetModuleFileNameW(nullptr)`（プロセスの exe）基準。test_harness は `build*/Release/XInputXFire.ini` を読み、実ゲームはゲーム exe と同フォルダの ini を読む。動作確認で `build*/Release/XInputXFire.ini` を触るのは test_harness 経由のみ正しい。

## CMake マクロ追加時の注意

- **VS ジェネレータで文字列マクロはクォート崩壊する**: Release で `target_compile_definitions(... "STR=v1.0")` の `"` が失われ壊れる。数値マクロを渡してソース側で文字列化する（既存 CMakeLists が `XFIRE_VERSION_MAJOR/MINOR/PATCH` を数値で渡しソースで `#define` 文字列化しているのはこのため）。バージョン等の文字列マクロを追加する場合はこの制約に従うこと。