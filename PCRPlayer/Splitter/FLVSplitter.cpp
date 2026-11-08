/* 
 *  Copyright (C) 2003-2006 Gabest
 *  http://www.gabest.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *   
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *   
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "../stdafx.h"
#include "FLVSplitter.h"
//#include "../../../DSUtil/DSUtil.h"

#include <initguid.h>
#include "moreuuids.h"



#ifdef _DEBUG
#	define DebugText(str, ...) \
	{ \
		wchar_t c[1024]; \
		swprintf_s(c, str, __VA_ARGS__); \
		OutputDebugString(c); \
	}
#else
#    define DebugText(str, ...) // 空実装
#endif



#ifdef REGISTER_FILTER

const AMOVIESETUP_MEDIATYPE sudPinTypesIn[] =
{
	{&MEDIATYPE_Stream, &MEDIASUBTYPE_FLV},
	{&MEDIATYPE_Stream, &MEDIASUBTYPE_NULL},
};

const AMOVIESETUP_PIN sudpPins[] =
{
	{L"Input", FALSE, FALSE, FALSE, FALSE, &CLSID_NULL, NULL, countof(sudPinTypesIn), sudPinTypesIn},
	{L"Output", FALSE, TRUE, FALSE, FALSE, &CLSID_NULL, NULL, 0, NULL}
};

const AMOVIESETUP_MEDIATYPE sudPinTypesOut2[] =
{
	{&MEDIATYPE_Video, &MEDIASUBTYPE_NULL},
};

const AMOVIESETUP_FILTER sudFilter[] =
{
	{&__uuidof(CFLVSplitterFilter), L"MPC - FLV Splitter (Gabest)", MERIT_NORMAL, countof(sudpPins), sudpPins, CLSID_LegacyAmFilterCategory},
	{&__uuidof(CFLVSourceFilter), L"MPC - FLV Source (Gabest)", MERIT_NORMAL, 0, NULL, CLSID_LegacyAmFilterCategory},
};

CFactoryTemplate g_Templates[] =
{
	{sudFilter[0].strName, sudFilter[0].clsID, CreateInstance<CFLVSplitterFilter>, NULL, &sudFilter[0]},
	{sudFilter[1].strName, sudFilter[1].clsID, CreateInstance<CFLVSourceFilter>, NULL, &sudFilter[1]},
};

int g_cTemplates = countof(g_Templates);

STDAPI DllRegisterServer()
{
	DeleteRegKey(_T("Media Type\\Extensions\\"), _T(".flv"));

	RegisterSourceFilter(CLSID_AsyncReader, MEDIASUBTYPE_FLV, _T("0,4,,464C5601"), NULL);

	return AMovieDllRegisterServer2(TRUE);
}

STDAPI DllUnregisterServer()
{
	UnRegisterSourceFilter(MEDIASUBTYPE_FLV);

	return AMovieDllRegisterServer2(FALSE);
}

#include "../../FilterApp.h"

CFilterApp theApp;

#endif

//
// CFLVSplitterFilter
//

CFLVSplitterFilter::CFLVSplitterFilter(LPUNKNOWN pUnk, HRESULT* phr, utl::SyncLog& log)
	: CBaseSplitterFilter(NAME("CFLVSplitterFilter"), pUnk, phr, __uuidof(this))
	, Logger(log, utl::Log::LOGTYPE_FILTER)
	, currentPos_(LONGLONG_MIN)
	, positionDiff_(LONGLONG_MIN)
{
}

bool CFLVSplitterFilter::ReadTag(Tag& t)
{
	if (m_pFile->GetRemaining() < 15) { return false; }

	t.PreviousTagSize = (UINT32)m_pFile->BitRead(32);
	t.TagType = (BYTE)m_pFile->BitRead(8);
	t.DataSize = (UINT32)m_pFile->BitRead(24);
	t.TimeStamp = (UINT32)m_pFile->BitRead(24);
	t.TimeStamp |= (UINT32)m_pFile->BitRead(8) << 24;
	t.StreamID = (UINT32)m_pFile->BitRead(24);

	return m_pFile->GetRemaining() >= t.DataSize;
}


// バッファがlength以上貯まるまで待機
bool CFLVSplitterFilter::WaitForData(LONGLONG length)
{
	if (m_pFile->IsStreaming())
	{
		int i = 0;
		while (m_pFile->GetRemaining() < length)
		{
			if (m_pFile->GetAvailable() < 0 || i >= 5000)
			{// バッファリング中断(GetAvailable()がマイナス)かループ回数で終了
				return false;
			}

			Sleep(1);
			i++;
		}
		return true;
	}

	if (m_pFile->GetRemaining() < length)
	{
		return false;
	}
	return true;
}

bool CFLVSplitterFilter::ReadTag(Tag& t, UINT32& previousTagSize, bool sync, bool& error)
{
	if (!WaitForData(15))
	{// タグサイズの確保失敗
		NotifyEvent(EC_USERABORT, E_ABORT, USERABORT_TAG_SIZE);
		return false;
	}

	bool res = ReadTag(t);

	if (!m_IgnorePrevSizes)
	{
		// 最初のPreviousTagSizeは0 以後は前のDataSize+11(TagSize);
		if (previousTagSize != 0 && t.PreviousTagSize != previousTagSize)
		{
			LogTag(t, L"不正タグ (PreviousTagSize != TagSize)", previousTagSize);
			if (sync)
			{
				error = true;
				return SyncReadTag(t, previousTagSize);
			}
			else
			{
				NotifyEvent(EC_USERABORT, E_ABORT, USERABORT_PREVIOUS_TAG_SIZE);
				return false;
			}
		}
	}

	if (!res)
	{
		if (t.StreamID != 0)
		{
			LogTag(t, L"不正タグ (StreamID != 0)", previousTagSize);
			if (sync)
			{
				error = true;
				return SyncReadTag(t, previousTagSize);
			}
			else
			{
				NotifyEvent(EC_USERABORT, E_ABORT, USERABORT_STREAM_ID);
				return false;
			}
		}

		if (!WaitForData(t.DataSize))
		{// DataSizeの確保失敗
			NotifyEvent(EC_USERABORT, E_ABORT, USERABORT_DATA_SIZE);
			return false;
		}
	}

	previousTagSize = t.DataSize + 11;
	return true;
}

bool CFLVSplitterFilter::SyncReadTag(Tag& t, UINT32& previousTagSize)
{
	LONGLONG pos = m_pFile->GetPos();
	if (Sync(pos))
	{
		if (!WaitForData(15))
		{// タグサイズの確保失敗
			NotifyEvent(EC_USERABORT, E_ABORT, USERABORT_TAG_SIZE);
			return false;
		}

		bool res = ReadTag(t);

		if (!res)
		{
			if (t.StreamID != 0)
			{
				LogTag(t, L"不正タグ (StreamID != 0)", previousTagSize);
				NotifyEvent(EC_USERABORT, E_ABORT, USERABORT_STREAM_ID);
				return false;
			}

			if (!WaitForData(t.DataSize))
			{// DataSizeの確保失敗
				NotifyEvent(EC_USERABORT, E_ABORT, USERABORT_DATA_SIZE);
				return false;
			}
		}

		LogTag(t, L"再同期 (Sync)");

		previousTagSize = t.DataSize + 11;
		return true;
	}
	LogTag(t, L"再同期失敗 (Sync Error)", previousTagSize);
	NotifyEvent(EC_USERABORT, E_ABORT, USERABORT_SYNC_ERROR);
	return false;
}

void CFLVSplitterFilter::LogTag(const Tag& t, const std::wstring& str)
{
	detailLog(str,
		L"Position:%lld\r\n"
		L"PreviousTagSize:%u\r\n"
		L"TagType:%u\r\n"
		L"DataSize:%u\r\n"
		L"TimeStamp:%u\r\n"
		L"StreamID:%u"
		, m_pFile->GetPos()
		, t.PreviousTagSize
		, t.TagType
		, t.DataSize
		, t.TimeStamp
		, t.StreamID);
}

void CFLVSplitterFilter::LogTag(const Tag& t, const std::wstring& str, UINT32 previousTagSize)
{
	detailLog(str,
		L"Position:%lld\r\n"
		L"TagSize:%u\r\n"
		L"PreviousTagSize:%u\r\n"
		L"TagType:%u\r\n"
		L"DataSize:%u\r\n"
		L"TimeStamp:%u\r\n"
		L"StreamID:%u"
		, m_pFile->GetPos()
		, previousTagSize
		, t.PreviousTagSize
		, t.TagType
		, t.DataSize
		, t.TimeStamp
		, t.StreamID);
}

void CFLVSplitterFilter::LogTag(const Tag& t, const std::wstring& str, LONGLONG diff)
{
	detailLog(str,
		L"Position:%lld\r\n"
		L"TimeStampDifference:%lld\r\n"
		L"PreviousTagSize:%u\r\n"
		L"TagType:%u\r\n"
		L"DataSize:%u\r\n"
		L"TimeStamp:%u\r\n"
		L"StreamID:%u"
		, m_pFile->GetPos()
		, diff
		, t.PreviousTagSize
		, t.TagType
		, t.DataSize
		, t.TimeStamp
		, t.StreamID);
}


bool CFLVSplitterFilter::ReadTag(AudioTag& at)
{
	if(!m_pFile->GetRemaining()) 
		return false;

	//[pcr-ertmp] byte0 layout: SoundFormat(4)|Rate(2)|Size(1)|Type(1).
	//SoundFormat == 9 is the IsExAudioHeader marker of Enhanced RTMP: the low
	//nibble is then the packet type and a big-endian UI32 fourCC follows.
	BYTE b0 = (BYTE)m_pFile->BitRead(8);

	at.SoundFormat = (b0 >> 4) & 0x0F;
	at.SoundRate = (b0 >> 2) & 0x03;
	at.SoundSize = (b0 >> 1) & 0x01;
	at.SoundType = b0 & 0x01;
	at.IsEx = (at.SoundFormat == 9);
	at.PacketType = 0;
	at.FourCC = 0;

	if (at.IsEx) {
		at.PacketType = b0 & 0x0F;
		if (!m_pFile->GetRemaining()) { return false; }
		at.FourCC = _byteswap_ulong((UINT32)m_pFile->BitRead(32)); // -> MAKEFOURCC order
	}

	return true;
}

bool CFLVSplitterFilter::ReadTag(VideoTag& vt)
{
	if(!m_pFile->GetRemaining()) 
		return false;

	//[pcr-ertmp] byte0 layout: IsExHeader(1)|FrameType(3)|PacketType(4).
	//When IsExHeader is set the CodecID nibble must NOT be read as a codec id;
	//a big-endian UI32 fourCC follows and the packet type selects the rest of
	//the header. (veovera/enhanced-rtmp v1; matches FFmpeg's flv demuxer.)
	BYTE b0 = (BYTE)m_pFile->BitRead(8);

	vt.IsEx = (b0 & 0x80) != 0;
	vt.FrameType = (b0 >> 4) & 0x07;
	vt.CodecID = b0 & 0x0F;
	vt.PacketType = 0;
	vt.FourCC = 0;
	vt.TsOffset = 0;

	if (!vt.IsEx) {
		return true;
	}

	vt.PacketType = vt.CodecID;

	if (!m_pFile->GetRemaining()) { return false; }
	vt.FourCC = _byteswap_ulong((UINT32)m_pFile->BitRead(32)); // -> MAKEFOURCC order

	// PacketTypeCodedFrames carries the SI24 composition time offset; every
	// other packet type has no timing field (SequenceStart carries the codec
	// configuration record, Metadata carries AMF, and the rest are not
	// delivered).
	if (vt.PacketType == 1) {
		if (!m_pFile->GetRemaining()) { return false; }
		UINT32 ts = (UINT32)m_pFile->BitRead(24);
		vt.TsOffset = (ts + 0xff800000) ^ 0xff800000; // sign extension
	}

	return true;
}

//[pcr-ertmp] Enhanced RTMP support helpers ---------------------------------
// BitRead(32) yields wire order (b0<<24|...); _byteswap_ulong converts it to
// MAKEFOURCC order used by all comparisons ('hvc1' wire 0x68766331 -> 0x31637668).
UINT32 CFLVSplitterFilter::MakeFourCC(UINT32 be)
{
	return _byteswap_ulong(be);
}

bool CFLVSplitterFilter::IsEnhancedVideoFourCC(UINT32 cc)
{
	switch (cc) {
	case MAKEFOURCC('a','v','c','1'): case MAKEFOURCC('a','v','c','C'):
	case MAKEFOURCC('h','v','c','1'): case MAKEFOURCC('h','v','c','C'):
	case MAKEFOURCC('h','e','v','1'):
	case MAKEFOURCC('a','v','0','1'):
	case MAKEFOURCC('v','p','0','8'): case MAKEFOURCC('v','p','0','9'):
		return true;
	}
	return false;
}

bool CFLVSplitterFilter::IsEnhancedAudioFourCC(UINT32 cc)
{
	switch (cc) {
	case MAKEFOURCC('m','p','4','a'):
	case MAKEFOURCC('O','p','u','s'):
	case MAKEFOURCC('f','L','a','C'):
	case MAKEFOURCC('a','c','-','3'):
	case MAKEFOURCC('e','c','-','3'):
		return true;
	}
	return false;
}

namespace {
// small bit reader over a memory blob (config records are parsed off-stream)
struct ErtmpBitStream {
	const BYTE* p; size_t n; size_t bit;
	ErtmpBitStream(const BYTE* d, size_t bytes) : p(d), n(bytes), bit(0) {}
	UINT64 read(size_t bits)
	{
		UINT64 v = 0;
		for (size_t i = 0; i < bits; ++i) {
			if ((bit >> 3) >= n) { return 0; }
			v = (v << 1) | ((p[bit >> 3] >> (7 - (bit & 7))) & 1);
			++bit;
		}
		return v;
	}
	void skip(size_t bits) { bit += bits; }
	UINT64 uexp()
	{
		size_t zeros = 0;
		while ((bit >> 3) < n && read(1) == 0) { ++zeros; if (zeros > 31) { return 0; } }
		// leading '1' bit + zeros suffix bits: value = info - 1
		return ((1ULL << zeros) | read(zeros)) - 1;
	}
};

// profile_tier_level(1, maxSub) bit-exact skip (HEVC)
void hevc_skip_ptl(ErtmpBitStream& bs, UINT32 maxSub)
{
	bs.skip(96); // general block: 12 bytes
	if (maxSub > 0) {
		UINT32 prof = (UINT32)bs.read(8);
		UINT32 lvl = (UINT32)bs.read(8);
		for (UINT32 i = maxSub; i < 8; ++i) { bs.skip(2); }
		for (UINT32 i = 0; i < maxSub; ++i) {
			if (prof & (0x80u >> i)) { bs.skip(88); }
			if (lvl & (0x80u >> i)) { bs.skip(8); }
		}
	}
}
} // namespace

//[pcr-ertmp] Build candidate media types from an enhanced-video sequence-start
//configuration record located at the current file position. Several subtype
//spellings are added so the graph builder finds one the installed decoder
//accepts (LAV registers with FFmpeg-style fourCC subtypes).
bool CFLVSplitterFilter::BuildEnhancedVideoTypes(UINT32 fourcc, CMediaType&,
	CAtlArray<CMediaType>& mts, UINT32& w, UINT32& h)
{
	w = h = 0;

	__int64 avail = m_pFile->GetRemaining();
	if (avail <= 0 || avail > 512 * 1024) { return false; }

	std::vector<BYTE> cfg((size_t)avail);
	m_pFile->ByteRead(cfg.data(), (DWORD)cfg.size());

	CMediaType mt;
	mt.SetSampleSize(1);
	mt.majortype = MEDIATYPE_Video;
	mt.formattype = FORMAT_MPEG2Video;

	MPEG2VIDEOINFO* vih = (MPEG2VIDEOINFO*)mt.AllocFormatBuffer(
		FIELD_OFFSET(MPEG2VIDEOINFO, dwSequenceHeader) + (DWORD)cfg.size());
	memset(vih, 0, mt.FormatLength());
	vih->hdr.bmiHeader.biSize = sizeof(vih->hdr.bmiHeader);
	vih->hdr.bmiHeader.biPlanes = 1;
	vih->hdr.bmiHeader.biBitCount = 24;
	vih->cbSequenceHeader = (DWORD)cfg.size();
	memcpy(vih->dwSequenceHeader, cfg.data(), cfg.size());

	if (fourcc == MAKEFOURCC('h','v','c','1') || fourcc == MAKEFOURCC('h','v','c','C')
		|| fourcc == MAKEFOURCC('h','e','v','1')) {
		// HEVCDecoderConfigurationRecord (ISO 14496-15): 23 fixed bytes
		if (cfg.size() < 23 || cfg[0] != 1) { return false; }
		vih->dwProfile = cfg[1] & 0x1F;   // general_profile_idc
		vih->dwLevel = cfg[12];           // general_level_idc
		vih->dwFlags = (cfg[21] & 3) + 1; // nal length size minus one

		// walk the NAL arrays for the SPS (type 33) and derive coded size
		size_t o = 23;
		UINT32 numArrays = cfg[22] & 0x3F;
		for (UINT32 a = 0; a < numArrays && o + 3 <= cfg.size(); ++a) {
			UINT32 nalType = cfg[o] & 0x3F;
			UINT32 numNalus = (cfg[o + 1] << 8) | cfg[o + 2];
			o += 3;
			for (UINT32 k = 0; k < numNalus && o + 2 <= cfg.size(); ++k) {
				UINT32 len = (cfg[o] << 8) | cfg[o + 1];
				if (o + 2 + len > cfg.size()) { break; }
				if (nalType == 33 && len > 2) {
					// HEVC NAL header is 2 bytes; hvcC NALs are stored with
					// emulation prevention bytes (00 00 03) that must be
					// removed before bit parsing the SPS RBSP.
					const BYTE* rp = cfg.data() + o + 2;
					size_t rn = len - 2;
					std::vector<BYTE> epb;
					epb.reserve(rn);
					for (size_t q = 0; q < rn; ) {
						if (q + 2 < rn && rp[q] == 0 && rp[q + 1] == 0 && rp[q + 2] == 3) {
							epb.push_back(0); epb.push_back(0); q += 3;
						} else {
							epb.push_back(rp[q]); ++q;
						}
					}
					ErtmpBitStream bs(epb.data() + 2, epb.size() - 2);
					bs.skip(4);                         // sps_video_parameter_set_id
					UINT32 maxSub = (UINT32)bs.read(3); // sps_max_sub_layers_minus1
					bs.skip(1);                         // temporal_id_nesting_flag
					hevc_skip_ptl(bs, maxSub);
					bs.uexp();                          // sps_seq_parameter_set_id
					UINT32 chroma = (UINT32)bs.uexp();  // chroma_format_idc
					if (chroma == 3) { bs.skip(1); }    // separate_colour_plane_flag
					UINT32 pw = (UINT32)bs.uexp();
					UINT32 ph = (UINT32)bs.uexp();
					UINT32 subW = (chroma == 3) ? 1 : 2;
					UINT32 subH = (chroma == 1) ? 2 : 1;
					if (bs.read(1)) {                   // conformance_window_flag
						pw -= (UINT32)(bs.uexp() + bs.uexp()) * subW;
						ph -= (UINT32)(bs.uexp() + bs.uexp()) * subH;
					}
					w = pw; h = ph;
					break;
				}
				o += 2 + len;
			}
			if (w && h) { break; }
		}
		if (!w || !h) { addLog(L"ertmp: HEVC SPS size parse failed"); }
		vih->hdr.bmiHeader.biWidth = (LONG)w;
		vih->hdr.bmiHeader.biHeight = (LONG)h;
		vih->hdr.dwPictAspectRatioX = (LONG)(w ? w : 16);
		vih->hdr.dwPictAspectRatioY = (LONG)(h ? h : 9);

		const UINT32 subs[] = { MAKEFOURCC('H','E','V','C'), MAKEFOURCC('h','v','c','1'),
			MAKEFOURCC('h','e','v','1'), MAKEFOURCC('H','V','C','1'), MAKEFOURCC('h','e','v','c') };
		for (size_t i = 0; i < _countof(subs); ++i) {
			CMediaType m = mt;
			m.subtype = FOURCCMap(subs[i]);
			mts.Add(m);
		}
		return true;
	}

	if (fourcc == MAKEFOURCC('a','v','0','1')) {
		// AV1CodecConfigurationRecord: 4 fixed bytes + configOBUs. Dimensions
		// are left at 0; the FFmpeg-based decoder reads them from the
		// sequence header OBU.
		if (cfg.size() < 4) { return false; }
		vih->hdr.bmiHeader.biWidth = 0;
		vih->hdr.bmiHeader.biHeight = 0;
		vih->hdr.dwPictAspectRatioX = 16;
		vih->hdr.dwPictAspectRatioY = 9;
		const UINT32 subs[] = { MAKEFOURCC('A','V','0','1'), MAKEFOURCC('A','V','1',' '),
			MAKEFOURCC('a','v','0','1') };
		for (size_t i = 0; i < _countof(subs); ++i) {
			CMediaType m = mt;
			m.subtype = FOURCCMap(subs[i]);
			mts.Add(m);
		}
		return true;
	}

	if (fourcc == MAKEFOURCC('v','p','0','8') || fourcc == MAKEFOURCC('v','p','0','9')) {
		if (cfg.size() < 4) { return false; }
		vih->hdr.bmiHeader.biWidth = 0;
		vih->hdr.bmiHeader.biHeight = 0;
		vih->hdr.dwPictAspectRatioX = 16;
		vih->hdr.dwPictAspectRatioY = 9;
		const UINT32 subs[] = { MAKEFOURCC('V','P','9','0'), MAKEFOURCC('V','P','9',' '),
			MAKEFOURCC('v','p','0','9'), MAKEFOURCC('V','P','8','0'), MAKEFOURCC('v','p','0','8') };
		for (size_t i = 0; i < _countof(subs); ++i) {
			CMediaType m = mt;
			m.subtype = FOURCCMap(subs[i]);
			mts.Add(m);
		}
		return true;
	}

	// 'avc1'/'avcC' carried enhanced: not handled (legacy path unaffected)
	return false;
}

//[pcr-ertmp] Build candidate audio media types from an enhanced-audio config
//record (AudioSpecificConfig / OpusHead / FLAC STREAMINFO).
bool CFLVSplitterFilter::BuildEnhancedAudioTypes(UINT32 fourcc, const BYTE* cfg,
	DWORD cfgSize, CAtlArray<CMediaType>& mts)
{
	if (!cfg || cfgSize == 0) { return false; }

	if (fourcc == MAKEFOURCC('m','p','4','a')) {
		if (cfgSize < 2) { return false; }
		ErtmpBitStream bs(cfg, cfgSize);
		bs.read(5); // audioObjectType
		UINT32 idx = (UINT32)bs.read(4);
		UINT32 chan = (UINT32)bs.read(4);
		static const int rates[] = { 96000,88200,64000,48000,44100,32000,24000,
			22050,16000,12000,11025,8000,7350 };
		if (idx > 12 || chan > 7 || chan == 0) { return false; }

		CMediaType mt;
		mt.SetSampleSize(1);
		mt.majortype = MEDIATYPE_Audio;
		mt.subtype = FOURCCMap(WAVE_FORMAT_AAC);
		mt.formattype = FORMAT_WaveFormatEx;
		WAVEFORMATEX* wfe = (WAVEFORMATEX*)mt.AllocFormatBuffer(sizeof(WAVEFORMATEX) + cfgSize);
		memset(wfe, 0, mt.FormatLength());
		wfe->wFormatTag = WAVE_FORMAT_AAC;
		wfe->nChannels = (WORD)chan;
		wfe->nSamplesPerSec = rates[idx];
		wfe->wBitsPerSample = 16;
		wfe->nBlockAlign = 1;
		wfe->cbSize = (WORD)cfgSize;
		memcpy(wfe + 1, cfg, cfgSize);
		mts.Add(mt);
		return true;
	}

	if (fourcc == MAKEFOURCC('O','p','u','s')) {
		// OpusHead: magic 'OpusHead'(8) version(1) channels(1) preSkip(2)
		// rate(4) gain(2) mapping(1)
		if (cfgSize < 16) { return false; }
		BYTE channels = cfg[9];
		if (channels == 0 || channels > 8) { return false; }

		static const GUID SUBTYPE_OPUS =
			{ 0x2C711410, 0xFDC2, 0x4E19, { 0xBA, 0x7D, 0x7B, 0x47, 0x50, 0xC8, 0x47, 0x44 } };

		CMediaType mt;
		mt.SetSampleSize(1);
		mt.majortype = MEDIATYPE_Audio;
		mt.subtype = SUBTYPE_OPUS;
		mt.formattype = FORMAT_WaveFormatEx;
		WAVEFORMATEXTENSIBLE* wfx = (WAVEFORMATEXTENSIBLE*)mt.AllocFormatBuffer(
			sizeof(WAVEFORMATEXTENSIBLE) + cfgSize);
		memset(wfx, 0, sizeof(WAVEFORMATEXTENSIBLE) + cfgSize);
		wfx->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
		wfx->Format.nChannels = channels;
		wfx->Format.nSamplesPerSec = 48000; // RFC 7845: container always states 48k
		wfx->Format.wBitsPerSample = 16;
		wfx->Format.nBlockAlign = (WORD)(channels * 2);
		wfx->Format.nAvgBytesPerSec = 48000 * channels * 2;
		wfx->Format.cbSize = 22 + (WORD)cfgSize;
		wfx->Samples.wValidBitsPerSample = 16;
		wfx->dwChannelMask = (channels == 1) ? SPEAKER_FRONT_CENTER
			: (channels == 2) ? (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT) : 0;
		wfx->SubFormat = SUBTYPE_OPUS;
		memcpy((BYTE*)wfx + sizeof(WAVEFORMATEXTENSIBLE), cfg, cfgSize);
		mts.Add(mt);
		return true;
	}

	if (fourcc == MAKEFOURCC('f','L','a','C')) {
		// STREAMINFO: min/max block(16+16) min/max frame(24+24) rate(20)
		// channels-1(3) bits-1(5) total samples(36)
		if (cfgSize < 34) { return false; }
		ErtmpBitStream bs(cfg, cfgSize);
		bs.skip(16 + 16 + 24 + 24);
		UINT32 rate = (UINT32)bs.read(20);
		UINT32 chan = (UINT32)bs.read(3) + 1;
		UINT32 bits = (UINT32)bs.read(5) + 1;
		if (rate == 0 || chan == 0) { return false; }

		static const GUID SUBTYPE_FLAC =
			{ 0x9FAD7850, 0xB6B4, 0x4AB5, { 0x88, 0x2B, 0xAF, 0x92, 0x47, 0x4F, 0x95, 0xFD } };

		CMediaType mt;
		mt.SetSampleSize(1);
		mt.majortype = MEDIATYPE_Audio;
		mt.subtype = SUBTYPE_FLAC;
		mt.formattype = FORMAT_WaveFormatEx;
		WAVEFORMATEXTENSIBLE* wfx = (WAVEFORMATEXTENSIBLE*)mt.AllocFormatBuffer(
			sizeof(WAVEFORMATEXTENSIBLE) + cfgSize);
		memset(wfx, 0, sizeof(WAVEFORMATEXTENSIBLE) + cfgSize);
		wfx->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
		wfx->Format.nChannels = (WORD)chan;
		wfx->Format.nSamplesPerSec = rate;
		wfx->Format.wBitsPerSample = (WORD)bits;
		wfx->Format.nBlockAlign = (WORD)(chan * ((bits + 7) / 8));
		wfx->Format.nAvgBytesPerSec = rate * wfx->Format.nBlockAlign;
		wfx->Format.cbSize = 22;
		wfx->Samples.wValidBitsPerSample = (WORD)bits;
		wfx->dwChannelMask = (chan == 1) ? SPEAKER_FRONT_CENTER
			: (chan == 2) ? (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT) : 0;
		wfx->SubFormat = SUBTYPE_FLAC;
		memcpy((BYTE*)wfx + sizeof(WAVEFORMATEXTENSIBLE), cfg, cfgSize);
		mts.Add(mt);
		return true;
	}

	if (fourcc == MAKEFOURCC('a','c','-','3') || fourcc == MAKEFOURCC('e','c','-','3')) {
		WORD tag = (fourcc == MAKEFOURCC('a','c','-','3')) ? 0x2000 /*AC3*/ : 0xAAC3 /*EAC3*/;
		CMediaType mt;
		mt.SetSampleSize(1);
		mt.majortype = MEDIATYPE_Audio;
		mt.subtype = FOURCCMap(tag);
		mt.formattype = FORMAT_WaveFormatEx;
		WAVEFORMATEX* wfe = (WAVEFORMATEX*)mt.AllocFormatBuffer(sizeof(WAVEFORMATEX));
		memset(wfe, 0, mt.FormatLength());
		wfe->wFormatTag = tag;
		wfe->nChannels = 2;
		wfe->nSamplesPerSec = 48000;
		wfe->wBitsPerSample = 16;
		wfe->nBlockAlign = 1;
		mts.Add(mt);
		return true;
	}

	return false;
}

