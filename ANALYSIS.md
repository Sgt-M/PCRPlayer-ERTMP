# PCRPlayer ソース解析レポート

作成日: 2026-11-07 / 対象: `Desktop\mimocode\PCRPlayer`（git管理なし・ローカルスナップショット）

---

## 1. 概要と来歴

- **PCRPlayer** はPeerCast配信（動画/音声）再生特化のWindowsプレイヤー。Version **0.2.7.0**、著作権表記 **(C) 2016 narayado**、公式サイト `http://pecatv.s25.xrea.com/`（VersionDlg.cpp）。
- 特徴は2つ:
  1. **DirectShowグラフを自前で構築**（ファイル/HTTP/PeerCastの3モード、FLVは内蔵スプリッタフィルタ）
  2. **2ちゃんねる系BBS（したらば対応）をブラウザ連携で併走表示**（BBSバー＋内蔵ブラウザ＋外部スキンHTML）
- ライセンスは **GPL v3**（LICENSE.txt全文同梱）。改修版を配布する場合はソース公開義務が生じる点に注意。

## 2. 技術スタックとビルド構成（vcxproj/slnから確認した事実）

| 項目 | 内容 |
|---|---|
| 言語/FW | C++ (MFC, **静的リンク**, Unicode), ダイアログベースアプリ |
| コンパイラ | VS2015 ツールセット **v140_xp**（XP互換）, Windows TargetPlatformVersion **8.1** |
| プラットフォーム | Win32 / x64 の Debug/Release（4構成）。Release/Win32のみDPI Awareness有効 |
| 必須外部SDK | DirectX SDK (June 2010), **Windows SDK 7.1 Samples の DirectShow BaseClasses**（`strmbase.lib`/`strmbasd.lib` を自作ビルドした `lib2015`）, **Boost C++ Libraries** |
| 依存Boostライブラリ | thread / xpressive / iostreams(gzip) / format / function(al) / shared_ptr / optional / date_time / filesystem / property_tree / tokenizer / algorithm / lexical_cast |
| 設定のハードコード | `LibraryPath` が `C:\Program Files\Microsoft SDKs\Windows\v7.1\Samples\...\lib2015\{x86,x64}` に直書き（vcxproj:77-94） |
| リソースカルチャ | 日本語 (0x0411)。**ソース本体はShift-JIS(cp932)エンコード** |
| ソース規模 | コンパイル対象 約365ファイル / **約8.4万行**（磁盘上の全cpp+hでは約8.9万行） |

### ビルド対象外の死蔵ファイル（磁盘にあるがvcxprojに含まれない）
- `BaseHandler.cpp` / `BaseHandler.h`（空壳）
- `EVRCustom/Presenter_.cpp`（3,019行のPresenter.cpp旧コピー）
- `EVRCustom/_PresentEngine.cpp` / `_PresentEngine.h`（旧PresentEngineコピー）
- `CustomDHtmlDialog.cpp.bak`
- （`resource.h` は ClInclude 側が `Resource.h` と大文字違いなだけで実体は同一・有効）

修正計画では **これらを削除 or 区別可能なディレクトリへ退避** しておくと誤編集を防げる。

## 3. アーキテクチャ（サブシステム別・実在クラスで追跡済み）

### 3.0 全体図
```
CMainApp (PCRPlayer.cpp) ─ InitInstance: ウィンドウクラス名"PCRPlayer"登録,
   DWM/D3D/GDI+/MLang DLL遅延ローダ初期化 → CMainDlg.DoModal()
        │
   グローバル単一インスタンス gl_ : gl::GlobalValues (Global.h)
   ├─ dsp::DSPlayer   player   … 再生本体
   ├─ bbs::BBSOperator bbs     … BBS読み書き
   ├─ network::PeerCastManager peca … YP/チャンネル情報
   ├─ utl::SyncLog    log      … ログ(LogDlgへ)
   └─ sl::SerializeValues value … 設定(PCRPlayer.xml)
```
- 設定はexeと同じフォルダの **`PCRPlayer.xml`**（ポータブル構成。Serialize.cpp `load/save(current)`）。
- 再生通知は `WM_GRAPH_NOTIFY (WM_APP+20)` を CMainDlg が `OnGraphNotify` で受ける非同期モデル。

