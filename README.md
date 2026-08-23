# XInputXFire Proxy DLL

Xboxコントローラ(XInput)の **R2 または L2 トリガーを押している間だけ、方向キー(DPAD)と ABXY ボタンを連射** するツール。XInput Plus のプロキシDLL方式を参考に自作。

## 動作方式（プロキシDLL方式）

対象ゲームの exe と同じフォルダにプロキシDLL(`xinput1_3.dll`)と設定(`XInputXFire.ini`)を配置すると、OSのDLL検索順序でローカルDLLが優先ロードされ XInput API 呼び出しを横取りします。本物システムDLLを動的ロードして転送しつつ、`XInputGetState` の戻り値の DPAD/ABXY ビットを周期ON/OFFで改変してゲームへ返します。**常駐不要**。

```
game.exe ─(暗黙DLL解決)─▶ ローカル xinput1_3.dll [プロキシ]
                              │ 初回呼出で遅延Init(本物DLLロード+ini読込)
                              ▼
                          本物 System32\xinput1_3.dll (or 1_4) に転送 → 生state取得
                              ▼
                          連射エンジン: L2/R2判定 → QPC周期 → DPAD/ABXY 改変
                              ▼ (トリガ値は触らない)
                          ゲームへ改変後 state を返す
```

## 機能（連射ロジック）

- **発動条件**: L2 または R2 が押下閾値(既定150/255)以上。いずれかで発動。
- **連射対象**: DPAD(上下左右) + ABXY（ini で個別指定可）。
- **周期**: QPC 高精度タイマベース。ON区間(既定50ms)=ボタン押下、OFF区間(既定50ms)=ボタン離。ポーリング間隔(60/120Hz等)に依存しない。
- **初回ON区間(FirstOnMs)**: 対象ボタン押下直後の最初のON区間だけ専用の長さを指定(既定200ms・0=無効=OnMsと同値)。位相クロックは対象ボタン押下基準で、ボタンを離すとリセットされ次回押下で再びFirstOnMsから。`FirstOnMs` を大きくすれば連射ON中でも「1回だけボタンを押す」がコントロール可能(長い初回ONの間に離せば1回押下で確定)。
- **ヒステリシス**: 押下閾値150 / 離上閾値140 で、トリガが閾値付近でチャタリングしても連射が暴走しない。
- **4コントローラ独立**: dwUserIndex 0-3 各々独立状態。
- **トリガ透過**: L2/R2 のアナログ値はゲームへそのまま伝達（連射は DPAD/ABXY のみに作用）。
- **物理押下のみ連射**: 物理的に押されている対象ボタンだけ連射。離せば停止。

## 設定ファイル (XInputXFire.ini)

ゲームexeと同フォルダに配置。無い場合はデフォルト値を使用。

```ini
[XFire]
OnMs=50
OffMs=50
; 押下直後の最初のON区間(ms・0=無効=OnMsと同値・既定200)。大きくすれば連射ON中でも1回だけ押せる。
FirstOnMs=200
TriggerThreshold=150
HysteresisLow=140
TargetButtons=DPAD_UP|DPAD_DOWN|DPAD_LEFT|DPAD_RIGHT|A|B|X|Y
EnableL2=1
EnableR2=1
```

`TargetButtons` 対応名: `DPAD_UP` `DPAD_DOWN` `DPAD_LEFT` `DPAD_RIGHT` `A` `B` `X` `Y` `LB` `RB` `START` `BACK` `LSB` `RSB`（`|` 区切り）

## プロキシDLLビルド

要 Visual Studio Build Tools 2022 (C++ workload)。CMake・MSBuild は Build Tools 2022 に同梱されていますが、通常のシェル(cmd / PowerShell / Git Bash 等)では **PATH に含まれない**ため、そのまま `cmake` を打つと見つかりません。**「Developer Command Prompt for VS 2022」**を起動してそこから実行してください（同梱の CMake・MSBuild にパスが通ります）。