#ifndef NOVIDEOTWEAK
bool CFLVSplitterFilter::ReadTag(VideoTweak& vt)
{
	if(!m_pFile->GetRemaining()) 
		return false;

	vt.x = (BYTE)m_pFile->BitRead(4);
	vt.y = (BYTE)m_pFile->BitRead(4);

	return true;
}
#endif

bool CFLVSplitterFilter::Sync(__int64& pos)
{
	m_pFile->Seek(pos);

	while(m_pFile->GetRemaining() >= 15)
	{
		__int64 limit = m_pFile->GetRemaining();
		while (true) {
			//DebugText(L"Sync Test\n");
			BYTE b = (BYTE)m_pFile->BitRead(8);
			if (b == 8 || b == 9) break;
			if (--limit < 15) return false;
		}

		pos = m_pFile->GetPos() - 5;
		m_pFile->Seek(pos);

		Tag ct;
		if (ReadTag(ct)) {
			__int64 next = m_pFile->GetPos() + ct.DataSize;
			if(next == m_pFile->GetLength() - 4) {
				m_pFile->Seek(pos);
				return true;
			}
			else if (next <= m_pFile->GetLength() - 19) {
				m_pFile->Seek(next);
				Tag nt;
				if (ReadTag(nt) && (nt.TagType == 8 || nt.TagType == 9 || nt.TagType == 18)) {
					if ((nt.PreviousTagSize == ct.DataSize + 11) ||
						(m_IgnorePrevSizes &&
						 nt.TimeStamp >= ct.TimeStamp &&
						 nt.TimeStamp - ct.TimeStamp <= 1000))
					{
						m_pFile->Seek(pos);
						return true;
					}
				}
			}
		}

		m_pFile->Seek(pos + 5);
	}

	return false;
}

