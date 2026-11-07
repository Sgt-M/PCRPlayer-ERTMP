#pragma once

//[port] D3DX(d3dx9)依存撤去: 標準同梱の d3dcompiler で置換
#include <d3dcompiler.h>

//[port] D3DXMACRO 互換(D3D_SHADER_MACRO は固定長配列のため別定義)
struct D3DXMACRO
{
	LPCSTR Name;
	LPCSTR Definition;
};

//[port] ID3DXBuffer/ID3DXInclude 互換: ID3DBlob/ID3DInclude で代用
typedef ID3DBlob ID3DXBuffer;
typedef ID3DXBuffer* LPD3DXBUFFER;
typedef ID3DInclude* LPD3DXINCLUDE;
#include "DX9Shaders.h"

namespace d3dutl {

typedef enum {
	RESIZER_UNKNOWN = -1,
	RESIZER_DEFAULT = 0,
	RESIZER_NEAREST,
	RESIZER_BILINEAR,
	RESIZER_SHADER_NEAREST,
	RESIZER_SHADER_BILINEAR,
	RESIZER_SHADER_SMOOTHERSTEP,
	RESIZER_SHADER_BICUBIC06,
	RESIZER_SHADER_BICUBIC08,
	RESIZER_SHADER_BICUBIC10,
	RESIZER_SHADER_BSPLINE4,
	RESIZER_SHADER_MITCHELL4,
	RESIZER_SHADER_CATMULL4,
	RESIZER_SHADER_2PASS_BICUBIC06,
	RESIZER_SHADER_2PASS_BICUBIC08,
	RESIZER_SHADER_2PASS_BICUBIC10,
	RESIZER_SHADER_2PASS_BSPLINE4,
	RESIZER_SHADER_2PASS_MITCHELL4,
	RESIZER_SHADER_2PASS_CATMULL4,
	RESIZER_SHADER_2PASS_LANCZOS2,
	RESIZER_SHADER_2PASS_LANCZOS3,
} RESIZER_TYPE;

typedef enum {
	shader_nearest,
	shader_bilinear,
	shader_smootherstep,
	shader_bicubic06,
	shader_bicubic08,
	shader_bicubic10,
	shader_bspline4,
	shader_mitchell4,
	shader_catmull4,
	shader_bicubic06_x,
	shader_bicubic06_y,
	shader_bicubic08_x,
	shader_bicubic08_y,
	shader_bicubic10_x,
	shader_bicubic10_y,
	shader_bspline4_x,
	shader_bspline4_y,
	shader_mitchell4_x,
	shader_mitchell4_y,
	shader_catmull4_x,
	shader_catmull4_y,
	shader_lanczos2_x,
	shader_lanczos2_y,
	shader_lanczos3_x,
	shader_lanczos3_y,
	shader_downscaling_x,
	shader_downscaling_y,
	shader_downscaling,
	shader_count
} shader_type;



HINSTANCE GetD3X9Dll();

class D3DLoader {

	//[port] d3dcompiler 側関数のシグネチャ
	typedef HRESULT (WINAPI * D3DCompilePtr) (
			LPCVOID		pSrcData,
			SIZE_T		SrcDataSize,
			LPCSTR		pSourceName,
			const D3D_SHADER_MACRO*	pDefines,
			ID3DInclude*	pInclude,
			LPCSTR		pEntrypoint,
			LPCSTR		pTarget,
			UINT		Flags1,
			UINT		Flags2,
			ID3DBlob**	ppCode,
			ID3DBlob**	ppErrorMsgs);

	typedef HRESULT (WINAPI * D3DDisassemblePtr) (
			LPCVOID		pSrcData,
			SIZE_T		SrcDataSize,
			UINT		Flags,
			LPCSTR		szComments,
			ID3DBlob**	ppDisassembly);

	static D3DCompilePtr D3DCompile_;
	static D3DDisassemblePtr D3DDisassemble_;



	D3DLoader();
	~D3DLoader();
public:
	static void Init()
	{
		static D3DLoader init;
	}

