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

リリースファイルには
MPC Video Renderer		https://github.com/Aleksoid1978/VideoRenderer	GPL3.0
および
LAV Filters				https://github.com/Nevcairiel/LAVFilters		GPL2.0
のバイナリが含まれています。
