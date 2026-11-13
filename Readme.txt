PeerCastStationが9.0.0.0にてEnhanced RTMP配信に対応したので、既存のPCRPlayerをEnhanced RTMP (今のところv1)に対応させたものです。
HEVCとAV1の映像を想定しています。
ついでにBBS表示周りがIEコンポーネントを使っていたので、Webview2に変更しました。
ビデオレンダラーのMPC Video Rendererに対応しました。設定の詳細設定→デコーダ→ビデオレンダラで従来通りのEVRとどちらかを選べます。（個人的にRTX VSR使いたくてMPC VRに対応させただけ。分かる人だけ使えばいいと思う。)
デコーダーはLAV Filtersを使うことを推奨します。設定の詳細設定→デコーダ→優先して使用するビデオデコーダの欄で「LAV Video Decoder」を追加してください。オーディオデコーダは好きにして。
PCRPlayer起動時にMPC VRやLAV Filtersがインストールされていない場合に同梱されているものをDSFilterとして登録して利用できるようにします。別にインストールしてある場合はそちらを使います。



Visual Studio Community 2026 / Visual C++ 2015 で開発しています ※ほぼAI丸投げ

ビルドには標準ライブラリとWindowsSDKの他に
	DirectX SDK (June 2010)
	Windows SDK 7.1 の Samples に含まれる DirectShow BaseClasses
	Boost C++ Libraries
以上が必要になります

DirectShow BaseClasses と Boost C++ Libraries はそれぞれライブラリのビルドが必要です

■ VS2026移植版の外部依存と入手元 (詳細な手順は BUILD.md)
いずれも third_party\ 以下に配置し、リポジトリには同梱しません (.gitignore 済み)。

	Boost 1.91.0 (static)            https://github.com/microsoft/vcpkg
	                                 vcpkg install boost-*:x64-windows-static
	DirectShow BaseClasses           https://github.com/microsoft/Windows-classic-samples
	                                 Samples\Win7Samples\multimedia\directshow\baseclasses
	                                 を /DUNICODE /D_UNICODE 付きでビルド
	WebView2 SDK                     https://learn.microsoft.com/microsoft-edge/webview2/
	                                 NuGetパッケージ Microsoft.Web.WebView2 を展開
	VS2026 の MFC コンポーネント     VS Installer の
	                                 「C++ MFCをv145ビルドツール Used (x86 & x64)」

※ VS2026移植では DirectX SDK (June 2010) は不要です。BaseClasses は上記の
   GitHub版 (旧Windows SDK 7.1 Samples と同一内容) を使用します。

リリースファイルには
MPC Video Renderer		https://github.com/Aleksoid1978/VideoRenderer	GPL3.0
および
LAV Filters				https://github.com/Nevcairiel/LAVFilters		GPL2.0
のバイナリが含まれています。

■ リリースzip同梱ファイルの説明

	RegisterFilters.bat      同梱LAV/MPC VRのHKCU登録を再有効化（ダブルクリック）
	UnregisterFilters.bat    同梱LAV/MPC VRのHKCU登録を解除し自動登録を無効化。
	                         別途LAV FiltersやMPC VRをインストーラで導入する場合に実行。
	VSR_*.reg                NVIDIA RTX VSR連動の切り替え（D3D11 copy back強制の
	                         オン/オフ）。インポート後にプレイヤーを再起動。
	PcrDiag64.exe / PcrDiag.exe
	                         フィルタが認識されないとき実行すると、同じフォルダに
	                         PcrDiag.txt が生成されます（不具合報告時に添付してください）。

※ WebView2 (Edge) ランタイムが必要です。ローダは静的リンクのためDLLの同梱は不要です。
