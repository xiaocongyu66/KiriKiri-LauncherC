// -*- coding: utf-8 -*-
//---------------------------------------------------------------------------
// AlphaMovie プラグイン (吉里吉里Z 版・再実装)
//
//   .amv (Alpha Movie) コンテナを再生し、アルファ付きの各フレームを Layer の
//   メイン画像バッファへ直接描画するプラグイン。
//
//   Copyright T.Imoto <http://www.kaede-software.com>
//   ライセンスは吉里吉里(krkr) 本体に準拠します。
//
//   本ファイルは、オリジナル版 AlphaMovie.dll がソース非公開のため、
//   その (x64) バイナリのデコンパイル解析により .amv 形式とデコード処理を
//   割り出し、同一仕様となるよう再実装したものです。
//
//   ■ デコード方式について
//   当初は「フレーム毎に標準 JPEG を合成し turbojpeg で展開」する方針だったが、
//   デコンパイル精査の結果、本コーデックの DC 予測が**非標準**であることが判明:
//     ・DC 予測子は Huffman テーブル単位で保持され、フレーム毎に 2 個だけ
//       (luma 予測子 = Y と A が共有 / chroma 予測子 = Cb と Cr が共有) リセット。
//     ・標準 JPEG(=libjpeg/turbojpeg) は成分毎に独立した予測子を用いるため、
//       この bitstream をそのまま食わせると DC がずれて破綻する。
//   そのため、標準ベースライン Huffman + IDCT の**自前デコーダ**を実装し、
//   上記 2 共有予測子方式を忠実に再現する (Huffman/量子化は標準テーブル)。
//   4:2:0 サブサンプリング / MCU=16x16。
//
//   Ported into KiriKiri-LauncherC from https://github.com/wamsoft/AlphaMovie
//   (2026-07-04). Layer dirty-rect update added for GPU/SDL present paths.
//---------------------------------------------------------------------------

#define NCB_MODULE_NAME TJS_W("AlphaMovie.dll")

#include "ncbind.hpp"
#include "StorageIntf.h"
#include "MsgIntf.h"

#include <zlib.h>

#include <cmath>
#include <cstring>
#include <vector>

//---------------------------------------------------------------------------
// リトルエンディアン読み出しヘルパ
//---------------------------------------------------------------------------
static inline tjs_uint16 rdU16(const tjs_uint8 *p) { return (tjs_uint16)(p[0] | (p[1] << 8)); }
static inline tjs_int16  rdS16(const tjs_uint8 *p) { return (tjs_int16)rdU16(p); }
static inline tjs_uint32 rdU32(const tjs_uint8 *p) {
	return (tjs_uint32)(p[0] | (p[1] << 8) | (p[2] << 16) | ((tjs_uint32)p[3] << 24));
}

static const tjs_uint32 AMV_MAGIC  = 0x4d504a41; // 'AJPM'
static const tjs_uint32 FRAM_MAGIC = 0x4d415246; // 'FRAM'