```bash
# 32bit
cmake -S . -B build-x86 -A Win32
cmake --build build-x86 --config Release
# 64bit
cmake -S . -B build-x64 -A x64
cmake --build build-x64 --config Release
# 単体テスト(コントローラ不要)
cmake --build build-x64 --config Release --target xfire_unit
ctest --test-dir build-x64 --output-on-failure
```

成果物:
- `build-x86/Release/xinput1_3_x86.dll`, `xinput1_3.dll` (`xinput1_3_x86.dll` のリネーム版)
- `build-x64/Release/xinput1_3_x64.dll`, `xinput1_3.dll` (`xinput1_3_x64.dll` のリネーム版)
- `xfire_unit.exe`, `test_harness.exe`

## 配置（手動）

1. 対象ゲームのビット(32/64)に合うプロキシDLLを、ゲームが使う XInput DLL 名にしてゲームexeと同フォルダへ。
   - 1.3 形態（某14等）: `xinput1_3.dll` を**そのまま配置**。
   - 9.1.0 形態: `XInput9_1_0_x64.dll` / `XInput9_1_0_x86.dll` を `XInput9_1_0.dll` にリネームして配置。
   - 1.4 形態: `xinput1_3_x64.dll` / `xinput1_3_x86.dll` を `xinput1_4.dll` にリネームして配置。
   - **プロキシDLLはゲームexeと同フォルダにのみ配置してください。`C:\Windows\System32` / `SysWOW64` には置かないでください**（プロキシ自身が本物DLLと同名で同フォルダに置かれると、自己再ロードによる無限再帰を起こす可能性があります）。
2. `XInputXFire.ini` を同フォルダへ。
3. ゲームを通常起動（常駐不要）。

## 検証

1. **単体テスト** (`xfire_unit`): モックQPC時刻注入で連射ロジック(ON/OFF周期・ヒステリシス・トリガ透過・対象外保護・4コントローラ独立)を検証。コントローラ不要。
2. **テストハーネス** (`test_harness`): プロキシDLLを同フォルダに `xinput1_3.dll` として配置し実行。実コントローラの R2 押下中に対象ボタンが周期トグルするか CSV ログで確認（60秒）。
   - **Smart App Control(SAC) 有効環境では test_harness.exe が起動できない場合があります**。SAC は未署名かつ Microsoft クラウドに実績のない新規バイナリを「未確認」としてブロックし（パスベースではないため Program Files 等へのコピーでも回避不可）、ユーザ側の例外設定もありません。イベントログ `Microsoft-Windows-CodeIntegrity/Operational` の ID 3118/3033 で「Smart App Control Block」を確認できます。
   - なお**本番のプロキシDLLは SAC 下でもブロックされません**（署名済みゲーム exe が LoadLibrary で読み込む DLL は許容されるため）。SAC に引っかかるのは test_harness 等の**独立未署名 exe のみ**です。test_harness が起動できない場合は、SAC オフの別PC/VM で検証するか、本番ゲーム経路で実機確認してください。
3. **統合**: 許可されたオフラインゲームに配置し実動作確認。

## 対応ゲームの条件

本ツールは **XInput の `XInputGetState` でコントローラ入力を取得するゲーム** にのみ有効です。多くの XInput 対応ゲームが該当します。

**非対応ケース（入力を XInput 経由で取得しないゲーム）:**
- コントローラ入力を HID デバイスの直接読み取り（`SetupDi*` + `CreateFileW` + `ReadFile` + `HidP_*`）や RawInput で取得するゲームでは、プロキシDLLがロードされても `XInputGetState` が呼ばれないため連射できません。
- このようなゲームでは **XInput Plus**（プロキシDLLを足場に Mhook で能動的に HID 経路をインラインフックする方式）などを使用してください。
- ゲームが XInput 経由で入力を取るか不明な場合は、配置後にコントローラが無反応なら非対応の可能性があります。

