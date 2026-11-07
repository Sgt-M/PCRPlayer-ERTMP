# PCRPlayer ビルド手順 (VS2026 / port/vs2026ブランチ)

対象: Visual Studio 2026 Community (MSVC v145 / 14.51) + Windows SDK 10.0.26100.0  
XP対応(toolset v140_xp)は廃止。**Windows 10/11 想定**。

## 依存関係の準備 (1回だけ)

### 1. Boost 1.91.0 (vcpkg static)
```
C:\vcpkg\vcpkg.exe install boost-xpressive boost-thread boost-filesystem boost-date-time ^
  boost-iostreams boost-property-tree boost-format boost-optional boost-algorithm ^
  boost-lexical-cast boost-tokenizer boost-function boost-function-types ^
  --triplet x86-windows-static
```
x64でビルドする場合は `--triplet x64-windows-static` も同様に実行。  
※ CWDが `mimocode` 配下だと上位の `vcpkg.json` に吸われるので注意 (`VCPKG_MANIFEST_MODE=off` を付けるか別の場所で実行)。  
※ vcxproj の `$(VcpkgRoot)` (既定 `C:\vcpkg`) と `$(BoostTriplet)` で解決される。

### 2. DirectShow BaseClasses (strmbase.lib / strmbasd.lib)
```
git clone --depth 1 --filter=blob:none --sparse https://github.com/microsoft/Windows-classic-samples ^
  third_party\directshow-baseclasses\src
git -C third_party\directshow-baseclasses\src sparse-checkout set ^
  Samples\Win7Samples\multimedia\directshow\baseclasses
```
ビルドは **`/DUNICODE /D_UNICODE` 必須** (アプリ側がUnicodeのため、無いとCBaseFilterのリンクが落ちる)。
```
third_party\directshow-baseclasses\build-baseclasses.bat   ※ x86のみ。x64はVCのDeveloper promptで同 flags を参照して手動 or port/vs2026環境で生成済 lib を使用
```
出力先: `third_party\directshow-baseclasses\lib\{x86,x64}\strmbase.lib, strmbasd.lib`  
flags: `/EHsc /W3 /MT(d) /DWIN32 /D_WINDOWS /DUNICODE /D_UNICODE /D_STDC_WANT_LIB_EXT1_=1 /I<src>` + lib.exe まとめ。

### 3. VS2026コンポーネント
- 「C++ MFCをv145ビルドツールUsed (x86 & x64)」 (`Microsoft.VisualStudio.Component.VC.14.51.MFC`)  
  → 未導入なら VS Installer → 変更 → 個々のコンポーネント。

## ビルド
```
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" ^
  PCRPlayer.sln -p:Configuration=Release -p:Platform=Win32 -m
```
4構成 (Debug/Release × Win32/x64) すべて通ることを確認済み (2026-11-07)。

## ソース変更の要点 (原状との差分、すべて `//[port]` マーカーあり)
| ファイル | 内容 |
|---|---|
| `PCRPlayer.vcxproj` | v140_xp→v145 / SDK 8.1→10.0.26100 / BaseClasses・Boostパス $(SolutionDir)/$(VcpkgRoot) 化 / BOOST_ALL_NO_LIB + 構成別明示リンク / Debugにdebug\lib追加 |
| `VideoRenderer.h` | 未使用の `#include <qedit.h>` をコメント化 (現行SDKに存在しない) |
| `EVRCustom/D3DUtl.h/.cpp` | **D3DX(d3dx9)依存撤去**: shaderコンパイル/逆アセンブルを `d3dcompiler_47.dll`(Windows同梱) の `D3DCompile`/`D3DDisassemble` に置換。CreateTextureはD3D9 API直行。型は `D3D_SHADER_MACRO≡D3DXMACRO`、`ID3DBlob≡ID3DXBuffer` |
| `Network.cpp` | `file_.assign(base(), path_.end())` → `path_.cend()` (現行MSVC STLの同一イテレータ型制約対応) |

## 运行时依赖
- DLL追加配布不要 (d3dx9_43.dll は不要化済み。d3dcompiler_47.dll / D3D9 / EVR / MFC静的リンク)
- PeerCast視聴用: 別途 PeerCastノード or YP URL は従来通りアプリ設定