### 3.1 再生コア（~9千行: Media/Network core）
- **`dsp::DSPlayer`**（DSPlayer.h/.cpp）: MODE_OFFLINE / MODE_ONLINE / MODE_PEERCAST の3モード。
  `IBaseBuilder` を `boost::shared_ptr` で持ち替え、execute/terminate を **boost::thread** で分離。
- ビルダ: `BaseBuilder`（共通グラフ操作）→ `OfflineBuilder` / `OnlineBuilder` / `PeerCastBuilder`。
  - Online/PeerCast(F4V): `URLBufferSource` + `EVRCustom` + `DSR`(Default Sound Renderer)（OnlineBuilder.cpp:42-44, PeerCastBuilder.cpp:82-84）
  - PeerCast(WMV/ASF): `WMASFSource`（PeerCastBuilder.cpp:105）
- **`dsp::GraphManager`**（GraphManager.cpp）: `IGraphBuilder` 手動構築。`SourcePtr/VideoPtr/AudioPtr` 抽象＋デコーダは `DecoderIDSet`（フィルタ名+CLSID）でユーザー選択。
  - **FLVは进程的な登録不要でインプロセス新 instantiation**: `new CFLVSplitterFilter(...)` を graph に AddFilter（GraphManager.cpp:440）。設定は `IFLVSplitterProp` 経由（バッファ設定 setFLVConfig, :962）。FLV停止時は `Abort()`→`stop()` の順序依存あり（:165コメント）。
  - 強制終了対策として `detach()` でCComPtrを意図的にリークさせデストラクタクラッシュを回避（:204-224）— **終了系コードは地雷原**。
- ソース実装（FileSource.h）: `DefaultSource`(ファイル) / `URLFileSource` / `URLBufferSource` / `WMASFSource`、
  `AsyncBufferingLoader`（非同期バッファ読み込み）＋ `MemSourceFilter`（メモリ供給DSフィルタ, MemAsyncReader付き）。
- 音声: `AudioRenderer`(DSR)。映像: `VideoRenderer.cpp` に `VMR9` / `EVR` / **`EVRCustom`** の3実装。
- 時計: `CustomReferenceClock`（PeerCastストリームの可変レート対策）。

### 3.2 EVRCustom（約1.7万行、うち死蔵约4千行を除くと約1.3万行）
- マイクロソフトの **EVRBase(カスタムプレゼンタ)サンプルの改変版**（`EVRCustomPresenter::CreateInstance` を直接呼ぶ。COM登録なし。VideoRenderer.cpp:228-250）。
- `Presenter.cpp`(3,058行) / `PresentEngine.cpp` / `scheduler.cpp` / `DX9Shaders.cpp` / `D3DUtl.cpp` / `CoordGeom.cpp`。
- 自作拡張: `ICustomRenderProp`（**DXVA2 ON/OFF**, VideoRenderer.cpp:249）、リサイズ方式・出力レンジ(0-255/16-235)・色補正（ProcAmp）→ UIは `VideoAdjustDlg` / `DetailDecoderDlg`。
- `EVRCustom/common/` は MSサンプルの汎用ヘッダ群（critsec, AsyncCB 等）。

### 3.3 Splitter（約9.5千行）
- DirectShow BaseClasses 上の自作FLVスプリッタ一式: `BaseSplitter(File/FileEx)` → `FLVSplitter`、`FLVMetaData`, `AsyncReader`, `GolombBuffer`, `MultiFiles`, `DSUtil`。
- `DllRegisterServer` を実装しているが**exe内側**（外部DLL登録はしておらず、GraphManagerがC++newで直接使う）。旧来DLLフィルタ名残のuuidは `moreuuids.h`。

### 3.4 ネットワーク（Network.h/.cpp, PeerCast*, ~3千行）
- **WinHTTP** ベースの `network::HttpClient`（GET/POST別プロキシ＋白黒正規表現 `Proxy` 構造）, `URLParser`。UserAgent既定 `PCRPlayer/1.00`。
- `network::PeerCast`: チャンネル `channel.xml` → `ChannelInfo`（relay/track/変換値）。`PeerCastManager`: Yellow Pages一覧・チャンネル情報の定期更新をboost::thread＋コールバック(`SyncValue<boost::function>`)でUIへ。