**某14(dx11) について:** XInput 独自ラッパ（`XInputXIV3.dll`）経由で `XINPUT1_3.dll` を**序数(2,3,5)でインポート**して `XInputGetState` を呼ぶため、本ツールの XInput 経路改変で連射が動作します（実機確認済み）。配置は `xinput1_3.dll` **1本のみ**（`XInput9_1_0.dll` プロキシは置かない — 2本配置は同一プロセスに2プロキシがロードされ起動クラッシュする）。序数インポートに対応するため `.def` で正規序数(`@2`=`GetState` 等)を明示指定済みです。

## トラブルシューティング（診断ログ）

コントローラが無反応・連射が効かない場合、プロキシDLLは起動時（初回エクスポート呼出時）の診断結果を **`%TEMP%\XInputXFire_xinput.log`** に1行ずつ追記します。このログで原因を切り分けられます。

- `[STICKYINIT] LoadOnce=1 hDll=... GetState=...` → プロキシが正常にロードされ、本物DLLの関数ポインタを取得した（`hDll=0000000000000000` なら本物DLLロード失敗）。
- `[LOADER] ...` → 本物DLLのロード失敗・必須エクスポート欠落等（フォールバック先DLLの切り替え状況）。
- `[CONFIG] ...` → ini の値が非数値・範囲外・不明トークンで既定値に置換された（意図しない挙動の原因特定に）。
- `[XFIRE] QueryPerformanceFrequency returned 0 ...` → 高精度タイマ取得失敗（連射機能が無効化・パススルーのみ動作）。

**プレイ中はこのログは増えません**。診断行は起動時のみ書かれ、毎フレームの `XInputGetState` / 連射処理のホットパスはログを書きません（長時間プレイでも肥大化しません）。ログが `[STICKYINIT]` 1行だけで後が続かない場合はプロキシは正常に動いているので、コントローラ無反応は「ゲームが XInput 経由で入力を取得していない（非対応）」の可能性が高いです（→ [対応ゲームの条件](#対応ゲームの条件)）。

## ⚠️ 注意（自己責任）

- プロキシDLL方式は XInput Plus / x360ce と同様に**アンチチートに検出される可能性**があります。**オンラインゲームでの使用は自己責任**です。
- 設定不備でゲームが誤動作する場合があります。事前にテストハーネスで確認してください。
- 本ツールは個人の学習・研究目的、および許可された環境での使用を想定しています。

## 設計のポイント

- **DllMain で何もしない**: 本物DLLロード・ini読込・QPC初期化は初回エクスポート呼出時の遅延Initで行う。DllMain 内の LoadLibrary はローダーロックでデッドロックを起こすため（MS 公式 DLL Best Practices 準拠）。
- **自己再ロード防止**: 本物DLLは `GetSystemDirectoryW` で System32(32bitプロセスはSysWOW64)の**フルパス**を構築してロードする。`LOAD_LIBRARY_SEARCH_SYSTEM32` だけでは、プロセスに既にロード済みの同名モジュール(プロキシ自身)が名前ベースで再利用され自己再帰するため不十分（実証済み）。フルパス指定なら正規化パスで既存モジュールを判定し、プロキシ自身と区別される。
- **1.3→1.4 フォールバック**: `xinput1_3.dll` が無い環境(Win8+)では `xinput1_4.dll` を使用。
- **xinput.lib 非リンク**: `xinput.h` を include せず構造体を自前定義。本物DLLとリンク衝突しない。
- **エクスポート名固定**: `.def` で装飾無し名前を固定（x86 stdcall の装飾名問題回避）。
- **CRT 静的リンク(`/MT`)**: DLL依存を最小化。

## ライセンス

本プロジェクトのコードは **MIT License** で公開します。詳細は [LICENSE](LICENSE) を参照してください。