static inline tjs_uint8 clip8(int v) {
	return (tjs_uint8)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

//---------------------------------------------------------------------------
// ジグザグ順 → 自然順 (8x8) 変換表
//---------------------------------------------------------------------------
static const int ZIGZAG[64] = {
	 0, 1, 8,16, 9, 2, 3,10,
	17,24,32,25,18,11, 4, 5,
	12,19,26,33,40,48,41,34,
	27,20,13, 6, 7,14,21,28,
	35,42,49,56,57,50,43,36,
	29,22,15,23,30,37,44,51,
	58,59,52,45,38,31,39,46,
	53,60,61,54,47,55,62,63
};

//---------------------------------------------------------------------------
// 標準ベースライン Huffman テーブル (JPEG Annex K / libjpeg 標準)
//   amv フレームには Huffman テーブルが格納されていないため、標準テーブルを使う。
//---------------------------------------------------------------------------
static const tjs_uint8 std_dc_luminance_bits[16] =
	{ 0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0 };
static const tjs_uint8 std_dc_luminance_val[12] =
	{ 0,1,2,3,4,5,6,7,8,9,10,11 };

static const tjs_uint8 std_dc_chrominance_bits[16] =
	{ 0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0 };
static const tjs_uint8 std_dc_chrominance_val[12] =
	{ 0,1,2,3,4,5,6,7,8,9,10,11 };

static const tjs_uint8 std_ac_luminance_bits[16] =
	{ 0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,0x7d };
static const tjs_uint8 std_ac_luminance_val[162] = {
	0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,
	0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,
	0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,
	0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,
	0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,
	0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,0x84,0x85,0x86,0x87,0x88,0x89,
	0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,
	0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,
	0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,
	0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,
	0xf9,0xfa
};

static const tjs_uint8 std_ac_chrominance_bits[16] =
	{ 0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,0x77 };
static const tjs_uint8 std_ac_chrominance_val[162] = {
	0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,
	0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,
	0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,
	0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,
	0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,
	0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,
	0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,
	0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,
	0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,
	0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,
	0xf9,0xfa
};

//===========================================================================
// 自作ベースライン JPEG デコーダ (2 共有 DC 予測子方式)
//===========================================================================
namespace amvdec {

// Huffman 復号テーブル (canonical, mincode/maxcode/valptr 方式)
struct HuffTable {
	int   mincode[17];
	int   maxcode[18];  // maxcode[l] = -1 なら長さ l のコード無し
	int   valptr[17];
	tjs_uint8 huffval[256];

	void build(const tjs_uint8 *bits16, const tjs_uint8 *vals) {
		int huffsize[257];
		int huffcode[257];
		int k = 0;
		for (int l = 1; l <= 16; l++)
			for (int i = 0; i < bits16[l - 1]; i++) huffsize[k++] = l;
		huffsize[k] = 0;
		int nsym = k;

		int code = 0, si = huffsize[0];
		k = 0;
		while (huffsize[k]) {
			while (huffsize[k] == si) { huffcode[k] = code; code++; k++; }
			code <<= 1; si++;
		}
		int p = 0;
		for (int l = 1; l <= 16; l++) {
			if (bits16[l - 1]) {
				valptr[l]  = p;
				mincode[l] = huffcode[p];
				p += bits16[l - 1];
				maxcode[l] = huffcode[p - 1];
			} else {
				maxcode[l] = -1;
			}
		}
		maxcode[17] = 0x7fffffff;
		for (int i = 0; i < nsym; i++) huffval[i] = vals[i];
	}
};

// エントロピー用ビットリーダ (JPEG 由来: 0xFF00 バイトスタッフを解除)
struct BitReader {
	const tjs_uint8 *p, *end;
	tjs_uint32 acc;   // 現在のバイトのビット
	int cnt;          // 残りビット数
	BitReader(const tjs_uint8 *d, size_t n) : p(d), end(d + n), acc(0), cnt(0) {}

	inline int bit() {
		if (cnt == 0) {
			if (p >= end) return 0;         // データ枯渇時は 0 で埋める
			tjs_uint8 b = *p++;
			if (b == 0xFF) {
				// 0xFF00 は literal 0xFF、0xFFxx(marker) は終端扱い
				if (p < end && *p == 0x00) p++;
				else { /* marker: これ以上読まない */ }
			}
			acc = b; cnt = 8;
		}
		cnt--;
		return (acc >> cnt) & 1;
	}
	inline int bits(int n) { int v = 0; while (n-- > 0) v = (v << 1) | bit(); return v; }

	inline int decode(const HuffTable &h) {
		int l = 1, code = bit();
		while (code > h.maxcode[l]) {
			code = (code << 1) | bit();
			l++;
			if (l > 16) return 0;
		}
		return h.huffval[h.valptr[l] + code - h.mincode[l]];
	}
};

static inline int receive_extend(BitReader &br, int s) {
	if (s == 0) return 0;
	int v = br.bits(s);
	if (v < (1 << (s - 1))) v += (-1 << s) + 1;
	return v;
}

// 分離型 IDCT 用コサイン係数表  cosT[x][u] = 0.5 * c(u) * cos((2x+1)uπ/16)
static double g_cosT[8][8];
static bool   g_cosInit = false;
static void initCos() {
	if (g_cosInit) return;
	const double PI = 3.14159265358979323846;
	for (int x = 0; x < 8; x++)
		for (int u = 0; u < 8; u++) {
			double cu = (u == 0) ? (1.0 / 1.4142135623730951) : 1.0;
			g_cosT[x][u] = 0.5 * cu * cos((2 * x + 1) * u * PI / 16.0);
		}
	g_cosInit = true;
}

// 自然順の逆量子化済み係数 coef[64] → 空間 8x8 (レベルシフト +128 / クランプ)
static void idct8x8(const int *coef, tjs_uint8 *out /*[64] raster*/) {
	double tmp[64];
	// 行方向 (u について)
	for (int v = 0; v < 8; v++) {
		const int *row = coef + v * 8;
		for (int x = 0; x < 8; x++) {
			double s = 0.0;
			for (int u = 0; u < 8; u++) s += g_cosT[x][u] * row[u];
			tmp[v * 8 + x] = s;
		}
	}
	// 列方向 (v について)
	for (int x = 0; x < 8; x++) {
		for (int y = 0; y < 8; y++) {
			double s = 0.0;
			for (int v = 0; v < 8; v++) s += g_cosT[y][v] * tmp[v * 8 + x];
			out[y * 8 + x] = clip8((int)std::floor(s + 128.0 + 0.5));
		}
	}
}

// 1 成分の記述
struct Comp {
	int h, v;                  // サンプリング (MCU 内ブロック数 = h*v)
	const tjs_uint8 *quant;    // 量子化テーブル (ジグザグ順, 64)
	const HuffTable *dc, *ac;  // Huffman テーブル
	int  predIdx;              // DC 予測子インデックス (0=luma,1=chroma)
	int  pw, ph;               // 成分プレーンの実サイズ
	std::vector<tjs_uint8> plane;
};

// 1 ブロック復号 → 自然順の逆量子化係数
static void decodeBlock(BitReader &br, const Comp &c, int &pred, int *coef) {
	memset(coef, 0, 64 * sizeof(int));
	int t = br.decode(*c.dc);
	int diff = receive_extend(br, t);
	pred += diff;
	coef[0] = pred * c.quant[0];        // DC (zigzag0 = natural0)
	int k = 1;
	while (k < 64) {
		int rs = br.decode(*c.ac);
		int r = rs >> 4, s = rs & 15;
		if (s == 0) {
			if (r == 15) { k += 16; continue; }  // ZRL
			break;                               // EOB
		}
		k += r;
		if (k > 63) break;
		int val = receive_extend(br, s);
		coef[ZIGZAG[k]] = val * c.quant[k];      // 逆量子化 (ともにジグザグ順)
		k++;
	}
}

// インターリーブ・スキャンをまるごと復号し、各成分プレーンを埋める。
//   fw,fh : フレーム画素サイズ (16 の倍数)。comps は SOS 順。
//   予測子は predIdx 単位で共有、フレーム先頭で 0 リセット (非標準仕様の再現)。
static void decodeScan(const tjs_uint8 *entropy, size_t elen,
					   int fw, int fh, std::vector<Comp> &comps) {
	initCos();
	int hmax = 1, vmax = 1;
	for (size_t i = 0; i < comps.size(); i++) {
		if (comps[i].h > hmax) hmax = comps[i].h;
		if (comps[i].v > vmax) vmax = comps[i].v;
	}
	int mcuW = fw / (hmax * 8);
	int mcuH = fh / (vmax * 8);
	for (size_t i = 0; i < comps.size(); i++) {
		Comp &c = comps[i];
		c.pw = mcuW * c.h * 8;
		c.ph = mcuH * c.v * 8;
		c.plane.assign((size_t)c.pw * c.ph, 0);
	}

	BitReader br(entropy, elen);
	int pred[2] = { 0, 0 };
	int coef[64];
	tjs_uint8 blk[64];

	for (int my = 0; my < mcuH; my++) {
		for (int mx = 0; mx < mcuW; mx++) {
			for (size_t ci = 0; ci < comps.size(); ci++) {
				Comp &c = comps[ci];
				for (int by = 0; by < c.v; by++) {
					for (int bx = 0; bx < c.h; bx++) {
						decodeBlock(br, c, pred[c.predIdx], coef);
						idct8x8(coef, blk);
						int px = (mx * c.h + bx) * 8;
						int py = (my * c.v + by) * 8;
						for (int r = 0; r < 8; r++)
							memcpy(&c.plane[(size_t)(py + r) * c.pw + px], &blk[r * 8], 8);
					}
				}
			}
		}
	}
}

} // namespace amvdec

//---------------------------------------------------------------------------
// レイヤの書き込みバッファ取得 (PropGet 方式・全バリアント対応)
//   参考: src/plugins/layerExSave/utils.cpp
//---------------------------------------------------------------------------
static bool getLayerWriteBuffer(iTJSDispatch2 *lay, tjs_int &w, tjs_int &h,
								tjs_uint8 *&ptr, tjs_int &pitch)
{
	if (!lay || TJS_FAILED(lay->IsInstanceOf(0, 0, 0, TJS_W("Layer"), lay))) return false;

	tTJSVariant val;
	if (TJS_FAILED(lay->PropGet(0, TJS_W("hasImage"), 0, &val, lay)) || val.AsInteger() == 0)
		return false;

	val.Clear();
	if (TJS_FAILED(lay->PropGet(0, TJS_W("imageWidth"), 0, &val, lay))) return false;
	w = (tjs_int)val.AsInteger();
	val.Clear();
	if (TJS_FAILED(lay->PropGet(0, TJS_W("imageHeight"), 0, &val, lay))) return false;
	h = (tjs_int)val.AsInteger();
	val.Clear();
	if (TJS_FAILED(lay->PropGet(0, TJS_W("mainImageBufferPitch"), 0, &val, lay))) return false;
	pitch = (tjs_int)val.AsInteger();
	val.Clear();
	if (TJS_FAILED(lay->PropGet(0, TJS_W("mainImageBufferForWrite"), 0, &val, lay))) return false;
	ptr = reinterpret_cast<tjs_uint8*>(val.AsInteger());

	return (ptr != 0 && w > 0 && h > 0 && pitch != 0);
}

//---------------------------------------------------------------------------
// AlphaMovie クラス本体
//---------------------------------------------------------------------------
class AlphaMovie {
public:
	AlphaMovie()
		: Stream(0), NumOfFrame(0), FirstFrameOfs(0),
		  Width(0), Height(0), FpsScale(0), FpsRate(0),
		  IsZlibAlpha(false), QuantSize(0),
		  CurrentIndex(0), Left(0), Top(0), Loop(false),
		  NextLoop(false), PreloadSamples(5), Playing(false)
	{
		memset(Quant, 0, sizeof(Quant));
		HDCLuma.build(std_dc_luminance_bits,   std_dc_luminance_val);
		HDCChroma.build(std_dc_chrominance_bits, std_dc_chrominance_val);
		HACLuma.build(std_ac_luminance_bits,   std_ac_luminance_val);
		HACChroma.build(std_ac_chrominance_bits, std_ac_chrominance_val);
	}
	virtual ~AlphaMovie() { closeStream(); }

	//----------------------------------------------------------- ストレージ操作
	void closeStream() {
		if (Stream) { delete Stream; Stream = nullptr; }
	}

	// amv を開く
	void open(ttstr filename) {
		closeStream();
		Frames.clear();
		CurrentIndex = 0;
		Playing = false;

		Stream = TVPCreateStream(filename, TJS_BS_READ);
		if (!Stream) TVPThrowExceptionMessage(TJS_W("AlphaMovie: cannot open storage."));
		try {
			parseHeader();
			scanFrames();
		} catch (...) {
			closeStream();
			throw;
		}
	}

	//----------------------------------------------------------- ヘッダ解析
	void parseHeader() {
		tjs_uint8 hdr[0x28];
		seekRead(0, hdr, 0x28);

		if (rdU32(hdr + 0x00) != AMV_MAGIC)
			TVPThrowExceptionMessage(TJS_W("This file is not Alpha Movie File."));
		if (rdU32(hdr + 0x08) != 0)
			TVPThrowExceptionMessage(TJS_W("Invalid File revision number."));

		tjs_uint32 headerSize = rdU32(hdr + 0x0c);
		if (headerSize < 0x28) TVPThrowExceptionMessage(TJS_W("Invalid header size."));
		QuantSize = headerSize - 0x28;
		if (QuantSize != 0x80 && QuantSize != 0xc0)
			TVPThrowExceptionMessage(TJS_W("Invalid Quantaization table size."));
		FirstFrameOfs = headerSize;

		NumOfFrame = rdU32(hdr + 0x14);
		if (NumOfFrame == 0) TVPThrowExceptionMessage(TJS_W("Not found frame in this file."));

		FpsScale = rdU32(hdr + 0x18);
		FpsRate  = rdU32(hdr + 0x1c);
		if (FpsScale == 0 || FpsRate == 0) TVPThrowExceptionMessage(TJS_W("Invalid frame rate."));

		Width  = rdU16(hdr + 0x20);
		Height = rdU16(hdr + 0x22);
		if (Width == 0 || Height == 0) TVPThrowExceptionMessage(TJS_W("Screen size is zero ?"));

		tjs_uint32 flags = rdU32(hdr + 0x24);
		if (flags & 1) {
			IsZlibAlpha = false;          // alpha は JPEG (3 テーブル)
		} else if (flags & 2) {
			IsZlibAlpha = true;           // alpha は zlib (2 テーブル)
		} else {
			TVPThrowExceptionMessage(TJS_W("Invalid Attribute."));
		}
		if (( IsZlibAlpha && QuantSize != 0x80) ||
			(!IsZlibAlpha && QuantSize != 0xc0))
			TVPThrowExceptionMessage(TJS_W("Invalid Quantaization table size."));

		// 量子化テーブル (各 64 バイト, ジグザグ順で格納)
		tjs_uint8 qbuf[0xc0];
		seekRead(0x28, qbuf, QuantSize);
		memcpy(Quant[0], qbuf + 0,   64);   // table0 : Y luma
		memcpy(Quant[1], qbuf + 64,  64);   // table1 : Cb/Cr chroma
		if (QuantSize == 0xc0)
			memcpy(Quant[2], qbuf + 128, 64); // table2 : alpha luma
		else
			memset(Quant[2], 0, 64);
	}

	//----------------------------------------------------------- フレーム走査
	void scanFrames() {
		Frames.clear();
		tjs_int64 ofs = FirstFrameOfs;
		for (tjs_uint32 i = 0; i < NumOfFrame; i++) {
			tjs_uint8 fh[12];
			seekRead(ofs, fh, 12);
			if (rdU32(fh + 0) != FRAM_MAGIC)
				TVPThrowExceptionMessage(TJS_W("File format error."));
			tjs_uint32 size = rdU32(fh + 4);
			FrameInfo fi;
			fi.offset = ofs;
			fi.number = rdU32(fh + 8);
			Frames.push_back(fi);
			ofs += (tjs_int64)size + 8;   // 次フレーム位置
		}
	}

	//----------------------------------------------------------- 1 フレーム取得
	void decodeFrame(tjs_int index,
					 std::vector<tjs_uint8> &bgra,
					 tjs_int &fleft, tjs_int &ftop, tjs_int &fw, tjs_int &fh)
	{
		if (index < 0 || index >= (tjs_int)Frames.size())
			TVPThrowExceptionMessage(TJS_W("AlphaMovie: frame index out of range."));

		tjs_int64 ofs = Frames[index].offset;
		if (!IsZlibAlpha)
			decodeJpegAlpha(ofs, bgra, fleft, ftop, fw, fh);
		else
			decodeZlibAlpha(ofs, bgra, fleft, ftop, fw, fh);
	}

	//----------------------------------------------------------- showNextImage
	tjs_int showNextImage(iTJSDispatch2 *layer) {
		if (Frames.empty()) TVPThrowExceptionMessage(TJS_W("AlphaMovie: no movie opened."));

		if (CurrentIndex >= (tjs_int)Frames.size()) {
			if (NextMovieFile.length() > 0) {
				ttstr next = NextMovieFile;
				NextMovieFile = TJS_W("");
				bool nl = NextLoop;
				open(next);
				Loop = nl;
				CurrentIndex = 0;
			} else if (Loop) {
				CurrentIndex = 0;
			} else {
				CurrentIndex = (tjs_int)Frames.size() - 1;   // 末尾に据え置き
			}
		}

		std::vector<tjs_uint8> bgra;
		tjs_int fleft, ftop, fw, fh;
		decodeFrame(CurrentIndex, bgra, fleft, ftop, fw, fh);

		blitToLayer(layer, bgra, fleft, ftop, fw, fh);

		tjs_int ret = (tjs_int)Frames[CurrentIndex].number;
		CurrentIndex++;
		return ret;
	}

	//----------------------------------------------------------- 再生制御
	void play()  { CurrentIndex = 0; Playing = true; }
	void stop()  { Playing = false; }
	bool isPlaying() const { return Playing; }
	void clear() { /* 同期デコードのため保持キューは無し。no-op */ }

	void setPosition(tjs_int x, tjs_int y) { Left = x; Top = y; }
	void setNextMovieFile(ttstr filename) { NextMovieFile = filename; }

	//----------------------------------------------------------- プロパティ
	tjs_int getNumOfFrame() const { return (tjs_int)NumOfFrame; }
	tjs_int getFrame() const { return CurrentIndex; }
	void    setFrame(tjs_int v) {
		if (Frames.empty()) { CurrentIndex = 0; return; }
		if (v < 0) v = 0;
		if (v >= (tjs_int)Frames.size()) v = (tjs_int)Frames.size() - 1;
		CurrentIndex = v;
	}
	bool getLoop() const { return Loop; }
	void setLoop(bool v) { Loop = v; }
	bool getNextLoop() const { return NextLoop; }
	void setNextLoop(bool v) { NextLoop = v; }
	tjs_int getPreloadSamples() const { return PreloadSamples; }
	void    setPreloadSamples(tjs_int v) { PreloadSamples = v; }
	tjs_int getLeft() const { return Left; }
	void    setLeft(tjs_int v) { Left = v; }
	tjs_int getTop() const { return Top; }
	void    setTop(tjs_int v) { Top = v; }
	tjs_int getScreenWidth()  const { return (tjs_int)Width; }
	tjs_int getScreenHeight() const { return (tjs_int)Height; }
	tjs_int getFPSScale() const { return (tjs_int)FpsScale; }
	tjs_int getFPSRate()  const { return (tjs_int)FpsRate; }

private:
	//----------------------------------------------------------- フレーム情報
	struct FrameInfo { tjs_int64 offset; tjs_uint32 number; };

	tTJSBinaryStream *Stream;
	tjs_uint32 NumOfFrame;
	tjs_int64  FirstFrameOfs;
	tjs_uint16 Width, Height;
	tjs_uint32 FpsScale, FpsRate;
	bool       IsZlibAlpha;
	tjs_uint32 QuantSize;
	tjs_uint8  Quant[3][64];
	std::vector<FrameInfo> Frames;

	amvdec::HuffTable HDCLuma, HDCChroma, HACLuma, HACChroma;

	tjs_int  CurrentIndex;
	tjs_int  Left, Top;
	bool     Loop, NextLoop;
	tjs_int  PreloadSamples;
	bool     Playing;
	ttstr    NextMovieFile;

	//----------------------------------------------------------- ストリーム I/O
	void seekRead(tjs_int64 ofs, tjs_uint8 *buf, tjs_uint len) {
		if (!Stream) TVPThrowExceptionMessage(TJS_W("AlphaMovie: stream not opened."));
		Stream->Seek(ofs, TJS_BS_SEEK_SET);
		tjs_uint got = Stream->Read(buf, len);
		if (got != len) TVPThrowExceptionMessage(TJS_W("AlphaMovie: read error."));
	}

	//----------------------------------------------------------- (A) JPEG-alpha
	// 4 成分 (Cb,Cr,Y,A) を 1 スキャンにインターリーブしたエントロピーを、
	// 2 共有 DC 予測子方式の自作デコーダで展開する。
	//   MCU ブロック順: Cb, Cr, Y0-3, A0-3 (計 10)
	//   Cb: h1v1 quant1 chroma / Cr: h1v1 quant1 chroma
	//   Y : h2v2 quant0 luma   / A : h2v2 quant2 luma
	//   DC 予測子: luma(Y,A 共有) と chroma(Cb,Cr 共有) の 2 個。
	void decodeJpegAlpha(tjs_int64 ofs, std::vector<tjs_uint8> &bgra,
						 tjs_int &fleft, tjs_int &ftop, tjs_int &fw, tjs_int &fh)
	{
		tjs_uint8 fh20[20];
		seekRead(ofs, fh20, 20);
		if (rdU32(fh20 + 0) != FRAM_MAGIC)
			TVPThrowExceptionMessage(TJS_W("File format error."));
		tjs_uint32 size = rdU32(fh20 + 4);
		fleft = rdS16(fh20 + 12);
		ftop  = rdS16(fh20 + 14);
		fw    = rdU16(fh20 + 16);
		fh    = rdU16(fh20 + 18);
		if ((fw & 0x0f) || (fh & 0x0f) || fw <= 0 || fh <= 0)
			TVPThrowExceptionMessage(TJS_W("File format error."));

		tjs_uint payloadLen = size - 12;
		std::vector<tjs_uint8> entropy(payloadLen ? payloadLen : 1);
		if (payloadLen) {
			tjs_uint got = Stream->Read(&entropy[0], payloadLen);
			if (got != payloadLen) TVPThrowExceptionMessage(TJS_W("AlphaMovie: read error."));
		}

		// 成分定義 (SOS 順 = Cb,Cr,Y,A)
		std::vector<amvdec::Comp> comps(4);
		comps[0].h = 1; comps[0].v = 1; comps[0].quant = Quant[1];
		comps[0].dc = &HDCChroma; comps[0].ac = &HACChroma; comps[0].predIdx = 1; // Cb
		comps[1].h = 1; comps[1].v = 1; comps[1].quant = Quant[1];
		comps[1].dc = &HDCChroma; comps[1].ac = &HACChroma; comps[1].predIdx = 1; // Cr
		comps[2].h = 2; comps[2].v = 2; comps[2].quant = Quant[0];
		comps[2].dc = &HDCLuma;   comps[2].ac = &HACLuma;   comps[2].predIdx = 0; // Y
		comps[3].h = 2; comps[3].v = 2; comps[3].quant = Quant[2];
		comps[3].dc = &HDCLuma;   comps[3].ac = &HACLuma;   comps[3].predIdx = 0; // A

		amvdec::decodeScan(&entropy[0], payloadLen, fw, fh, comps);

		// 合成: Y,Cb,Cr,A → BGRA (Cb,Cr は 1/2 解像度なので x/2,y/2 参照)
		bgra.assign((size_t)fw * fh * 4, 0);
		const amvdec::Comp &Cb = comps[0], &Cr = comps[1], &Y = comps[2], &A = comps[3];
		for (tjs_int y = 0; y < fh; y++) {
			tjs_uint8 *dst = &bgra[(size_t)y * fw * 4];
			const tjs_uint8 *yr = &Y.plane[(size_t)y * Y.pw];
			const tjs_uint8 *ar = &A.plane[(size_t)y * A.pw];
			const tjs_uint8 *cbr = &Cb.plane[(size_t)(y / 2) * Cb.pw];
			const tjs_uint8 *crr = &Cr.plane[(size_t)(y / 2) * Cr.pw];
			for (tjs_int x = 0; x < fw; x++, dst += 4) {
				int yy = yr[x];
				int cb = cbr[x / 2];
				int cr = crr[x / 2];
				int a  = ar[x];
				int r = yy + ((91881  * (cr - 128)) >> 16);
				int g = yy - ((22554  * (cb - 128) + 46802 * (cr - 128)) >> 16);
				int b = yy + ((116130 * (cb - 128)) >> 16);
				dst[0] = clip8(b);
				dst[1] = clip8(g);
				dst[2] = clip8(r);
				dst[3] = (tjs_uint8)a;
			}
		}
	}

	//----------------------------------------------------------- (B) zlib-alpha
	//   FRAM 24 バイトヘッダ: FRAM/size/frameNum/left/top/w/h/**alphaZlibSize**。
	//   データ順は【alpha(zlib) が先 → color(JPEG) が後】(デコンパイル FUN_1800151a0)。
	//     alpha zlib データ  : alphaZlibSize バイト → inflate で w*h の 8bit グレースケール
	//     color エントロピー : (size - alphaZlibSize - 16) バイト、3 成分 (Cb,Cr,Y) 4:2:0
	void decodeZlibAlpha(tjs_int64 ofs, std::vector<tjs_uint8> &bgra,
						 tjs_int &fleft, tjs_int &ftop, tjs_int &fw, tjs_int &fh)
	{
		tjs_uint8 fh24[24];
		seekRead(ofs, fh24, 24);
		if (rdU32(fh24 + 0) != FRAM_MAGIC)
			TVPThrowExceptionMessage(TJS_W("File format error."));
		tjs_uint32 size          = rdU32(fh24 + 4);
		fleft = rdS16(fh24 + 12);
		ftop  = rdS16(fh24 + 14);
		fw    = rdU16(fh24 + 16);
		fh    = rdU16(fh24 + 18);
		tjs_uint32 alphaZlibLen  = rdU32(fh24 + 20);
		if ((fw & 0x0f) || (fh & 0x0f) || fw <= 0 || fh <= 0)
			TVPThrowExceptionMessage(TJS_W("File format error."));

		// alpha(zlib) が先
		std::vector<tjs_uint8> alphaZ(alphaZlibLen ? alphaZlibLen : 1);
		if (alphaZlibLen) {
			tjs_uint got = Stream->Read(&alphaZ[0], alphaZlibLen);
			if (got != alphaZlibLen) TVPThrowExceptionMessage(TJS_W("AlphaMovie: read error."));
		}
		// color(JPEG) が後 = size - alphaZlibSize - 16
		tjs_int64 colorLen64 = (tjs_int64)size - (tjs_int64)alphaZlibLen - 16;
		tjs_uint colorSize = (colorLen64 > 0) ? (tjs_uint)colorLen64 : 0;
		std::vector<tjs_uint8> color(colorSize ? colorSize : 1);
		if (colorSize) {
			tjs_uint got = Stream->Read(&color[0], colorSize);
			if (got != colorSize) TVPThrowExceptionMessage(TJS_W("AlphaMovie: read error."));
		}

		// 色 (Cb,Cr,Y 3 成分)
		std::vector<amvdec::Comp> comps(3);
		comps[0].h = 1; comps[0].v = 1; comps[0].quant = Quant[1];
		comps[0].dc = &HDCChroma; comps[0].ac = &HACChroma; comps[0].predIdx = 1; // Cb
		comps[1].h = 1; comps[1].v = 1; comps[1].quant = Quant[1];
		comps[1].dc = &HDCChroma; comps[1].ac = &HACChroma; comps[1].predIdx = 1; // Cr
		comps[2].h = 2; comps[2].v = 2; comps[2].quant = Quant[0];
		comps[2].dc = &HDCLuma;   comps[2].ac = &HACLuma;   comps[2].predIdx = 0; // Y
		amvdec::decodeScan(&color[0], colorSize, fw, fh, comps);

		// alpha zlib 展開
		std::vector<tjs_uint8> alpha((size_t)fw * fh, 255);
		if (alphaZlibLen) {
			uLongf destLen = (uLongf)alpha.size();
			int zr = uncompress(&alpha[0], &destLen, &alphaZ[0], (uLong)alphaZlibLen);
			if (zr != Z_OK || destLen != alpha.size()) {
				for (size_t i = 0; i < alpha.size(); i++) alpha[i] = 255;
			}
		}

		bgra.assign((size_t)fw * fh * 4, 0);
		const amvdec::Comp &Cb = comps[0], &Cr = comps[1], &Y = comps[2];
		for (tjs_int y = 0; y < fh; y++) {
			tjs_uint8 *dst = &bgra[(size_t)y * fw * 4];
			const tjs_uint8 *yr = &Y.plane[(size_t)y * Y.pw];
			const tjs_uint8 *cbr = &Cb.plane[(size_t)(y / 2) * Cb.pw];
			const tjs_uint8 *crr = &Cr.plane[(size_t)(y / 2) * Cr.pw];
			const tjs_uint8 *ar = &alpha[(size_t)y * fw];
			for (tjs_int x = 0; x < fw; x++, dst += 4) {
				int yy = yr[x];
				int cb = cbr[x / 2];
				int cr = crr[x / 2];
				int r = yy + ((91881  * (cr - 128)) >> 16);
				int g = yy - ((22554  * (cb - 128) + 46802 * (cr - 128)) >> 16);
				int b = yy + ((116130 * (cb - 128)) >> 16);
				dst[0] = clip8(b);
				dst[1] = clip8(g);
				dst[2] = clip8(r);
				dst[3] = ar[x];
			}
		}
	}

	//----------------------------------------------------------- レイヤ描画
	// 内部 BGRA バッファ (fw*fh) を Layer のメイン画像へ上書きコピー。
	// 書き込み左上 = (this.Left + fleft, this.Top + ftop)。範囲外はクリップ。
	void blitToLayer(iTJSDispatch2 *layer, std::vector<tjs_uint8> &bgra,
					 tjs_int fleft, tjs_int ftop, tjs_int fw, tjs_int fh)
	{
		tjs_int lw, lh, pitch;
		tjs_uint8 *lbuf;
		if (!getLayerWriteBuffer(layer, lw, lh, lbuf, pitch))
			TVPThrowExceptionMessage(TJS_W("AlphaMovie: target must be a Layer with image."));

		tjs_int dx0 = Left + fleft;
		tjs_int dy0 = Top + ftop;

		tjs_int sx = 0, sy = 0;
		tjs_int cw = fw, ch = fh;
		if (dx0 < 0) { sx = -dx0; cw += dx0; dx0 = 0; }
		if (dy0 < 0) { sy = -dy0; ch += dy0; dy0 = 0; }
		if (dx0 + cw > lw) cw = lw - dx0;
		if (dy0 + ch > lh) ch = lh - dy0;
		if (cw <= 0 || ch <= 0) return;

		for (tjs_int y = 0; y < ch; y++) {
			const tjs_uint8 *src = &bgra[(size_t)(sy + y) * fw * 4 + (size_t)sx * 4];
			tjs_uint8 *dst = lbuf + (size_t)(dy0 + y) * pitch + (size_t)dx0 * 4;
			memcpy(dst, src, (size_t)cw * 4);
		}

		// Notify the layer that its main image changed so software/GPU
		// present paths re-upload the dirty region.
		tTJSVariant *args[4];
		tTJSVariant vLeft((tjs_int)dx0);
		tTJSVariant vTop((tjs_int)dy0);
		tTJSVariant vW((tjs_int)cw);
		tTJSVariant vH((tjs_int)ch);
		args[0] = &vLeft;
		args[1] = &vTop;
		args[2] = &vW;
		args[3] = &vH;
		if(TJS_FAILED(layer->FuncCall(0, TJS_W("update"), nullptr, nullptr, 4,
									  args, layer))) {
			layer->FuncCall(0, TJS_W("update"), nullptr, nullptr, 0, nullptr,
							layer);
		}
	}
};

//---------------------------------------------------------------------------
// showNextImage は Layer オブジェクトを引数に取るため RawCallback で実装。
//---------------------------------------------------------------------------
static tjs_error TJS_INTF_METHOD
AlphaMovie_showNextImage(tTJSVariant *result, tjs_int numparams,
						 tTJSVariant **param, AlphaMovie *self)
{
	if (!self) return TJS_E_NATIVECLASSCRASH;
	if (numparams < 1) return TJS_E_BADPARAMCOUNT;

	iTJSDispatch2 *layer = 0;
	if (param[0]->Type() == tvtObject)
		layer = param[0]->AsObjectNoAddRef();
	if (!layer) TVPThrowExceptionMessage(TJS_W("AlphaMovie: showNextImage requires a Layer."));

	tjs_int frameNo = self->showNextImage(layer);
	if (result) *result = (tjs_int64)frameNo;
	return TJS_S_OK;
}

//---------------------------------------------------------------------------
// NCBIND クラス登録
//---------------------------------------------------------------------------
NCB_REGISTER_CLASS(AlphaMovie)
{
	Constructor();

	Method(TJS_W("open"),             &Class::open);
	Method(TJS_W("clear"),            &Class::clear);
	Method(TJS_W("isPlaying"),        &Class::isPlaying);
	Method(TJS_W("play"),             &Class::play);
	Method(TJS_W("stop"),             &Class::stop);
	Method(TJS_W("setPosition"),      &Class::setPosition);
	Method(TJS_W("setNextMovieFile"), &Class::setNextMovieFile);

	RawCallback(TJS_W("showNextImage"), &AlphaMovie_showNextImage, 0);

	Property(TJS_W("numOfFrame"),     &Class::getNumOfFrame,     (int)0);
	Property(TJS_W("frame"),          &Class::getFrame,          &Class::setFrame);
	Property(TJS_W("loop"),           &Class::getLoop,           &Class::setLoop);
	Property(TJS_W("nextLoop"),       &Class::getNextLoop,       &Class::setNextLoop);
	Property(TJS_W("preloadSamples"), &Class::getPreloadSamples, &Class::setPreloadSamples);
	Property(TJS_W("left"),           &Class::getLeft,           &Class::setLeft);
	Property(TJS_W("top"),            &Class::getTop,            &Class::setTop);
	Property(TJS_W("screenWidth"),    &Class::getScreenWidth,    (int)0);
	Property(TJS_W("screenHeight"),   &Class::getScreenHeight,   (int)0);
	// Compatibility aliases used by some scripts / older stubs
	Property(TJS_W("width"),          &Class::getScreenWidth,    (int)0);
	Property(TJS_W("height"),         &Class::getScreenHeight,   (int)0);
	Property(TJS_W("FPSScale"),       &Class::getFPSScale,       (int)0);
	Property(TJS_W("FPSRate"),        &Class::getFPSRate,        (int)0);
}