HRESULT CFLVSplitterFilter::CreateOutputs(IAsyncReader* pAsyncReader)
{
	//DebugText(L"Enter CreateOutputs\n");
	CheckPointer(pAsyncReader, E_POINTER);

	HRESULT hr = E_FAIL;

	m_pFile.Free();
	// 2017/05/04
	// ストリーミングの有効化のため変更
	//m_pFile.Attach(DNew CBaseSplitterFileEx(pAsyncReader, hr, DEFAULT_CACHE_LENGTH, false));
	m_pFile.Attach(DNew CBaseSplitterFileEx(pAsyncReader, hr, DEFAULT_CACHE_LENGTH, false, true));
	if(!m_pFile) return E_OUTOFMEMORY;
	if(FAILED(hr)) {m_pFile.Free(); return hr;}

	m_rtNewStart = m_rtCurrent = 0;
	m_rtNewStop = m_rtStop = m_rtDuration = 0;

	currentPos_ = LONGLONG_MIN;
	positionDiff_ = LONGLONG_MIN;

	if(m_pFile->BitRead(24) != 'FLV' || m_pFile->BitRead(8) != 1)
		return E_FAIL;

	EXECUTE_ASSERT(m_pFile->BitRead(5) == 0); // TypeFlagsReserved
	bool fTypeFlagsAudio = !!m_pFile->BitRead(1);
	EXECUTE_ASSERT(m_pFile->BitRead(1) == 0); // TypeFlagsReserved
	bool fTypeFlagsVideo = !!m_pFile->BitRead(1);
	m_DataOffset = (UINT32)m_pFile->BitRead(32);

	// doh, these flags aren't always telling the truth
	fTypeFlagsAudio = fTypeFlagsVideo = true;

	Tag t = { 0 };
	AudioTag at = { 0 };
	VideoTag vt = { 0 };

	UINT32 prevTagSize = 0;
	m_IgnorePrevSizes = false;

	m_pFile->Seek(m_DataOffset);

	//auto log = [&](const Tag& t, const std::wstring& str) {
	//	addLog(L">%s", str.c_str());
	//	addLog(L">PreviousTagSize:%u", t.PreviousTagSize);
	//	addLog(L">TagType:%u", t.TagType);
	//	addLog(L">DataSize:%u", t.DataSize);
	//	addLog(L">TimeStamp:%u", t.TimeStamp);
	//	addLog(L">StreamID:%u", t.StreamID);
	//};

	// for(int i = 0; ReadTag(t) && (fTypeFlagsVideo || fTypeFlagsAudio) && i < 100; i++)
	for(int i = 0; i < 100; i++)
	{
		if (!fTypeFlagsVideo && !fTypeFlagsAudio) { break; }

		if (m_pFile->IsStreaming())
		{
			if (!WaitForData(15)) { break; }
			if (!ReadTag(t))
			{
				if (!WaitForData(t.DataSize)) { break; }
			}
		}
		else
		{
			if (!ReadTag(t)) { break; }
		}

		//log(t, L"CFLVSplitterFilter::CreateOutputs");

		//DebugText(L"CreateOutputs:%d\n", i);

		UINT64 next = m_pFile->GetPos() + t.DataSize;

		CStringW name;

		CMediaType mt;
		mt.SetSampleSize(1);
		mt.subtype = GUID_NULL;
		CAtlArray<CMediaType> mts; //[pcr-ertmp] prefilled types for enhanced tags

		if (i != 0 && t.PreviousTagSize != prevTagSize) {
			m_IgnorePrevSizes = true;
		}
		prevTagSize = t.DataSize + 11;

		if (t.TagType == 18 && t.DataSize != 0)
		{
			flv::FLVSCRIPT script;
			script.resize(t.DataSize, 0);

			m_pFile->ByteRead(script.data(), script.size());

			metadata_.clear();
			metadata_.readMetaData(script);
		}
		else if(t.TagType == 8 && t.DataSize != 0 && fTypeFlagsAudio)
		{
			UNREFERENCED_PARAMETER(at);
			AudioTag at;
			name = L"Audio";

			if(ReadTag(at))
			{
				if (at.IsEx) //[pcr-ertmp] enhanced audio: pin comes from the sequence-start tag only
				{
					name = L"Audio";
					if (at.PacketType == 0 && IsEnhancedAudioFourCC(at.FourCC))
					{
						__int64 cfgPos = m_pFile->GetPos();
						UINT32 cfgSize = (UINT32)(next - cfgPos);
						if (cfgSize > 0 && cfgSize <= t.DataSize)
						{
							std::vector<BYTE> cfg(cfgSize);
							m_pFile->ByteRead(cfg.data(), cfgSize);
							if (BuildEnhancedAudioTypes(at.FourCC, cfg.data(), cfgSize, mts))
							{
								fTypeFlagsAudio = false;
								addLog(L"ertmp: audio ex fourcc=0x%08X pkt=%u pin", at.FourCC, at.PacketType);
							}
							else
							{
								addLog(L"ertmp: unsupported audio ex fourcc=0x%08X", at.FourCC);
								fTypeFlagsAudio = false; // don't wait for a legacy config that never comes
							}
						}
					}
					else if (at.PacketType >= 4)
					{
						addLog(L"ertmp: audio ex pkt=%u not supported", at.PacketType);
						fTypeFlagsAudio = false;
					}
					// other pkt values: keep scanning for the config tag
				}
				else
				{
				int dataSize = t.DataSize - 1;

				fTypeFlagsAudio = false;

				mt.majortype = MEDIATYPE_Audio;
				mt.formattype = FORMAT_WaveFormatEx;
				WAVEFORMATEX* wfe = (WAVEFORMATEX*)mt.AllocFormatBuffer(sizeof(WAVEFORMATEX));
				memset(wfe, 0, sizeof(WAVEFORMATEX));
				wfe->nSamplesPerSec = 44100*(1<<at.SoundRate)/8;
				wfe->wBitsPerSample = 8*(at.SoundSize+1);
				wfe->nChannels = 1*(at.SoundType+1);
				
				switch(at.SoundFormat)
				{
				case 0: // FLV_CODECID_PCM_BE
					mt.subtype = FOURCCMap(wfe->wFormatTag = WAVE_FORMAT_PCM);
					break;
				case 1: // FLV_CODECID_ADPCM
					mt.subtype = FOURCCMap(MAKEFOURCC('A','S','W','F'));
					break;
				case 2:	// FLV_CODECID_MP3
					mt.subtype = FOURCCMap(wfe->wFormatTag = WAVE_FORMAT_MP3);

					{
						CBaseSplitterFileEx::mpahdr h;
						CMediaType mt2;
						if(m_pFile->Read(h, 4, false, &mt2))
							mt = mt2;
					}
					break;
				case 3 :	// FLV_CODECID_PCM_LE
					// ToDo
					break;
				case 4 :	// unknown
					break;
				case 5 :	// FLV_CODECID_NELLYMOSER_8HZ_MONO
					mt.subtype = FOURCCMap(MAKEFOURCC('N','E','L','L'));
					wfe->nSamplesPerSec = 8000;
					break;
				case 6 :	// FLV_CODECID_NELLYMOSER
					mt.subtype = FOURCCMap(MAKEFOURCC('N','E','L','L'));
					break;
				case 10: { // FLV_CODECID_AAC
					if (dataSize < 1 || m_pFile->BitRead(8) != 0) { // packet type 0 == aac header
						fTypeFlagsAudio = true;
						break;
					}

					const int sampleRates[] = {
						96000, 88200, 64000, 48000, 44100, 32000, 24000,
						22050, 16000, 12000, 11025, 8000, 7350
					};
					const int channels[] = {
						0, 1, 2, 3, 4, 5, 6, 8
					};

					__int64 configOffset = m_pFile->GetPos();
					UINT32 configSize = dataSize - 1;
					if (configSize < 2) break;

					// Might break depending on the AAC profile, see ff_mpeg4audio_get_config in ffmpeg's mpeg4audio.c
					m_pFile->BitRead(5);
					int iSampleRate = (int)m_pFile->BitRead(4);
					int iChannels = (int)m_pFile->BitRead(4);
					if (iSampleRate > 12 || iChannels > 7) break;

					wfe = (WAVEFORMATEX*)mt.AllocFormatBuffer(sizeof(WAVEFORMATEX) + configSize);
					memset(wfe, 0, mt.FormatLength());
					wfe->nSamplesPerSec = sampleRates[iSampleRate];
					wfe->wBitsPerSample = 16;
					wfe->nChannels = channels[iChannels];
					wfe->cbSize = configSize;

					m_pFile->Seek(configOffset);
					m_pFile->ByteRead((BYTE*)(wfe+1), configSize);

					mt.subtype = FOURCCMap(wfe->wFormatTag = WAVE_FORMAT_AAC);
				}
				
				}
				}
			}
		}
		else if(t.TagType == 9 && t.DataSize != 0 && fTypeFlagsVideo)
		{
			UNREFERENCED_PARAMETER(vt);
			VideoTag vt;
			if(ReadTag(vt))
			{
				if (vt.IsEx) //[pcr-ertmp] enhanced video: pin comes from the sequence-start tag only
				{
					name = L"Video";
					if (vt.PacketType == 0 && IsEnhancedVideoFourCC(vt.FourCC))
					{
						UINT32 ew = 0, eh = 0;
						if (BuildEnhancedVideoTypes(vt.FourCC, mt, mts, ew, eh))
						{
							fTypeFlagsVideo = false;
							addLog(L"ertmp: video ex fourcc=0x%08X %ux%u pin", vt.FourCC, ew, eh);
						}
						else
						{
							addLog(L"ertmp: unsupported video ex fourcc=0x%08X", vt.FourCC);
							fTypeFlagsVideo = false; // don't wait for a legacy config that never comes
						}
					}
					else if (vt.PacketType >= 5)
					{
						// MPEG2TS(5)/Multitrack(6)/ModEx(7): not supported by this splitter
						addLog(L"ertmp: video ex pkt=%u not supported", vt.PacketType);
						fTypeFlagsVideo = false;
					}
					// pkt 1-4 (coded/seqend/metadata without a config yet, live mid-stream): keep scanning
				}
				else if (vt.FrameType == 1)
				{
				int dataSize = t.DataSize - 1;

				fTypeFlagsVideo = false;
				name = L"Video";

				mt.majortype = MEDIATYPE_Video;
				mt.formattype = FORMAT_VideoInfo;
				VIDEOINFOHEADER* vih = (VIDEOINFOHEADER*)mt.AllocFormatBuffer(sizeof(VIDEOINFOHEADER));
				memset(vih, 0, sizeof(VIDEOINFOHEADER));

				BITMAPINFOHEADER* bih = &vih->bmiHeader;

				int w, h, arx, ary;

				switch(vt.CodecID)
				{
				case 2:   // H.263
					//DebugText(L"H.263\n");
					if(m_pFile->BitRead(17) != 1) break;

					m_pFile->BitRead(13); // Version (5), TemporalReference (8)

					switch(BYTE PictureSize = (BYTE)m_pFile->BitRead(3)) // w00t
					{
					case 0: case 1:
						vih->bmiHeader.biWidth = (WORD)m_pFile->BitRead(8*(PictureSize+1));
						vih->bmiHeader.biHeight = (WORD)m_pFile->BitRead(8*(PictureSize+1));
						break;
					case 2: case 3: case 4: 
						vih->bmiHeader.biWidth = 704 / PictureSize;
						vih->bmiHeader.biHeight = 576 / PictureSize;
						break;
					case 5: case 6: 
						PictureSize -= 3;
						vih->bmiHeader.biWidth = 640 / PictureSize;
						vih->bmiHeader.biHeight = 480 / PictureSize;
						break;
					}

					if(!vih->bmiHeader.biWidth || !vih->bmiHeader.biHeight) break;

					mt.subtype = FOURCCMap(vih->bmiHeader.biCompression = '1VLF');

					break;

				case 5:  // VP6 with alpha
					//DebugText(L"VP6 with alpha\n");
					m_pFile->BitRead(24);
				case 4: { // VP6
					//DebugText(L"VP6\n");
					#ifdef NOVIDEOTWEAK
					m_pFile->BitRead(8);
					#else					
					VideoTweak fudge;
					ReadTag(fudge);
					#endif
				
					if (m_pFile->BitRead(1)) {
						// Delta (inter) frame
						fTypeFlagsVideo = true;
						break;
					}
					m_pFile->BitRead(6);
					bool fSeparatedCoeff = !!m_pFile->BitRead(1);
					m_pFile->BitRead(5);
					int filterHeader = (int)m_pFile->BitRead(2);
					m_pFile->BitRead(1);
					if (fSeparatedCoeff || !filterHeader) {
						m_pFile->BitRead(16);
					}

					h = (int)m_pFile->BitRead(8) * 16;
					w = (int)m_pFile->BitRead(8) * 16;

					ary = (int)m_pFile->BitRead(8) * 16;
					arx = (int)m_pFile->BitRead(8) * 16;

					if(arx && arx != w || ary && ary != h) {
						VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)mt.AllocFormatBuffer(sizeof(VIDEOINFOHEADER2));
						memset(vih2, 0, sizeof(VIDEOINFOHEADER2));
						vih2->dwPictAspectRatioX = arx;
						vih2->dwPictAspectRatioY = ary;
						bih = &vih2->bmiHeader;						
						mt.formattype = FORMAT_VideoInfo2;
						vih = (VIDEOINFOHEADER *)vih2;
					}

					bih->biWidth = w;
					bih->biHeight = h;
					#ifndef NOVIDEOTWEAK
					SetRect(&vih->rcSource, 0, 0, w - fudge.x, h - fudge.y);
					SetRect(&vih->rcTarget, 0, 0, w - fudge.x, h - fudge.y);
					#endif

					mt.subtype = FOURCCMap(bih->biCompression = '4VLF');

					break;
				}
				case 7: { // H.264
					//DebugText(L"H.264\n");
					if (dataSize < 4 || m_pFile->BitRead(8) != 0) { // packet type 0 == avc header
						fTypeFlagsVideo = true;
						break;
					}
					m_pFile->BitRead(24); // composition time

					__int64 headerOffset = m_pFile->GetPos();
					UINT32 headerSize = dataSize - 4;
					BYTE *headerData = DNew BYTE[headerSize];

					m_pFile->ByteRead(headerData, headerSize);

					m_pFile->Seek(headerOffset + 9);

					mt.formattype = FORMAT_MPEG2Video;
					MPEG2VIDEOINFO* vih = (MPEG2VIDEOINFO*)mt.AllocFormatBuffer(FIELD_OFFSET(MPEG2VIDEOINFO, dwSequenceHeader) + headerSize);
					memset(vih, 0, mt.FormatLength());
					vih->hdr.bmiHeader.biSize = sizeof(vih->hdr.bmiHeader);
					vih->hdr.bmiHeader.biPlanes = 1;
					vih->hdr.bmiHeader.biBitCount = 24;
					vih->dwFlags = (headerData[4] & 0x03) + 1; // nal length size

					

					vih->dwProfile = (BYTE)m_pFile->BitRead(8); // profile_idc
					//DebugText(L"profile_idc:%d\n", vih->dwProfile);

#ifdef _DEBUG
					// MOD
					//m_pFile->BitRead(1); // constrained_set0_flag
					//BYTE constrained_set1_flag = (BYTE)m_pFile->BitRead(1); // constrained_set1_flag
					//m_pFile->BitRead(1); // constrained_set2_flag
					//m_pFile->BitRead(1); // constrained_set3_flag
					//m_pFile->BitRead(1); // constrained_set4_flag
					//m_pFile->BitRead(3); // reserved_zero_3bits

					//DebugText(L"constrained_set1_flag:%d\n", constrained_set1_flag);

					// ORIGINAL
					m_pFile->BitRead(8);
#else
					// ORIGINAL
					m_pFile->BitRead(8);
#endif

					vih->dwLevel = (BYTE)m_pFile->BitRead(8);
					m_pFile->UExpGolombRead(); // seq_parameter_set_id
					if(vih->dwProfile >= 100) { // high profile
						UINT64 chroma_format_idc = m_pFile->UExpGolombRead();
						if(chroma_format_idc == 3) // chroma_format_idc
							m_pFile->BitRead(1); // residue_transform_flag
						m_pFile->UExpGolombRead(); // bit_depth_luma_minus8
						m_pFile->UExpGolombRead(); // bit_depth_chroma_minus8
						m_pFile->BitRead(1); // qpprime_y_zero_transform_bypass_flag
						if(m_pFile->BitRead(1)) // seq_scaling_matrix_present_flag
							for(int i = 0; i < 8; i++)
								if(m_pFile->BitRead(1)) // seq_scaling_list_present_flag
									for(int j = 0, size = i < 6 ? 16 : 64, next = 8; j < size && next != 0; ++j)
										next = (next + m_pFile->SExpGolombRead() + 256) & 255;
					}
					m_pFile->UExpGolombRead(); // log2_max_frame_num_minus4
					UINT64 pic_order_cnt_type = m_pFile->UExpGolombRead();
					if(pic_order_cnt_type == 0) {
						m_pFile->UExpGolombRead(); // log2_max_pic_order_cnt_lsb_minus4
					}
					else if(pic_order_cnt_type == 1) {
						m_pFile->BitRead(1); // delta_pic_order_always_zero_flag
						m_pFile->SExpGolombRead(); // offset_for_non_ref_pic
						m_pFile->SExpGolombRead(); // offset_for_top_to_bottom_field
						UINT64 num_ref_frames_in_pic_order_cnt_cycle = m_pFile->UExpGolombRead();
						for(int i = 0; i < num_ref_frames_in_pic_order_cnt_cycle; i++)
							m_pFile->SExpGolombRead(); // offset_for_ref_frame[i]
					}
					m_pFile->UExpGolombRead(); // num_ref_frames
					m_pFile->BitRead(1); // gaps_in_frame_num_value_allowed_flag
					UINT64 pic_width_in_mbs_minus1 = m_pFile->UExpGolombRead();
					UINT64 pic_height_in_map_units_minus1 = m_pFile->UExpGolombRead();
					BYTE frame_mbs_only_flag = (BYTE)m_pFile->BitRead(1);
					//////////////////////////////////////////////////////////
					if (!frame_mbs_only_flag) {
						m_pFile->BitRead(1); // mb_adaptive_frame_field_flag
					}
					m_pFile->BitRead(1); // direct_8x8_inference_flag
					BYTE frame_cropping_flag = (BYTE)m_pFile->BitRead(1);

					UINT64 frame_crop_left_offset = 0;
					UINT64 frame_crop_right_offset = 0;
					UINT64 frame_crop_top_offset = 0;
					UINT64 frame_crop_bottom_offset = 0;
					if (frame_cropping_flag) {
						frame_crop_left_offset = m_pFile->UExpGolombRead();
						frame_crop_right_offset = m_pFile->UExpGolombRead();
						frame_crop_top_offset = m_pFile->UExpGolombRead();
						frame_crop_bottom_offset = m_pFile->UExpGolombRead();

						//DebugText(L"frame_cropping_flag:%d\n", frame_cropping_flag);
						//DebugText(L"frame_crop_left_offset:%d\n", frame_crop_left_offset);
						//DebugText(L"frame_crop_right_offset:%d\n", frame_crop_right_offset);
						//DebugText(L"frame_crop_top_offset:%d\n", frame_crop_top_offset);
						//DebugText(L"frame_crop_bottom_offset:%d\n", frame_crop_bottom_offset);
					}
					
					//////////////////////////////////////////////////////////
					vih->hdr.bmiHeader.biWidth = vih->hdr.dwPictAspectRatioX = (LONG)(((pic_width_in_mbs_minus1 + 1) * 16) - (frame_crop_left_offset * 2) - (frame_crop_right_offset * 2));
					vih->hdr.bmiHeader.biHeight = vih->hdr.dwPictAspectRatioY = (LONG)(((2 - frame_mbs_only_flag) * (pic_height_in_map_units_minus1 + 1) * 16) - (frame_crop_top_offset * 2) - (frame_crop_bottom_offset * 2));

					
					//DebugText(L"frame_mbs_only_flag:%d pic_height_in_map_units_minus1:%llu\n", frame_mbs_only_flag, pic_height_in_map_units_minus1);
					//DebugText(L"biWidth:%d biHeight:%d\n", vih->hdr.bmiHeader.biWidth, vih->hdr.bmiHeader.biHeight);

					BYTE* src = (BYTE*)headerData + 5;
					BYTE* dst = (BYTE*)vih->dwSequenceHeader;
					BYTE* src_end = (BYTE*)headerData + headerSize;
					BYTE* dst_end = (BYTE*)vih->dwSequenceHeader + headerSize;
					int spsCount = *(src++) & 0x1F;
					int ppsCount = -1;

					vih->cbSequenceHeader = 0;

					while (src < src_end - 1) {
						if (spsCount == 0 && ppsCount == -1) {
							ppsCount = *(src++);
							continue;
						}

						if (spsCount > 0) spsCount--;
						else if (ppsCount > 0) ppsCount--;
						else break;

						int len = ((src[0] << 8) | src[1]) + 2;
						if(src + len > src_end || dst + len > dst_end) {ASSERT(0); break;}
						memcpy(dst, src, len);
						src += len; 
						dst += len;
						vih->cbSequenceHeader += len;
					}

					delete[] headerData;

					mt.subtype = FOURCCMap(vih->hdr.bmiHeader.biCompression = '1CVA');

					break;
				}
				default:
					fTypeFlagsVideo = true;
				}
				}
			}
		}
			
		if(mt.subtype != GUID_NULL)
		{
			mts.Add(mt);
		}
		if(!mts.IsEmpty()) //[pcr-ertmp] also fires for enhanced prefilled types
		{
			CAutoPtr<CBaseSplitterOutputPin> pPinOut(DNew CBaseSplitterOutputPin(mts, name, this, this, &hr));
			EXECUTE_ASSERT(SUCCEEDED(AddOutputPin(t.TagType, pPinOut)));
		}

		m_pFile->Seek(next);
	}

	if(m_pFile->IsRandomAccess())
	{
		__int64 pos = max(m_DataOffset, m_pFile->GetLength() - 256 * 1024);

		if(Sync(pos))
		{
			Tag t = { 0 };
			AudioTag at = { 0 };
			VideoTag vt = { 0 };

			while(ReadTag(t))
			{
				UINT64 next = m_pFile->GetPos() + t.DataSize;

				if(t.TagType == 8 && ReadTag(at) || t.TagType == 9 && ReadTag(vt))
				{
					m_rtDuration = max(m_rtDuration, 10000i64 * t.TimeStamp); 
				}

				m_pFile->Seek(next);
			}
		}
	}

	m_rtNewStop = m_rtStop = m_rtDuration;

	//DebugText(L"Exit CreateOutputs\n");
	return m_pOutputs.GetCount() > 0 ? S_OK : E_FAIL;
}

