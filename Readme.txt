PeerCastStationが9.0.0.0にてEnhanced RTMP配信に対応下ので、既存のPCRPlayerをEnhanced RTMP (今のところv1)に対応させたものです。
HEVCとAV1の映像を想定しています。
ついでにBBS表示周りがIEコンポーネントを使っていたので、Webview2に変更しました。











Visual Studio Community 2026 / Visual C++ 2015 で開発しています

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