### 3.5 BBSサブシステム（約8千行: BBS*/BaseBBS/Res*/Thread*/Box*/Extract*/Search*/Replace*）
- `bbs::BBSOperator`（BBSOperator.h）: 2ch/したらば。既定UA **`Monazilla/1.00 (PCRPlayer/1.00)`**, 自動更新間隔7秒, レスポップアップ, 履歴最大100。
  - **URL正規表現リプレイス群**（したらば旧ドメイン livedoor→shitaraba 等）と**メッセージ整形正規表現群**（空行圧縮、連続ｗ省略…）が既定値としてヘッダにベタ書き。ユーザーが任意の正規表現を追加できる設計＝改修の第一候補になりやすい箇所。
- `BaseBBS`: dat取得→解析。文字コードは `Mlang` で自動判定、gzipは boost::iostreams。`BBSRegex` がレス番号/ID/参照抽出。`BBSManager` が板/スレッド管理。
- UI: `BBSDlg`(BBSバー) / `BoxDlg`(書き込み) / `ResDlg`+`LayeredWindow`(レスポップアップ) / `ThreadDlg`(スレ一覧) / `ExtractDlg`+`ExtractBrowserDlg`(抽出し) / `SearchDlg` / `ReplaceDlg` / `FavoriteDlg`。
- **スキンHTML外部化**: `<exeフォルダ>\skin\<スレッドスキン>\Header.html,Res.html` と `Popup*.html`（MainDlgSub.cpp:1015-1040, `gl_.html`へ）。見つからなければエラー表示。`BBSSkin` が展開描画。

### 3.6 ブラウザ（MSHTML）
- `CustomDHtmlDialog`（MFC `CDHtmlDialog` の改造, 930行）＋ `CCustomControlSite`/`CustomEventHandler` でWebBrowser(IE)操作をフック。
- `BrowserDlg`（内蔵BBSブラウザ, CLayoutCtrl で映像枠の上下左右にドッキング）/ `PopupBrowserDlg` / `BrowserHook` / `BrowserMenu`。
- 外部ブラウザ起動は `utl::selectExecute(...)`（設定で既定ブラウザ選択可）。

### 3.7 UI・ウィンドウ（最大の塊 ~3.2万行 + 設定ダイアログ ~9千行）
- `CMainDlg`（MainDlg.cpp + **MainDlgMenu.cpp + MainDlgSub.cpp** の分割実装）: 映像ウィンドウそのもの。フレームレス化(`WM_NCCALCSIZE`操作)、スナップ(`WindowSnap`/`SizeMove`)、マウスジェスチャ(`Gesture`)、ホイール/サイドボタン操作、最小化時ミュート/停止。
- レイアウト: `CLayoutCtrl`（バー/ブラウザのドッキング位置管理）, `CSubpaneDlg`（BBSバー+シーク+ステータスのサブペイン）, `FrameDlg`。
- バー群: `StatusDlg`(+`StatusLayout`) / `SeekDlg` / `HintDlg`(情報表示) / `CheckBoxStatusBar`。
- **設定UIは MFC PropertyGrid 拡張**（MFCPropertyGrid* 9クラス）で、カテゴリ別ダイアログが **Detail\*Dlg / Display\*Dlg / Operation\*Dlg に約70ファイル分散**。ライブプレビュー用の `*SampleView` 群が対応。
- `OptionDlg`(タブホスト) / `SkinConfigDlg` / `ChannelDlg`(YPブラウザ) / `BrowserDlg`(抽出) / `LogDlg`。

### 3.8 設定シリアライズ（約4千行）
- `sl::SerializeValues` を Display / Operation / Video / Detail の4分割ヘッダで定義、Load/Saveを12ファイルに分離（Serialize*Load/Save.cpp）。`XmlParser` は簡易自作ラッパ。
- キー例: `root/recent`, `option`, 以降 `put(root, video/detail/operation/display)`。**既存PCRPlayer.xmlとの後方互換**を崩さない設計が望ましい。