bool CFLVSplitterFilter::DemuxInit()
{
	SetThreadName((DWORD)-1, "CFLVSplitterFilter");
	return true;
}

void CFLVSplitterFilter::DemuxSeek(REFERENCE_TIME rt)
{
	if(!m_rtDuration || rt <= 0) {
		m_pFile->Seek(m_DataOffset);
	}
	else if (!m_IgnorePrevSizes) {
		NormalSeek(rt);
	}
	else {
		AlternateSeek(rt);
	}
}

void CFLVSplitterFilter::NormalSeek(REFERENCE_TIME rt)
{
	//DebugText(L"NormalSeek Enter:%lld\n", rt);
	bool fAudio = !!GetOutputPin(8);
	bool fVideo = !!GetOutputPin(9);
	
	__int64 pos = metadata_.getKeyframe(rt);
	//DebugText(L"getKeyframe:%lld\n", pos);

	if (pos < 0)
	{
		pos = (LONGLONG)(m_DataOffset + 1.0 * rt / m_rtDuration * (m_pFile->GetLength() - m_DataOffset));
	}

	if(!Sync(pos))
	{
		ASSERT(0);
		m_pFile->Seek(m_DataOffset);
		return;
	}

	Tag t = { 0 };
	AudioTag at = { 0 };
	VideoTag vt = { 0 };

	while(ReadTag(t))
	{
		if(10000i64 * t.TimeStamp >= rt)
		{
			m_pFile->Seek(m_pFile->GetPos() - 15);
			break;
		}

		m_pFile->Seek(m_pFile->GetPos() + t.DataSize);
	}

	while(m_pFile->GetPos() >= m_DataOffset && (fAudio || fVideo) && ReadTag(t))
	{
		UINT64 prev = m_pFile->GetPos() - 15 - t.PreviousTagSize - 4;

		if(10000i64 * t.TimeStamp <= rt)
		{
			if(t.TagType == 8 && ReadTag(at))
			{
				fAudio = false;
			}
			else if(t.TagType == 9 && ReadTag(vt) && vt.FrameType == 1)
			{
				fVideo = false;
			}
		}

		m_pFile->Seek(prev);
	}

	if(fAudio || fVideo)
	{
		ASSERT(0);
		m_pFile->Seek(m_DataOffset);
	}
	//DebugText(L"NormalSeek Exit\n");
}

