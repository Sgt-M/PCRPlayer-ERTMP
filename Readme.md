# PCRPlayer (Enhanced RTMP Edition)

[![Release](https://img.shields.io/github/v/release/Sgt-M/PCRPlayer-ERTMP)](https://github.com/Sgt-M/PCRPlayer-ERTMP/releases/latest)
![Platform](https://img.shields.io/badge/platform-Windows%20x86%20%2F%20x64-lightgrey)
[![License](https://img.shields.io/github/license/Sgt-M/PCRPlayer-ERTMP)](LICENSE)

PeerCast / PeerCastStation の配信を見るための Windows 用プレイヤーです。
PeerCastStation が 9.0.0.0 で Enhanced RTMP 配信に対応したので、既存の PCRPlayer を Enhanced RTMP（今のところ v1）に対応させたものです。HEVC と AV1 の映像を想定しています。
ついでに BBS 表示周りが IE コンポーネントを使っていたので、WebView2 に変更しました。

## 主な特徴

| | |
|---|---|
| 📺 **映像** | Peercast 配信の視聴。Enhanced RTMP v1（HEVC / AV1）対応（PeerCastStation 9.0.0.0 以上での配信） |
| 🧩 **デコーダ** | LAV Filters 推奨。設定の「詳細設定 → デコーダ → 優先して使用するビデオデコーダ」に「LAV Video Decoder」を追加してください。オーディオデコーダは好きにして。インストールが無ければ起動時に同梱フィルタを自動で DSFilter 登録します。別途インストール済みの場合はそちらが使われます |
| 🎨 **レンダラ** | MPC Video Renderer 対応。設定の「詳細設定 → デコーダ → ビデオレンダラ」で従来の EVR と選択できます（個人的に RTX VSR を使いたくて対応させただけ。分かる人だけ使えばいいと思います）。NVIDIA RTX VSR 連動の切り替え用 .reg 同梱 |
| 💬 **内蔵BBSブラウザ** | 2ちゃんねる / したらば対応（WebView2 製）。新着レスへの滑らかスクロール追従、上限到達時の次スレ自動移動＋連番継続表示、ホバーポップアップ、書き込み、置換文字列、スキン表示 |

## ダウンロード

- 最新リリース: **[Latest Release](https://github.com/Sgt-M/PCRPlayer-ERTMP/releases/latest)**
- zip を解凍して `PCRPlayer64.exe`（64bit）または `PCRPlayer.exe`（32bit）を実行するだけです。64bit版を仕様を推奨

> WebView2（Edge）ランタイムが必要です。ローダは静的リンクのため DLL の同梱は不要です。

## クイックスタート

1. リリースの zip を任意のフォルダに**展開する**（インストール型ではありません）
2. PeCaRecorderやPecaListから引数をつけて再生
3. 初回は同梱の LAV Filters / MPC Video Renderer が自動登録されていればそれで動きます
   - 手動で登録し直したい場合: `RegisterFilters.bat` をダブルクリック
   - 別途インストーラで LAV / MPC VR を入れる場合は: `UnregisterFilters.bat` を実行してからインストールしてください

## リリース zip 同梱ファイルの説明

| ファイル | 説明 |
|---|---|
| `PCRPlayer64.exe` / `PCRPlayer.exe` | 本体（64bit / 32bit）。フィルタ・スキンは同じフォルダ内から読み込まれます |
| `RegisterFilters.bat` | 同梱 LAV / MPC VR の HKCU 登録を再有効化（ダブルクリック） |
| `UnregisterFilters.bat` | 同梱 LAV / MPC VR の HKCU 登録を解除し自動登録を無効化。別途インストーラで導入する場合に実行 |
| `VSR_*.reg` | NVIDIA RTX VSR 連動の切り替え（D3D11 copy back 強制のオン/オフ）。インポート後にプレイヤーを再起動 |
| `PcrDiag64.exe` / `PcrDiag.exe` | フィルタが認識されないとき実行すると、同じフォルダに `PcrDiag.txt` が生成されます（不具合報告時に添付してください） |
| `filters/` `skin/` | 同梱フィルタ（LAV / MPC VR）と BBS スキン |

## トラブルシュート

- **映像が出ない / フィルタが効かない** → `PcrDiag64.exe` を実行して生成される `PcrDiag.txt` を添付して報告してください
- **HKCU 登録フィルタは DirectShow の列挙ツールには見えません（仕様）**。プレイヤーからは認識されます
- **RTX VSR を使いたい** → レンダラに MPC VR を選択後、レンダラから使用する設定をして使用できます。。既定（DXVA2 copy back）で動作します。D3D11 強制が必要になったら `VSR_D3D11copyback_オプトイン.reg` をインポート

## ビルド

<details>
<summary>開発環境と外部依存（クリックで展開）</summary>

Visual Studio Community 2026 / Visual C++ 2015 で開発しています ※ほぼAI丸投げ

詳細な手順は [BUILD.md](BUILD.md) を参照。VS2026 移植版の外部依存はいずれも `third_party\` 以下に配置し、リポジトリには同梱しません（.gitignore 済み）。

| 依存 | 入手元 |
|---|---|
| Boost 1.91.0 (static) | [vcpkg](https://github.com/microsoft/vcpkg) — `vcpkg install boost-*:x64-windows-static` |
| DirectShow BaseClasses | [Windows-classic-samples](https://github.com/microsoft/Windows-classic-samples) `Samples\Win7Samples\multimedia\directshow\baseclasses` を `/DUNICODE /D_UNICODE` 付きでビルド |
| WebView2 SDK | [NuGet: Microsoft.Web.WebView2](https://learn.microsoft.com/microsoft-edge/webview2/) を展開 |
| MFC コンポーネント | VS Installer の「C++ MFC を v145 ビルドツール Used (x86 & x64)」 |

※ VS2026 移植では DirectX SDK (June 2010) は不要です。BaseClasses は上記 GitHub 版（旧 Windows SDK 7.1 Samples と同一内容）を使用します。

</details>

## 更新履歴（抜粋）

| バージョン | 概要 |
|---|---|
| [v0.3.8](https://github.com/Sgt-M/PCRPlayer-ERTMP/releases/tag/v0.3.8) | 内蔵BBS: 条件次第で次スレ自動移動が発火しない不具合の修正（更新ブロック中も周期再試行） |
| [v0.3.7](https://github.com/Sgt-M/PCRPlayer-ERTMP/releases/tag/v0.3.7) | 次スレ移動時のシームレス継続表示、ポップアップ閉じ時の稀なクラッシュ修正 |
| v0.3.5 / 0.3.6 | BBS スキンフォント継承の修正、ポップアップ居残り・クラッシュ系の改善 |
| v0.3.1〜0.3.4 | 同梱フィルタ登録・RTX VSR 連動関連の修正 |
| v0.3.0 | Enhanced RTMP v1（HEVC/AV1）対応、BBS の WebView2 化、MPC Video Renderer 対応、同梱フィルタ自動登録 |

## ライセンス・クレジット

- 本リポジトリ: **GPL-3.0**（[LICENSE](LICENSE)）
- 元となった [PCRPlayer v0.27](http://pecatv.s25.xrea.com/)
- リリースファイルには以下のバイナリが含まれています
  - [MPC Video Renderer](https://github.com/Aleksoid1978/VideoRenderer)（GPL3.0）
  - [LAV Filters](https://github.com/Nevcairiel/LAVFilters)（GPL2.0）