### 3.9 共通インフラ（Util.cpp 1,915行ほか）
- `utl` namespace: ログ(`SyncLog`/`Logger`/`Debug.h`)、同期(`SyncValue`/`SyncOptional`/`SyncThread`)、DWM/GDI+/MLang遅延ローダ(`Mlang`はコードページ変換)、`CommandLine`、`WindowSnap`、`Gesture`、`ProcessManager`(プロセス優先度)、`loadHtml`, `selectExecute`, `ReplaceElement`(正規表現置換要素)。
- `Hook.h`: `FilterPropertyHook` = DirectShowフィルタのプロパティダイアログをサブクラス化して中央寄せ等するUIハック。

## 4. 品質メモ

**良い点**
- namespace単位（dsp/bbs/network/sl/utl/d3dutl）で責務が明確。グローバルは `gl_` 単一。
- Builder/Source/Renderer等の抽象＋差し替え設計で、再生モード増設の拡張点は明確。
- 設定モデルが細分化・XML化されており、新規設定項目の追加手順が読みやすい。
- 終了時のCOM解放順序（detach()）など、DirectShowアプリの落とし穴を認識した書き込みがある。

**改修前に直面する既知の課題**
1. **ツールチェーンが旧い**: v140_xp + SDK 8.1 + WinSDK7.1 BaseClasses(lib2015自作) + DirectX SDK June 2010。現代のVS2022(v143)+現行SDKでの再ビルドは概ね可能だが、BaseClassesを同ツールセットで再ビルドする必要がある。`LibraryPath`の絶対パス直書きも移植性を損なう。
2. **Boost版数が効く**: `boost::xpressive`（BBSRegex/BaseBBS多用）は新しめのBoostでは非推奨化が進行中。ビルドが通るBoostのバージョンピン留めが必須になる。filesystem/date_timeはリンク必要ライブラリ。
3. **MSHTML(IE/WebBrowser)依存**: BBSブラウザ/レスポップアップが根幹。IE廃止後もMSHTML自体は生存しているが、ドキュメントモードやセキュリティ制限の変遷で挙動が崩れるリスクが中長期で上昇し続ける。
4. **PeerCast v1前提**: YP/チャンネル情報の想定が旧PeerCastプロトコル。現行中継（Peercast-relay等）との互換確認はネットワーク層（PeerCast*.cpp/Network.cpp）に閉じていて比較的局所的。
5. **ソースがShift-JIS・コメント日本語**: エディタ/CIのエンコーディング指定を誤ると即文字化け。BOMなしcp932前提。
6. **git管理なし**（.suoのみ）。テストコードも一切ない。**改修着手前にgit初期化＋現環境でのビルド成功コミット**が最優先。
7. 死蔵ファイル4種（§2）と `Presenter.cpp`(3,058行)/`VideoRenderer` 系の巨大ファイルは変更影響の把握に時間がかかる。
8. GPL v3: 派生物の配布時に同等のソース公開義務。社内利用のみなら問題なし。

## 5. 修正計画に際しての推奨着手順（要約）
1. git化 → 現構成のままビルド再現（VS2015 or v143化の可否をここで決める）→ 0.2.7.0動作確認
2. 死蔵ファイル退避、`LibraryPath`の環境変数化、Boostバージョン確定・同梱方針決定
3. 改修目的が「BBS/ブラウザ系」なら MSHTML代替(WebView2)の要否判断が最大の分岐。WebView2化は CustomDHtmlDialog/BrowserDlg/BBSSkin 層に波及し、再生系(EVR/DX9)とWebView2(D3D11)の合成方式の再設計を要する
4. 改修目的が「再生系」なら GraphManager/ビルダが拡張点。PeerCast互換更新は network 層のみで完結可能
5. 設定項目追加は Serialize\* の4カテゴリ対応（Load/Save対）に追記する定型的な手順で安全

---
一言で: **2016年製・GPLv3・約8.5万行のMFC+DirectShow+MSHTMLのモノリスだが、namespace分離と設定シリアライズの作りは素直で、最大のリスクはツールチェーン(旧SDK/Boost版)とIE(MSHTML)依存。**