void CFLVSplitterFilter::AlternateSeek(REFERENCE_TIME rt)
{
	//DebugText(L"AlternateSeek Enter\n");
	bool hasAudio = !!GetOutputPin(8);
	bool hasVideo = !!GetOutputPin(9);

	__int64 estimPos = (__int64)(m_DataOffset + 1.0 * rt / m_rtDuration * (m_pFile->GetLength() - m_DataOffset));
	__int64 seekBack = 256 * 1024;

	while (true) {
		bool foundAudio = false;
		bool foundVideo = false;
		__int64 bestPos = 0;

		estimPos = max(estimPos - seekBack, m_DataOffset);
		seekBack *= 2;

		if (Sync(estimPos)) {
			Tag t = { 0 };
			AudioTag at = { 0 };
			VideoTag vt = { 0 };

			while (ReadTag(t) && t.TimeStamp * 10000i64 < rt) {
				__int64 cur = m_pFile->GetPos() - 15;
				__int64 next = cur + 15 + t.DataSize;

				if (hasAudio && t.TagType == 8 && ReadTag(at)) {
					foundAudio = true;
					if (!hasVideo) bestPos = cur;
				}
				else if (hasVideo && t.TagType == 9 && ReadTag(vt) && vt.FrameType == 1) {
					foundVideo = true;
					bestPos = cur;
				}

				m_pFile->Seek(next);
			}
		}

		if ((hasAudio && !foundAudio) || (hasVideo && !foundVideo)) {
			if (estimPos == m_DataOffset) {
				m_pFile->Seek(m_DataOffset);
				return;
			}
		}
		else {
			m_pFile->Seek(bestPos);
			return;
		}
	}
	//DebugText(L"AlternateSeek Exit\n");
}