	static HRESULT WINAPI D3DXCreateTexture(
			LPDIRECT3DDEVICE9         pDevice,
			UINT                      Width,
			UINT                      Height,
			UINT                      MipLevels,
			DWORD                     Usage,
			D3DFORMAT                 Format,
			D3DPOOL                   Pool,
			LPDIRECT3DTEXTURE9*       ppTexture)
	{
		//[port] D3DXCreateTexture は単純な生成ラッパーのため Direct3D9 API に直行
		if (!pDevice || !ppTexture) { return E_INVALIDARG; }
		return pDevice->CreateTexture(Width, Height, MipLevels, Usage, Format, Pool, ppTexture, NULL);
	}

	static HRESULT WINAPI D3DXCompileShader(
			LPCSTR		pSrcData,
			UINT		SrcDataLen,
			CONST D3DXMACRO* pDefines,
			LPD3DXINCLUDE	pInclude,
			LPCSTR		pFunctionName,
			LPCSTR		pProfile,
			DWORD		Flags,
			LPD3DXBUFFER*	ppShader,
			LPD3DXBUFFER*	ppErrorMsgs,
			void*		/*ppConstantTable*/)
	{
		if (D3DCompile_)
		{
			//[port] D3DXMACRO(LPCSTR) -> D3D_SHADER_MACRO(char[128]) 変換
			D3D_SHADER_MACRO macros[8];
			int n = 0;
			while (pDefines && n < 7 && (pDefines[n].Name || pDefines[n].Definition))
			{
				strncpy_s(macros[n].Name, pDefines[n].Name ? pDefines[n].Name : "", _TRUNCATE);
				strncpy_s(macros[n].Definition, pDefines[n].Definition ? pDefines[n].Definition : "", _TRUNCATE);
				n++;
			}
			macros[n].Name[0] = 0;
			macros[n].Definition[0] = 0;

			return D3DCompile_(pSrcData, (SIZE_T)SrcDataLen, NULL, (n > 0 ? macros : NULL),
				pInclude, pFunctionName, pProfile, Flags, 0, ppShader, ppErrorMsgs);
		}
		return E_NOTIMPL;
	}


	static HRESULT WINAPI D3DXDisassembleShader(
			CONST DWORD*	pShader,
			UINT		ShaderSize,
			bool		EnableColorCode,
			LPCSTR		pComments,
			LPD3DXBUFFER*	ppDisassembly)
	{
		//[port] D3DDisassemble はサイズ必須のため ShaderSize 追加
		if (D3DDisassemble_)
		{
			return D3DDisassemble_(pShader, (SIZE_T)ShaderSize, EnableColorCode ? D3DIS_ENABLE_COLOR : 0, pComments, ppDisassembly);
		}
		return E_NOTIMPL;
	}
};



class D3DResizer {
	IDirect3DDevice9Ex* device_;
	D3DCAPS9 caps_;
	LPCSTR shaderProfile_;
	CComPtr<IDirect3DPixelShader9> pixelShaders_[shader_count];
	CComPtr<IDirect3DTexture9> resizeTexture_;

private:
	HRESULT InitShaderResizer(int iShader);

	HRESULT TextureResize(IDirect3DTexture9* pTexture, const CRect &srcRect, const CRect &dstRect, D3DTEXTUREFILTERTYPE filter);
	HRESULT TextureResizeShader(IDirect3DTexture9* pTexture, const CRect &srcRect, const CRect &dstRect, D3DTEXTUREFILTERTYPE filter, int iShader, bool bDownscale);
	HRESULT TextureResizeShader2pass(IDirect3DTexture9* pTexture, const CRect &srcRect, const CRect &dstRect, D3DTEXTUREFILTERTYPE filter, int iShader1, bool bDownscale);

public:
	D3DResizer(IDirect3DDevice9Ex* device);

	HRESULT ResizeRect(IDirect3DSurface9* src, const RECT* pSrcRect, IDirect3DSurface9* dst, const RECT* pDstRect, int resizer);
};

} // namespace d3dutl