bool CFLVSplitterFilter::DemuxLoop()
{
	//addLog(L"CFLVSplitterFilter::DemuxLoop (Enter)");

	HRESULT hr = S_OK;

	CAutoPtr<Packet> p;
	Tag t = { 0 };
	AudioTag at = { 0 };
	VideoTag vt = { 0 };
	UINT32 previousTagSize = 0;

	class Stream {
	public:
		struct Queue {
			struct Value {
				Value() : total(0), count(0) {}
				REFERENCE_TIME total;
				int count;
			};

			Value audio;
			Value video;
		};
		//------------------------------------------------------------------
	private:
		class Timestamp {
			UINT32 recent_;
			LONGLONG diff_;

		public:
			Timestamp() : recent_(0), diff_(0) {}
			LONGLONG check(UINT32 timestamp)
			{
				if (recent_ == 0) { recent_ = timestamp; }
				diff_ = (LONGLONG)timestamp - (LONGLONG)recent_;
				return diff_;
			}
			void set(UINT32 timestamp) { recent_ = timestamp; }
			LONGLONG diff() { return diff_; }
		};
		//------------------------------------------------------------------
		class Keyframe {
			bool enable_;
			int count_;
			DWORD time_;

		public:
			Keyframe() : enable_(false), count_(0), time_(0) {}

			void reset(bool enable)
			{
				enable_ = enable;
				count_ = 0;
				time_ = 0;
			}

			bool valid() { return enable_; }

			bool timeout(DWORD now)
			{
				if (time_ == 0) { time_ = now; return false; }
				if ((now - time_) > 10000) { enable_ = false; return true; }
				return false;
			}

			bool operator()(BYTE type, BYTE frame)
			{
				if (type == 9)
				{
					++count_;
				}

				if (type == 9 && frame == 1)
				{
					enable_ = false;
					return true;
				}
				return false;
			}

			int count() { return count_; }
		};
		//------------------------------------------------------------------
		class Timer {
			DWORD time_;
		public:
			Timer() : time_(0) {}

			bool operator()(DWORD now)
			{
				if (time_ == 0)
				{
					time_ = now;
					return true;
				}

				if (now - time_ > 10000)
				{
					time_ = now;
					return true;
				}
				return false;
			}
		};
		//------------------------------------------------------------------
		class Cache {
			DWORD origin_;
			REFERENCE_TIME current_;
			REFERENCE_TIME buffer_;
		public:
			Cache() : origin_(0), current_(0), buffer_(0) {}

			void operator()(DWORD now, REFERENCE_TIME buffer)
			{
				if (origin_ == 0)
				{
					origin_ = now;
				}

				current_ = ((REFERENCE_TIME)now - (REFERENCE_TIME)origin_) * 10000i64;
				buffer_ = buffer;
			}

			REFERENCE_TIME buffer() { return buffer_; }
			REFERENCE_TIME current() { return current_; }
			REFERENCE_TIME diff() { return (buffer_ - current_); }
		};
		//------------------------------------------------------------------
		class Util {
			struct Pin {
				CComPtr<CBaseSplitterOutputPin> audio;
				CComPtr<CBaseSplitterOutputPin> video;
			} pin_;

			struct Renderer {
				CComPtr<IBaseFilter> audio;
				CComPtr<IBaseFilter> video;
			} renderer_;

			struct Timer {
				Timer() : delay(0), status(0) {}
				DWORD delay;
				DWORD status;
			} timer_;

			bool running_;
			DWORD time_;
		public:
			Util() : running_(false) {}
			void init(const CComPtr<CBaseSplitterOutputPin>& audio, const CComPtr<CBaseSplitterOutputPin>& video)
			{
				pin_.audio = audio;
				pin_.video = video;
				renderer_.audio = GetRendererFilterFromOutputPin(pin_.audio);
				renderer_.video = GetRendererFilterFromOutputPin(pin_.video);
			}

			void get(Queue& queue)
			{
				if (pin_.audio)
				{
					pin_.audio->QueueTotalTime(queue.audio.total);
					queue.audio.count = pin_.audio->QueueCount();
				}

				if (pin_.video)
				{
					pin_.video->QueueTotalTime(queue.video.total);
					queue.video.count = pin_.video->QueueCount();
				}
			}

			bool running()
			{
				if (running_) { return true; }
				bool runA = !renderer_.audio || (renderer_.audio && IsFilterRunning(renderer_.audio));
				bool runV = !renderer_.video || (renderer_.video && IsFilterRunning(renderer_.video));
				running_ = runA && runV;
				return running_;
			}

			void reset(DWORD now)
			{
				timer_.delay = now;
			}

			bool delay(DWORD now, int interval)
			{
				if (!running_) { return false; }

				if (timer_.delay == 0) { timer_.delay = now; }
				if ((now - timer_.delay) > (DWORD)interval)
				{
					timer_.delay = now;
					return true;
				}
				return false;
			}

			bool shortage()
			{
				int audio = 0, video = 0;
				if (pin_.audio)
				{
					audio = pin_.audio->QueueCount();
				}

				if (pin_.video)
				{
					video = pin_.video->QueueCount();
				}

				if (audio <= 0 && video <= 0)
				{
					return true;
				}
				return false;
			}

			bool status(DWORD now)
			{
				if (timer_.status == 0 || (now - timer_.status) > 1000)
				{
					timer_.status = now;
					return true;
				}
				return false;
			}
		};
		//------------------------------------------------------------------
	public:
		Stream()
			: origin(0)
			, buffer(0)
			, optimize(false)
			, delay(false)
			, limit(0)
			, interval(0)
		{}

		REFERENCE_TIME origin;

		bool loop;
		bool keyframe;
		REFERENCE_TIME buffer;
		bool optimize;
		bool delay;
		REFERENCE_TIME limit;
		int interval;

		Timestamp audio;
		Timestamp video;
		Keyframe key;
		Timer timer;
		Cache cache;
		Util util;
	} stream;

	//======================================================================
	if (m_pFile->IsStreaming())
	{
		stream.loop = cfg_.loop;
		stream.keyframe = cfg_.keyframe;
		stream.buffer = cfg_.buffer * 10000i64;
		stream.optimize = cfg_.optimize;
		stream.delay = cfg_.delay;
		stream.limit = cfg_.limit * 10000i64;
		stream.interval = cfg_.interval;

		stream.key.reset(cfg_.keyframe);
		stream.util.init(GetOutputPin(8), GetOutputPin(9));
	}
	//======================================================================

	auto display = [&]() {
		Stream::Queue queue;
		stream.util.get(queue);

		CString str;
		str.Format(L"B:%lldms A:%lldms (%d) V:%lldms (%d)",
			stream.cache.diff() / 10000i64,
			queue.audio.total / 10000i64, queue.audio.count,
			queue.video.total / 10000i64, queue.video.count);

		detailLog(
			std::wstring(str),
			L"Buffer:%lldms\r\nAudio:%lldms (Queue:%d)\r\nVideo:%lldms (Queue:%d)",
			stream.cache.diff() / 10000i64,
			queue.audio.total / 10000i64, queue.audio.count,
			queue.video.total / 10000i64, queue.video.count);
	};

	auto loop = [&]()->bool{
		bool result = false, request = false;
		if (SUCCEEDED(hr))
		{
			result = true;
			if (!CheckRequest(NULL))
			{
				request = true;
				if (WaitForData(15))
				{
					return true;
				}
			}
		}

		if (m_pFile->IsStreaming())
		{
			std::wstring type;
			if (!result)
			{
				type = L"Result";
			}
			else if (!request)
			{
				type = L"Request";
			}
			else
			{
				type = L"Data";
			}
			addLog(L"DemuxLoop (%s)", type.c_str());
			NotifyEvent(EC_USERABORT, E_ABORT, USERABORT_DEFAULT);
		}
		return false;
	};

	bool sync = true; // 同期有効
	bool error = false;

	// while (SUCCEEDED(hr) && !CheckRequest(NULL) && m_pFile->GetRemaining())
	// while (SUCCEEDED(hr) && !CheckRequest(NULL) && WaitForData(15))
	while (loop())
	{
		if (!ReadTag(t, previousTagSize, sync, error)) { break; }
		
		__int64 next = m_pFile->GetPos() + t.DataSize;

		if((t.DataSize > 0) && (t.TagType == 8 && ReadTag(at) || t.TagType == 9 && ReadTag(vt)))
		{
			UINT32 tsOffset = 0;
			if(t.TagType == 9 && vt.IsEx) //[pcr-ertmp] enhanced video: header fully consumed by ReadTag
			{
				// only CodedFrames(1)/CodedFramesX(3) carry media samples
				if(vt.PacketType != 1 && vt.PacketType != 3) goto NextTag;
				tsOffset = vt.TsOffset;
			}
			else if(t.TagType == 9)
			{
				if(vt.FrameType == 5) goto NextTag; // video info/command frame
				if(vt.CodecID == 4)
				{
					m_pFile->BitRead(8);
				}
				else if(vt.CodecID == 5)
				{
					m_pFile->BitRead(32);
				}
				else if(vt.CodecID == 7) {
					if (m_pFile->BitRead(8) != 1) goto NextTag;

					// Tag timestamps specify decode time, this is the display time offset
					tsOffset = (UINT32)m_pFile->BitRead(24);
					tsOffset = (tsOffset + 0xff800000) ^ 0xff800000; // sign extension
				}
			}
			if(t.TagType == 8 && at.IsEx) { //[pcr-ertmp] enhanced audio: only CodedFrames(1) carries samples
				if (at.PacketType != 1) goto NextTag;
			} else if(t.TagType == 8 && at.SoundFormat == 10) {
				if (m_pFile->BitRead(8) != 1) goto NextTag;
			}
			__int64 dataSize = next - m_pFile->GetPos();
			if (dataSize <= 0) goto NextTag;

			if (p != NULL) { p.Free(); }
			p.Attach(DNew Packet());
			p->TrackNumber = t.TagType;

			// サンプルタイム設定
			////////////////////////////////////////////////////////
			//p->rtStart = 10000i64 * (t.TimeStamp + tsOffset); 
			//p->rtStop = p->rtStart + 1;
			////////////////////////////////////////////////////////
			if (m_pFile->IsStreaming())
			{// ストリーミング時
				DWORD now = timeGetTime();

				// ループ検出
				if (t.TimeStamp != 0 && t.TagType == 8)
				{
					LONGLONG diff = stream.audio.check(t.TimeStamp);
					if (diff < -10000)
					{
						if (!error && stream.loop)
						{
							LogTag(t, (boost::wformat(L"ループ (Audio:%lldms)") % diff).str(), diff);
							NotifyEvent(EC_USERABORT, E_ABORT, USERABORT_TIMESTAMP_AUDIO);
							break;
						}
					}
					else if (diff >= 10000)
					{
						LogTag(t, (boost::wformat(L"スキップ (Audio:%lldms)") % diff).str(), diff);
						NotifyEvent(EC_USERABORT, E_ABORT, USERABORT_DATA_ERROR);
						break;
					}
					stream.audio.set(t.TimeStamp);
				}
				else if (t.TimeStamp != 0 && t.TagType == 9)
				{
					LONGLONG diff = stream.video.check(t.TimeStamp);
					if (diff < -10000)
					{
						if (!error && stream.loop)
						{
							LogTag(t, (boost::wformat(L"ループ (Video:%lldms)") % diff).str(), diff);
							NotifyEvent(EC_USERABORT, E_ABORT, USERABORT_TIMESTAMP_VIDEO);
							break;
						}
					}
					else if (diff >= 10000)
					{
						LogTag(t, (boost::wformat(L"スキップ (Video:%lldms)") % diff).str(), diff);
						NotifyEvent(EC_USERABORT, E_ABORT, USERABORT_DATA_ERROR);
						break;
					}
					stream.video.set(t.TimeStamp);
				}

				if (stream.key.valid())
				{
					stream.util.reset(now); // 遅延補正タイマーリセット

					if (stream.key.timeout(now))
					{
						addLog(L"キーフレーム (Timeout)");
					}
					else if (stream.key(t.TagType, vt.FrameType))
					{
						addLog(L"キーフレーム (Count:%d)", stream.key.count());
					}
					else
					{
						m_pFile->Seek(next);
						continue;
					}
				}

				if (stream.util.running())
				{
					if (stream.optimize)
					{
						stream.optimize = false;
						currentPos_ = 10000i64 * (t.TimeStamp + tsOffset);
						stream.origin = currentPos_;

						display();
						addLog(L"バッファ最適化 (Buffer:%lldms)", stream.buffer / 10000i64);

						DeliverBeginFlush();
						DeliverEndFlush();

						stream.util.reset(now); // 遅延補正タイマーリセット

						if (stream.keyframe)
						{
							stream.key.reset(stream.keyframe);
							m_pFile->Seek(next);
							continue;
						}
					}
					else if (stream.delay && stream.util.delay(now, stream.interval))
					{
						REFERENCE_TIME delay = stream.cache.diff();
						if (delay > stream.limit)
						{
							REFERENCE_TIME fix = delay - stream.buffer;
							stream.origin += fix;

							display();
							addLog(L"遅延補正 (Delay:%lldms Fix:%lldms)", delay / 10000i64, fix / 10000i64);

							DeliverBeginFlush();
							DeliverEndFlush();

							stream.util.reset(now); // 遅延補正タイマーリセット

							if (stream.keyframe)
							{
								stream.key.reset(stream.keyframe);
								m_pFile->Seek(next);
								continue;
							}
						}
					}
				}

				currentPos_ = 10000i64 * (t.TimeStamp + tsOffset);
				if (stream.origin == 0) { stream.origin = currentPos_; }

				positionDiff_ = stream.origin - stream.buffer;
				p->rtStart = currentPos_ - positionDiff_;
				p->rtStop = p->rtStart + 1;

				stream.cache(now, p->rtStart);

				if (stream.util.status(now))
				{
					Stream::Queue queue;
					stream.util.get(queue);
					utl::StatusLog status;

					status.valid = true;
					status.buffer = stream.cache.diff();
					status.audio.total = queue.audio.total;
					status.audio.count = queue.audio.count;
					status.video.total = queue.video.total;
					status.video.count = queue.video.count;

					setStatus(status);
				}

				if (stream.timer(now)) { display(); }

				if ((t.TimeStamp != 0) && (stream.cache.diff() < 0))
				{
					if (stream.util.shortage())
					{
						addLog(L"バッファ不足 (Buffer:%lldms)",
							stream.cache.diff() / 10000i64);

						stream.origin += stream.cache.diff();

						positionDiff_ = stream.origin - stream.buffer;
						p->rtStart = currentPos_ - positionDiff_;
						p->rtStop = p->rtStart + 1;

						stream.util.reset(now); // 遅延補正タイマーリセット
					}
				}
			}
			else
			{
				p->rtStart = 10000i64 * (t.TimeStamp + tsOffset);
				p->rtStop = p->rtStart + 1;
			}

			p->bSyncPoint = t.TagType == 9 ? vt.FrameType == 1 : true;
			p->SetCount((size_t)dataSize);
			m_pFile->ByteRead(p->GetData(), p->GetCount());
			hr = DeliverPacket(p);
		}

NextTag:
		m_pFile->Seek(next);
	}


	if (m_pFile->IsStreaming())
	{
		setStatus(utl::StatusLog());
	}

	//detailLog(L"CFLVSplitterFilter::DemuxLoop (Exit)",
	//	L"DeliverPacket:0x%08X\r\nCheckRequest:%d\r\nGetRemaining:%lld",
	//	hr, CheckRequest(NULL), m_pFile->GetRemaining());
	return true;
}






STDMETHODIMP CFLVSplitterFilter::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
	CheckPointer(ppv, E_POINTER);

	*ppv = NULL;

	if (riid == IID_IFLVSplitterProp)
	{
		//DebugText(L"riid == IID_IFLVSplitterProp\n");
		*ppv = static_cast<IFLVSplitterProp*>(this);
		AddRef();
		return S_OK;
	}
	
	return __super::NonDelegatingQueryInterface(riid, ppv);
}


HRESULT STDMETHODCALLTYPE CFLVSplitterFilter::GetEncoder(std::string* str)
{
	if (str == NULL) { return E_POINTER; }
	*str = metadata_.getEncoder();
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CFLVSplitterFilter::GetDatarate(int* rate)
{
	if (rate == NULL) { return E_POINTER; }
	*rate = metadata_.getDatarate();
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CFLVSplitterFilter::GetFramerate(int* rate)
{
	if (rate == NULL) { return E_POINTER; }
	*rate = metadata_.getFramerate();
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CFLVSplitterFilter::GetCurrentPos(LONGLONG* pos)
{
	if (pos == NULL) { return E_POINTER; }
	if (currentPos_ == LONGLONG_MIN) { return E_FAIL; }

	*pos = currentPos_;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CFLVSplitterFilter::GetPositionDiff(LONGLONG* diff)
{
	if (diff == NULL) { return E_POINTER; }
	if (positionDiff_ == LONGLONG_MIN) { return E_FAIL; }

	*diff = positionDiff_;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CFLVSplitterFilter::SetConfig(const FLVConfig& cfg)
{
	cfg_ = cfg;
	return S_OK;
}







//
// CFLVSourceFilter
//

CFLVSourceFilter::CFLVSourceFilter(LPUNKNOWN pUnk, HRESULT* phr, utl::SyncLog& log)
	: CFLVSplitterFilter(pUnk, phr, log)
{
	m_clsid = __uuidof(this);
	m_pInput.Free();
}

