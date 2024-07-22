/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2021  PCSX2 Dev Team
 *
 *  PCSX2 is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU Lesser General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  PCSX2 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with PCSX2.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include "PrecompiledHeader.h"
#include "Common.h"

#include "IPU.h"
#include "IPUdma.h"

#include <limits.h>
#include "Config.h"

#include "common/MemsetFast.inl"
#include "mpeg2_vlc.h"
#include <array>

#ifdef _M_ARM64
#ifdef _MSC_VER
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif
#endif

 // the IPU is fixed to 16 byte strides (128-bit / QWC resolution):
enum : uint
{
	decoder_stride = 16
};

struct macroblock_8
{
	u8 Y[16][16]; //0
	u8 Cb[8][8]; //1
	u8 Cr[8][8]; //2
};

struct macroblock_16
{
	s16 Y[16][16]; //0
	s16 Cb[8][8]; //1
	s16 Cr[8][8]; //2
};

struct macroblock_rgb32
{
	struct
	{
		u8 r, g, b, a;
	} c[16][16];
};

struct rgb16_t
{
	u16 r : 5, g : 5, b : 5, a : 1;
};

struct macroblock_rgb16
{
	rgb16_t c[16][16];
};

struct decoder_t
{
	/* first, state that carries information from one macroblock to the */
	/* next inside a slice, and is never used outside of mpeg2_slice() */

	/* DCT coefficients - should be kept aligned ! */
	s16 DCTblock[64];

	u8 niq[64]; //non-intraquant matrix (sequence header)
	u8 iq[64]; //intraquant matrix (sequence header)

	macroblock_8 mb8;
	macroblock_16 mb16;
	macroblock_rgb32 rgb32;
	macroblock_rgb16 rgb16;

	uint ipu0_data; // amount of data in the output macroblock (in QWC)
	uint ipu0_idx;

	int quantizer_scale;

	/* now non-slice-specific information */

	/* picture header stuff */

	/* what type of picture this is (I, P, B, D) */
	int coding_type;

	/* picture coding extension stuff */

	/* predictor for DC coefficients in intra blocks */
	s16 dc_dct_pred[3];

	/* quantization factor for intra dc coefficients */
	int intra_dc_precision;
	/* top/bottom/both fields */
	int picture_structure;
	/* bool to indicate all predictions are frame based */
	int frame_pred_frame_dct;
	/* bool to indicate whether intra blocks have motion vectors */
	/* (for concealment) */
	int concealment_motion_vectors;
	/* bit to indicate which quantization table to use */
	int q_scale_type;
	/* bool to use different vlc tables */
	int intra_vlc_format;
	/* used for DMV MC */
	int top_field_first;
	// Pseudo Sign Offset
	int sgn;
	// Dither Enable
	int dte;
	// Output Format
	int ofm;
	// Macroblock type
	int macroblock_modes;
	// DC Reset
	int dcr;
	// Coded block pattern
	int coded_block_pattern;

	/* stuff derived from bitstream */

	/* the zigzag scan we're supposed to be using, true for alt, false for normal */
	bool scantype;

	int mpeg1;

	template <typename T>
	void SetOutputTo(T& obj)
	{
		uint mb_offset = ((uptr)&obj - (uptr)&mb8);
		pxAssume((mb_offset & 15) == 0);
		ipu0_idx = mb_offset / 16;
		ipu0_data = sizeof(obj) / 16;
	}

	u128* GetIpuDataPtr()
	{
		return ((u128*)&mb8) + ipu0_idx;
	}

	void AdvanceIpuDataBy(uint amt)
	{
		pxAssertMsg(ipu0_data >= amt, "IPU FIFO Overflow on advance!");
		ipu0_idx += amt;
		ipu0_data -= amt;
	}
};

// The IPU can only do one task at once and never uses other buffers so all mpeg state variables
// are made available to mpeg/vlc modules as globals here:
// the BP doesn't advance and returns -1 if there is no data to be read

__aligned16 tIPU_BP g_BP;
static __aligned16 decoder_t decoder;
static __aligned16 tIPU_cmd ipu_cmd;

#ifdef _MSC_VER
#define BigEndian(in) _byteswap_ulong(in)
#else
#define BigEndian(in) __builtin_bswap32(in)
#endif

#ifdef _MSC_VER
#define BigEndian64(in) _byteswap_uint64(in)
#else
#define BigEndian64(in) __builtin_bswap64(in)
#endif

// --------------------------------------------------------------------------------------
//  Buffer reader
// --------------------------------------------------------------------------------------

static __ri u32 UBITS(uint bits)
{
	uint readpos8 = g_BP.BP / 8;

	uint result = BigEndian(*(u32*)((u8*)g_BP.internal_qwc + readpos8));
	uint bp7 = (g_BP.BP & 7);
	result <<= bp7;
	result >>= (32 - bits);

	return result;
}

static __ri s32 SBITS(uint bits)
{
	// Read an unaligned 32 bit value and then shift the bits up and then back down.

	uint readpos8 = g_BP.BP / 8;

	int result = BigEndian(*(s32*)((s8*)g_BP.internal_qwc + readpos8));
	uint bp7 = (g_BP.BP & 7);
	result <<= bp7;
	result >>= (32 - bits);

	return result;
}

// whenever reading fractions of bytes. The low bits always come from the next byte
// while the high bits come from the current byte
static u8 getBits64(u8* address, bool advance)
{
	if (!g_BP.FillBuffer(64))
		return 0;

	const u8* readpos = &g_BP.internal_qwc[0]._u8[g_BP.BP / 8];

	if (uint shift = (g_BP.BP & 7))
	{
		u64 mask = (0xff >> shift);
		mask = mask | (mask << 8) | (mask << 16) | (mask << 24) | (mask << 32) | (mask << 40) | (mask << 48) | (mask << 56);

		*(u64*)address = ((~mask & *(u64*)(readpos + 1)) >> (8 - shift)) | (((mask) & *(u64*)readpos) << shift);
	}
	else
	{
		*(u64*)address = *(u64*)readpos;
	}

	if (advance)
		g_BP.Advance(64);

	return 1;
}

// whenever reading fractions of bytes. The low bits always come from the next byte
// while the high bits come from the current byte
static __fi u8 getBits32(u8* address, bool advance)
{
	if (!g_BP.FillBuffer(32))
		return 0;

	const u8* readpos = &g_BP.internal_qwc->_u8[g_BP.BP / 8];

	if (uint shift = (g_BP.BP & 7))
	{
		u32 mask = (0xff >> shift);
		mask = mask | (mask << 8) | (mask << 16) | (mask << 24);

		*(u32*)address = ((~mask & *(u32*)(readpos + 1)) >> (8 - shift)) | (((mask) & *(u32*)readpos) << shift);
	}
	else
	{
		// Bit position-aligned -- no masking/shifting necessary
		*(u32*)address = *(u32*)readpos;
	}

	if (advance)
		g_BP.Advance(32);

	return 1;
}

static __fi u8 getBits16(u8* address, bool advance)
{
	if (!g_BP.FillBuffer(16))
		return 0;

	const u8* readpos = &g_BP.internal_qwc[0]._u8[g_BP.BP / 8];

	if (uint shift = (g_BP.BP & 7))
	{
		uint mask = (0xff >> shift);
		mask = mask | (mask << 8);
		*(u16*)address = ((~mask & *(u16*)(readpos + 1)) >> (8 - shift)) | (((mask) & *(u16*)readpos) << shift);
	}
	else
	{
		*(u16*)address = *(u16*)readpos;
	}

	if (advance)
		g_BP.Advance(16);

	return 1;
}

static u8 getBits8(u8* address, bool advance)
{
	if (!g_BP.FillBuffer(8))
		return 0;

	const u8* readpos = &g_BP.internal_qwc[0]._u8[g_BP.BP / 8];

	if (uint shift = (g_BP.BP & 7))
	{
		uint mask = (0xff >> shift);
		*(u8*)address = (((~mask) & readpos[1]) >> (8 - shift)) | (((mask) & *readpos) << shift);
	}
	else
	{
		*(u8*)address = *(u8*)readpos;
	}

	if (advance)
		g_BP.Advance(8);

	return 1;
}

static __fi int GETWORD()
{
	return g_BP.FillBuffer(16);
}

// Removes bits from the bitstream.  This is done independently of UBITS/SBITS because a
// lot of mpeg streams have to read ahead and rewind bits and re-read them at different
// bit depths or sign'age.
static __fi void REMOVEBITS(uint num)
{
	g_BP.Advance(num);
	//pxAssume(g_BP.FP != 0);
}

static __fi u32 GETBITS(uint num)
{
	uint retVal = UBITS(num);
	g_BP.Advance(num);

	return retVal;
}

/* Bitstream and buffer needs to be reallocated in order for successful
	reading of the old data. Here the old data stored in the 2nd slot
	of the internal buffer is copied to 1st slot, and the new data read
	into 1st slot is copied to the 2nd slot. Which will later be copied
	back to the 1st slot when 128bits have been read.
*/
static __fi int bitstream_init()
{
	return g_BP.FillBuffer(32);
}


static void mpeg2_idct_copy(s16* block, u8* dest, int stride);
static void mpeg2_idct_add(int last, s16* block, s16* dest, int stride);

static bool mpeg2sliceIDEC();
static bool mpeg2_slice();
static int get_macroblock_address_increment();
static int get_macroblock_modes();

static int get_motion_delta(const int f_code);
static int get_dmv();

static void ipu_csc(macroblock_8& mb8, macroblock_rgb32& rgb32, int sgn);
static void ipu_dither(const macroblock_rgb32& rgb32, macroblock_rgb16& rgb16, int dte);
static void ipu_vq(macroblock_rgb16& rgb16, u8* indx4);

static void yuv2rgb_reference();

#if defined(_M_X86_32) || defined(_M_X86_64)

#define yuv2rgb yuv2rgb_sse2
static void yuv2rgb_sse2();

#elif defined(_M_ARM64)

#define yuv2rgb yuv2rgb_neon
static void yuv2rgb_neon();

#endif

static constexpr std::array<u8, 64> ComputeMpeg2Scan(bool alt)
{
	constexpr u8 mpeg2_scan_norm[64] = {
		/* Zig-Zag scan pattern */
		0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5,
		12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
		35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
		58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};
	constexpr u8 mpeg2_scan_alt[64] = {
		/* Alternate scan pattern */
		0, 8, 16, 24, 1, 9, 2, 10, 17, 25, 32, 40, 48, 56, 57, 49,
		41, 33, 26, 18, 3, 11, 4, 12, 19, 27, 34, 42, 50, 58, 35, 43,
		51, 59, 20, 28, 5, 13, 6, 14, 21, 29, 36, 44, 52, 60, 37, 45,
		53, 61, 22, 30, 7, 15, 23, 31, 38, 46, 54, 62, 39, 47, 55, 63};

	std::array<u8, 64> ret = {};
	for (int i = 0; i < 64; i++)
	{
		const int j = alt ? mpeg2_scan_alt[i] : mpeg2_scan_norm[i];
		ret[i] = ((j & 0x36) >> 1) | ((j & 0x09) << 2);
	}

	return ret;
}

static constexpr std::array<u8, 64> mpeg2_scan_norm = ComputeMpeg2Scan(false);
static constexpr std::array<u8, 64> mpeg2_scan_alt = ComputeMpeg2Scan(true);

static constexpr int non_linear_quantizer_scale[] =
	{
		0, 1, 2, 3, 4, 5, 6, 7,
		8, 10, 12, 14, 16, 18, 20, 22,
		24, 28, 32, 36, 40, 44, 48, 52,
		56, 64, 72, 80, 88, 96, 104, 112};


void IPUWorker();

// Color conversion stuff, the memory layout is a total hack
// convert_data_buffer is a pointer to the internal rgb struct (the first param in convert_init_t)
//char convert_data_buffer[sizeof(convert_rgb_t)];
//char convert_data_buffer[0x1C];							// unused?
//u8 PCT[] = {'r', 'I', 'P', 'B', 'D', '-', '-', '-'};		// unused?

// Quantization matrix
static rgb16_t vqclut[16];			//clut conversion table
static u16 s_thresh[2];				//thresholds for color conversions
static int coded_block_pattern = 0;

alignas(16) static u8 indx4[16*16/2];

uint eecount_on_last_vdec = 0;
bool FMVstarted = false;
bool EnableFMV = false;

void tIPU_cmd::clear()
{
	memzero_sse_a(*this);
	current = 0xffffffff;
}

__fi void IPUProcessInterrupt()
{
	if (ipuRegs.ctrl.BUSY) // && (g_BP.FP || g_BP.IFC || (ipu1ch.chcr.STR && ipu1ch.qwc > 0)))
		IPUWorker();
	if (ipuRegs.ctrl.BUSY && ipuRegs.cmd.BUSY && ipuRegs.cmd.DATA == 0x000001B7) {
		// 0x000001B7 is the MPEG2 sequence end code, signalling the end of a video.
		// At the end of a video BUSY values should be automatically set to 0. 
		// This does not happen for Enthusia - Professional Racing, causing it to get stuck in an endless loop.
		ipuRegs.cmd.BUSY = 0;
		ipuRegs.ctrl.BUSY = 0;
	}
}

/////////////////////////////////////////////////////////
// Register accesses (run on EE thread)

void ipuReset()
{
	memzero(ipuRegs);
	memzero(g_BP);
	memzero(decoder);

	decoder.picture_structure = FRAME_PICTURE;      //default: progressive...my guess:P

	ipu_fifo.init();
	ipu_cmd.clear();
}

void ReportIPU()
{
	//Console.WriteLn(g_nDMATransfer.desc());
	Console.WriteLn(ipu_fifo.in.desc());
	Console.WriteLn(ipu_fifo.out.desc());
	Console.WriteLn(g_BP.desc());
	Console.WriteLn("vqclut = 0x%x.", vqclut);
	Console.WriteLn("s_thresh = 0x%x.", s_thresh);
	Console.WriteLn("coded_block_pattern = 0x%x.", coded_block_pattern);
	Console.WriteLn(ipu_cmd.desc());
	Console.Newline();
}

void SaveStateBase::ipuFreeze()
{
	// Get a report of the status of the ipu variables when saving and loading savestates.
	//ReportIPU();
	FreezeTag("IPU");
	Freeze(ipu_fifo);

	Freeze(g_BP);
	Freeze(vqclut);
	Freeze(s_thresh);
	Freeze(coded_block_pattern);
	Freeze(decoder);
	Freeze(ipu_cmd);
}

void tIPU_CMD_IDEC::log() const
{
	IPU_LOG("IDEC command.");

	if (FB) IPU_LOG(" Skip %d	bits.", FB);
	IPU_LOG(" Quantizer step code=0x%X.", QSC);

	if (DTD == 0)
		IPU_LOG(" Does not decode DT.");
	else
		IPU_LOG(" Decodes DT.");

	if (SGN == 0)
		IPU_LOG(" No bias.");
	else
		IPU_LOG(" Bias=128.");

	if (DTE == 1) IPU_LOG(" Dither Enabled.");
	if (OFM == 0)
		IPU_LOG(" Output format is RGB32.");
	else
		IPU_LOG(" Output format is RGB16.");

	IPU_LOG("");
}

void tIPU_CMD_BDEC::log(int s_bdec) const
{
	IPU_LOG("BDEC(macroblock decode) command %x, num: 0x%x", cpuRegs.pc, s_bdec);
	if (FB) IPU_LOG(" Skip 0x%X bits.", FB);

	if (MBI)
		IPU_LOG(" Intra MB.");
	else
		IPU_LOG(" Non-intra MB.");

	if (DCR)
		IPU_LOG(" Resets DC prediction value.");
	else
		IPU_LOG(" Doesn't reset DC prediction value.");

	if (DT)
		IPU_LOG(" Use field DCT.");
	else
		IPU_LOG(" Use frame DCT.");

	IPU_LOG(" Quantizer step=0x%X", QSC);
}

void tIPU_CMD_CSC::log_from_YCbCr() const
{
	IPU_LOG("CSC(Colorspace conversion from YCbCr) command (%d).", MBC);
	if (OFM)
		IPU_LOG("Output format is RGB16. ");
	else
		IPU_LOG("Output format is RGB32. ");

	if (DTE) IPU_LOG("Dithering enabled.");
}

void tIPU_CMD_CSC::log_from_RGB32() const
{
	IPU_LOG("PACK (Colorspace conversion from RGB32) command.");

	if (OFM)
		IPU_LOG("Output format is RGB16. ");
	else
		IPU_LOG("Output format is INDX4. ");

	if (DTE) IPU_LOG("Dithering enabled.");

	IPU_LOG("Number of macroblocks to be converted: %d", MBC);
}


__fi u32 ipuRead32(u32 mem)
{
	// Note: It's assumed that mem's input value is always in the 0x10002000 page
	// of memory (if not, it's probably bad code).

	pxAssert((mem & ~0xff) == 0x10002000);
	mem &= 0xff;	// ipu repeats every 0x100

	IPUProcessInterrupt();

	switch (mem)
	{
		ipucase(IPU_CMD) : // IPU_CMD
		{
			if (ipu_cmd.CMD != SCE_IPU_FDEC && ipu_cmd.CMD != SCE_IPU_VDEC)
			{
				if (getBits32((u8*)&ipuRegs.cmd.DATA, 0))
					ipuRegs.cmd.DATA = BigEndian(ipuRegs.cmd.DATA);
			}
			return ipuRegs.cmd.DATA;
		}

		ipucase(IPU_CTRL): // IPU_CTRL
		{
			ipuRegs.ctrl.IFC = g_BP.IFC;
			ipuRegs.ctrl.CBP = coded_block_pattern;

			if (!ipuRegs.ctrl.BUSY)
				IPU_LOG("read32: IPU_CTRL=0x%08X", ipuRegs.ctrl._u32);

			return ipuRegs.ctrl._u32;
		}

		ipucase(IPU_BP): // IPU_BP
		{
			pxAssume(g_BP.FP <= 2);
			
			ipuRegs.ipubp = g_BP.BP & 0x7f;
			ipuRegs.ipubp |= g_BP.IFC << 8;
			ipuRegs.ipubp |= g_BP.FP << 16;

			IPU_LOG("read32: IPU_BP=0x%08X", ipuRegs.ipubp);
			return ipuRegs.ipubp;
		}

		default:
			IPU_LOG("read32: Addr=0x%08X Value = 0x%08X", mem, psHu32(IPU_CMD + mem));
	}

	return psHu32(IPU_CMD + mem);
}

__fi RETURNS_R64 ipuRead64(u32 mem)
{
	// Note: It's assumed that mem's input value is always in the 0x10002000 page
	// of memory (if not, it's probably bad code).

	pxAssert((mem & ~0xff) == 0x10002000);
	mem &= 0xff;	// ipu repeats every 0x100

	IPUProcessInterrupt();

	switch (mem)
	{
		ipucase(IPU_CMD): // IPU_CMD
		{
			if (ipu_cmd.CMD != SCE_IPU_FDEC && ipu_cmd.CMD != SCE_IPU_VDEC)
			{
				if (getBits32((u8*)&ipuRegs.cmd.DATA, 0))
					ipuRegs.cmd.DATA = BigEndian(ipuRegs.cmd.DATA);
			}
			
			if (ipuRegs.cmd.DATA & 0xffffff)
				IPU_LOG("read64: IPU_CMD=BUSY=%x, DATA=%08X", ipuRegs.cmd.BUSY ? 1 : 0, ipuRegs.cmd.DATA);
			return r64_load(&ipuRegs.cmd._u64);
		}

		ipucase(IPU_CTRL):
			DevCon.Warning("reading 64bit IPU ctrl");
			break;

		ipucase(IPU_BP):
			DevCon.Warning("reading 64bit IPU top");
			break;

		ipucase(IPU_TOP): // IPU_TOP
			IPU_LOG("read64: IPU_TOP=%x,  bp = %d", ipuRegs.top, g_BP.BP);
			break;

		default:
			IPU_LOG("read64: Unknown=%x", mem);
			break;
	}
	return r64_load(&psHu64(IPU_CMD + mem));
}

void ipuSoftReset()
{
	ipu_fifo.clear();

	coded_block_pattern = 0;

	ipuRegs.ctrl.reset();
	ipuRegs.top = 0;
	ipu_cmd.clear();
	ipuRegs.cmd.BUSY = 0;
	ipuRegs.cmd.DATA = 0; // required for Enthusia - Professional Racing after fix, or will freeze at start of next video.

	memzero(g_BP);
	hwIntcIrq(INTC_IPU); // required for FightBox
}

__fi bool ipuWrite32(u32 mem, u32 value)
{
	// Note: It's assumed that mem's input value is always in the 0x10002000 page
	// of memory (if not, it's probably bad code).

	pxAssert((mem & ~0xfff) == 0x10002000);
	mem &= 0xfff;

	switch (mem)
	{
		ipucase(IPU_CMD): // IPU_CMD
			IPU_LOG("write32: IPU_CMD=0x%08X", value);
			IPUCMD_WRITE(value);
			IPUProcessInterrupt();
		return false;

		ipucase(IPU_CTRL): // IPU_CTRL
            // CTRL = the first 16 bits of ctrl [0x8000ffff], + value for the next 16 bits,
            // minus the reserved bits. (18-19; 27-29) [0x47f30000]
			ipuRegs.ctrl.write(value);
			if (ipuRegs.ctrl.IDP == 3)
			{
				Console.WriteLn("IPU Invalid Intra DC Precision, switching to 9 bits");
				ipuRegs.ctrl.IDP = 1;
			}

			if (ipuRegs.ctrl.RST) ipuSoftReset(); // RESET

			IPU_LOG("write32: IPU_CTRL=0x%08X", value);
		return false;
	}
	return true;
}

// returns FALSE when the writeback is handled, TRUE if the caller should do the
// writeback itself.
__fi bool ipuWrite64(u32 mem, u64 value)
{
	// Note: It's assumed that mem's input value is always in the 0x10002000 page
	// of memory (if not, it's probably bad code).

	pxAssert((mem & ~0xfff) == 0x10002000);
	mem &= 0xfff;

	switch (mem)
	{
		ipucase(IPU_CMD):
			IPU_LOG("write64: IPU_CMD=0x%08X", value);
			IPUCMD_WRITE((u32)value);
			IPUProcessInterrupt();
		return false;
	}

	return true;
}


//////////////////////////////////////////////////////
// IPU Commands (exec on worker thread only)

static void ipuBCLR(u32 val)
{
	// The Input FIFO shouldn't be cleared when the DMA is running, however if it is the DMA should drain
	// as it is constantly fighting it....
	while(ipu1ch.chcr.STR)
	{
		ipu_fifo.in.clear();
		ipu1Interrupt();
	}
	
	ipu_fifo.in.clear();

	memzero(g_BP);
	g_BP.BP = val & 0x7F;

	ipuRegs.ctrl.BUSY = 0;
	ipuRegs.cmd.BUSY = 0;
	IPU_LOG("Clear IPU input FIFO. Set Bit offset=0x%X", g_BP.BP);
}

static __ri void ipuIDEC(tIPU_CMD_IDEC idec)
{
	idec.log();

	//from IPU_CTRL
	ipuRegs.ctrl.PCT = I_TYPE; //Intra DECoding;)

	decoder.coding_type			= ipuRegs.ctrl.PCT;
	decoder.mpeg1				= ipuRegs.ctrl.MP1;
	decoder.q_scale_type		= ipuRegs.ctrl.QST;
	decoder.intra_vlc_format	= ipuRegs.ctrl.IVF;
	decoder.scantype			= ipuRegs.ctrl.AS;
	decoder.intra_dc_precision	= ipuRegs.ctrl.IDP;

//from IDEC value
	decoder.quantizer_scale		= idec.QSC;
	decoder.frame_pred_frame_dct= !idec.DTD;
	decoder.sgn = idec.SGN;
	decoder.dte = idec.DTE;
	decoder.ofm = idec.OFM;

	//other stuff
	decoder.dcr = 1; // resets DC prediction value
}

static int s_bdec = 0;

static __ri void ipuBDEC(tIPU_CMD_BDEC bdec)
{
	bdec.log(s_bdec);
	if (IsDebugBuild) s_bdec++;

	decoder.coding_type			= I_TYPE;
	decoder.mpeg1				= ipuRegs.ctrl.MP1;
	decoder.q_scale_type		= ipuRegs.ctrl.QST;
	decoder.intra_vlc_format	= ipuRegs.ctrl.IVF;
	decoder.scantype			= ipuRegs.ctrl.AS;
	decoder.intra_dc_precision	= ipuRegs.ctrl.IDP;

	//from BDEC value
	decoder.quantizer_scale		= decoder.q_scale_type ? non_linear_quantizer_scale [bdec.QSC] : bdec.QSC << 1;
	decoder.macroblock_modes	= bdec.DT ? DCT_TYPE_INTERLACED : 0;
	decoder.dcr					= bdec.DCR;
	decoder.macroblock_modes	|= bdec.MBI ? MACROBLOCK_INTRA : MACROBLOCK_PATTERN;

	memzero_sse_a(decoder.mb8);
	memzero_sse_a(decoder.mb16);
}

static __fi bool ipuVDEC(u32 val)
{
	if (EmuConfig.GS.FMVAspectRatioSwitch != FMVAspectRatioSwitchType::Off) {
		static int count = 0;
		if (count++ > 5) {
			if (!FMVstarted) {
				EnableFMV = true;
				FMVstarted = true;
			}
			count = 0;
		}
		eecount_on_last_vdec = cpuRegs.cycle;
	}
	switch (ipu_cmd.pos[0])
	{
		case 0:
			if (!bitstream_init()) return false;

			switch ((val >> 26) & 3)
			{
				case 0://Macroblock Address Increment
					decoder.mpeg1 = ipuRegs.ctrl.MP1;
					ipuRegs.cmd.DATA = get_macroblock_address_increment();
					break;

				case 1://Macroblock Type
					decoder.frame_pred_frame_dct = 1;
					decoder.coding_type = ipuRegs.ctrl.PCT > 0 ? ipuRegs.ctrl.PCT : 1; // Kaiketsu Zorro Mezase doesn't set a Picture type, seems happy with I
					ipuRegs.cmd.DATA = get_macroblock_modes();
					break;

				case 2://Motion Code
					ipuRegs.cmd.DATA = get_motion_delta(0);
					break;

				case 3://DMVector
					ipuRegs.cmd.DATA = get_dmv();
					break;

				jNO_DEFAULT
			}

			// HACK ATTACK!  This code OR's the MPEG decoder's bitstream position into the upper
			// 16 bits of DATA; which really doesn't make sense since (a) we already rewound the bits
			// back into the IPU internal buffer above, and (b) the IPU doesn't have an MPEG internal
			// 32-bit decoder buffer of its own anyway.  Furthermore, setting the upper 16 bits to
			// any value other than zero appears to work fine.  When set to zero, however, FMVs run
			// very choppy (basically only decoding/updating every 30th frame or so). So yeah,
			// someone with knowledge on the subject please feel free to explain this one. :) --air

			// The upper bits are the "length" of the decoded command, where the lower is the address.
			// This is due to differences with IPU and the MPEG standard. See get_macroblock_address_increment().

			ipuRegs.ctrl.ECD = (ipuRegs.cmd.DATA == 0);
			[[fallthrough]];

		case 1:
			if (!getBits32((u8*)&ipuRegs.top, 0))
			{
				ipu_cmd.pos[0] = 1;
				return false;
			}

			ipuRegs.top = BigEndian(ipuRegs.top);

			IPU_LOG("VDEC command data 0x%x(0x%x). Skip 0x%X bits/Table=%d (%s), pct %d",
			        ipuRegs.cmd.DATA, ipuRegs.cmd.DATA >> 16, val & 0x3f, (val >> 26) & 3, (val >> 26) & 1 ?
			        ((val >> 26) & 2 ? "DMV" : "MBT") : (((val >> 26) & 2 ? "MC" : "MBAI")), ipuRegs.ctrl.PCT);

			return true;

		jNO_DEFAULT
	}

	return false;
}

static __ri bool ipuFDEC(u32 val)
{
	if (!getBits32((u8*)&ipuRegs.cmd.DATA, 0)) return false;

	ipuRegs.cmd.DATA = BigEndian(ipuRegs.cmd.DATA);
	ipuRegs.top = ipuRegs.cmd.DATA;

	IPU_LOG("FDEC read: 0x%08x", ipuRegs.top);

	return true;
}

static bool ipuSETIQ(u32 val)
{
	if ((val >> 27) & 1)
	{
		u8 (&niq)[64] = decoder.niq;

		for(;ipu_cmd.pos[0] < 8; ipu_cmd.pos[0]++)
		{
			if (!getBits64((u8*)niq + 8 * ipu_cmd.pos[0], 1)) return false;
		}

		IPU_LOG("Read non-intra quantization matrix from FIFO.");
		for (uint i = 0; i < 8; i++)
		{
			IPU_LOG("%02X %02X %02X %02X %02X %02X %02X %02X",
			        niq[i * 8 + 0], niq[i * 8 + 1], niq[i * 8 + 2], niq[i * 8 + 3],
			        niq[i * 8 + 4], niq[i * 8 + 5], niq[i * 8 + 6], niq[i * 8 + 7]);
		}
	}
	else
	{
		u8 (&iq)[64] = decoder.iq;

		for(;ipu_cmd.pos[0] < 8; ipu_cmd.pos[0]++)
		{
			if (!getBits64((u8*)iq + 8 * ipu_cmd.pos[0], 1)) return false;
		}

		IPU_LOG("Read intra quantization matrix from FIFO.");
		for (uint i = 0; i < 8; i++)
		{
			IPU_LOG("%02X %02X %02X %02X %02X %02X %02X %02X",
			        iq[i * 8 + 0], iq[i * 8 + 1], iq[i * 8 + 2], iq[i *8 + 3],
			        iq[i * 8 + 4], iq[i * 8 + 5], iq[i * 8 + 6], iq[i *8 + 7]);
		}
	}

	return true;
}

static bool ipuSETVQ(u32 val)
{
	for(;ipu_cmd.pos[0] < 4; ipu_cmd.pos[0]++)
	{
		if (!getBits64(((u8*)vqclut) + 8 * ipu_cmd.pos[0], 1)) return false;
	}

	IPU_LOG("SETVQ command.   Read VQCLUT table from FIFO.\n"
	    "%02d:%02d:%02d %02d:%02d:%02d %02d:%02d:%02d %02d:%02d:%02d\n"
	    "%02d:%02d:%02d %02d:%02d:%02d %02d:%02d:%02d %02d:%02d:%02d\n"
	    "%02d:%02d:%02d %02d:%02d:%02d %02d:%02d:%02d %02d:%02d:%02d\n"
	    "%02d:%02d:%02d %02d:%02d:%02d %02d:%02d:%02d %02d:%02d:%02d",
	    vqclut[0].r, vqclut[0].g, vqclut[0].b,
	    vqclut[1].r, vqclut[1].g, vqclut[1].b,
	    vqclut[2].r, vqclut[2].g, vqclut[2].b,
	    vqclut[3].r, vqclut[3].g, vqclut[3].b,
	    vqclut[4].r, vqclut[4].g, vqclut[4].b,
	    vqclut[5].r, vqclut[5].g, vqclut[5].b,
	    vqclut[6].r, vqclut[6].g, vqclut[6].b,
	    vqclut[7].r, vqclut[7].g, vqclut[7].b,
	    vqclut[8].r, vqclut[8].g, vqclut[8].b,
	    vqclut[9].r, vqclut[9].g, vqclut[9].b,
	    vqclut[10].r, vqclut[10].g, vqclut[10].b,
	    vqclut[11].r, vqclut[11].g, vqclut[11].b,
	    vqclut[12].r, vqclut[12].g, vqclut[12].b,
	    vqclut[13].r, vqclut[13].g, vqclut[13].b,
	    vqclut[14].r, vqclut[14].g, vqclut[14].b,
	    vqclut[15].r, vqclut[15].g, vqclut[15].b);

	return true;
}

// IPU Transfers are split into 8Qwords so we need to send ALL the data
static __ri bool ipuCSC(tIPU_CMD_CSC csc)
{
	csc.log_from_YCbCr();

	for (;ipu_cmd.index < (int)csc.MBC; ipu_cmd.index++)
	{
		for(;ipu_cmd.pos[0] < 48; ipu_cmd.pos[0]++)
		{
			if (!getBits64((u8*)&decoder.mb8 + 8 * ipu_cmd.pos[0], 1)) return false;
		}

		ipu_csc(decoder.mb8, decoder.rgb32, 0);
		if (csc.OFM) ipu_dither(decoder.rgb32, decoder.rgb16, csc.DTE);
		
		if (csc.OFM)
		{
			ipu_cmd.pos[1] += ipu_fifo.out.write(((u32*) & decoder.rgb16) + 4 * ipu_cmd.pos[1], 32 - ipu_cmd.pos[1]);
			if (ipu_cmd.pos[1] < 32) return false;
		}
		else
		{
			ipu_cmd.pos[1] += ipu_fifo.out.write(((u32*) & decoder.rgb32) + 4 * ipu_cmd.pos[1], 64 - ipu_cmd.pos[1]);
			if (ipu_cmd.pos[1] < 64) return false;
		}

		ipu_cmd.pos[0] = 0;
		ipu_cmd.pos[1] = 0;
	}

	return true;
}

static __ri bool ipuPACK(tIPU_CMD_CSC csc)
{
	csc.log_from_RGB32();

	for (;ipu_cmd.index < (int)csc.MBC; ipu_cmd.index++)
	{
		for(;ipu_cmd.pos[0] < (int)sizeof(macroblock_rgb32) / 8; ipu_cmd.pos[0]++)
		{
			if (!getBits64((u8*)&decoder.rgb32 + 8 * ipu_cmd.pos[0], 1)) return false;
		}

		ipu_dither(decoder.rgb32, decoder.rgb16, csc.DTE);

		if (!csc.OFM) ipu_vq(decoder.rgb16, indx4);

		if (csc.OFM)
		{
			ipu_cmd.pos[1] += ipu_fifo.out.write(((u32*) & decoder.rgb16) + 4 * ipu_cmd.pos[1], 32 - ipu_cmd.pos[1]);
			if (ipu_cmd.pos[1] < 32) return false;
		}
		else
		{
			ipu_cmd.pos[1] += ipu_fifo.out.write(((u32*)indx4) + 4 * ipu_cmd.pos[1], 8 - ipu_cmd.pos[1]);
			if (ipu_cmd.pos[1] < 8) return false;
		}

		ipu_cmd.pos[0] = 0;
		ipu_cmd.pos[1] = 0;
	}

	return TRUE;
}

static void ipuSETTH(u32 val)
{
	s_thresh[0] = (val & 0x1ff);
	s_thresh[1] = ((val >> 16) & 0x1ff);
	IPU_LOG("SETTH (Set threshold value)command %x.", val&0x1ff01ff);
}

// --------------------------------------------------------------------------------------
//  CORE Functions (referenced from MPEG library)
// --------------------------------------------------------------------------------------
__fi void ipu_csc(macroblock_8& mb8, macroblock_rgb32& rgb32, int sgn)
{
	int i;
	u8* p = (u8*)&rgb32;

	yuv2rgb();

	if (s_thresh[0] > 0)
	{
		for (i = 0; i < 16*16; i++, p += 4)
		{
			if ((p[0] < s_thresh[0]) && (p[1] < s_thresh[0]) && (p[2] < s_thresh[0]))
				*(u32*)p = 0;
			else if ((p[0] < s_thresh[1]) && (p[1] < s_thresh[1]) && (p[2] < s_thresh[1]))
				p[3] = 0x40;
		}
	}
	else if (s_thresh[1] > 0)
	{
		for (i = 0; i < 16*16; i++, p += 4)
		{
			if ((p[0] < s_thresh[1]) && (p[1] < s_thresh[1]) && (p[2] < s_thresh[1]))
				p[3] = 0x40;
		}
	}
	if (sgn)
	{
		for (i = 0; i < 16*16; i++, p += 4)
		{
			*(u32*)p ^= 0x808080;
		}
	}
}

__fi void ipu_vq(macroblock_rgb16& rgb16, u8* indx4)
{
	const auto closest_index = [&](int i, int j) {
		u8 index = 0;
		int min_distance = std::numeric_limits<int>::max();
		for (u8 k = 0; k < 16; ++k)
		{
			const int dr = rgb16.c[i][j].r - vqclut[k].r;
			const int dg = rgb16.c[i][j].g - vqclut[k].g;
			const int db = rgb16.c[i][j].b - vqclut[k].b;
			const int distance = dr * dr + dg * dg + db * db;

			// XXX: If two distances are the same which index is used?
			if (min_distance > distance)
			{
				index = k;
				min_distance = distance;
			}
		}

		return index;
	};

	for (int i = 0; i < 16; ++i)
		for (int j = 0; j < 8; ++j)
			indx4[i * 8 + j] = closest_index(i, 2 * j + 1) << 4 | closest_index(i, 2 * j);
}


// --------------------------------------------------------------------------------------
//  IPU Worker / Dispatcher
// --------------------------------------------------------------------------------------

// When a command is written, we set some various busy flags and clear some other junk.
// The actual decoding will be handled by IPUworker.
__fi void IPUCMD_WRITE(u32 val)
{
	// don't process anything if currently busy
	//if (ipuRegs.ctrl.BUSY) Console.WriteLn("IPU BUSY!"); // wait for thread

	ipuRegs.ctrl.ECD = 0;
	ipuRegs.ctrl.SCD = 0;
	ipu_cmd.clear();
	ipu_cmd.current = val;

	switch (ipu_cmd.CMD)
	{
		// BCLR and SETTH  require no data so they always execute inline:

		case SCE_IPU_BCLR:
			ipuBCLR(val);
			hwIntcIrq(INTC_IPU); //DMAC_TO_IPU
			ipuRegs.ctrl.BUSY = 0;
			return;

		case SCE_IPU_SETTH:
			ipuSETTH(val);
			hwIntcIrq(INTC_IPU);
			ipuRegs.ctrl.BUSY = 0;
			return;



		case SCE_IPU_IDEC:
			g_BP.Advance(val & 0x3F);
			ipuIDEC(val);
			ipuRegs.SetTopBusy();
			break;

		case SCE_IPU_BDEC:
			g_BP.Advance(val & 0x3F);
			ipuBDEC(val);
			ipuRegs.SetTopBusy();
			break;

		case SCE_IPU_VDEC:
			g_BP.Advance(val & 0x3F);
			ipuRegs.SetDataBusy();
			break;

		case SCE_IPU_FDEC:
			IPU_LOG("FDEC command. Skip 0x%X bits, FIFO 0x%X qwords, BP 0x%X, CHCR 0x%x",
			        val & 0x3f, g_BP.IFC, g_BP.BP, ipu1ch.chcr._u32);

			g_BP.Advance(val & 0x3F);
			ipuRegs.SetDataBusy();
			break;

		case SCE_IPU_SETIQ:
			IPU_LOG("SETIQ command.");
			g_BP.Advance(val & 0x3F);
			break;

		case SCE_IPU_SETVQ:
			break;

		case SCE_IPU_CSC:
			break;

		case SCE_IPU_PACK:
			break;

		jNO_DEFAULT;
			}

	ipuRegs.ctrl.BUSY = 1;

	//if(!ipu1ch.chcr.STR) hwIntcIrq(INTC_IPU);
}

__noinline void IPUWorker()
{
	pxAssert(ipuRegs.ctrl.BUSY);

	switch (ipu_cmd.CMD)
	{
		// These are unreachable (BUSY will always be 0 for them)
		//case SCE_IPU_BCLR:
		//case SCE_IPU_SETTH:
			//break;

		case SCE_IPU_IDEC:
			if (!mpeg2sliceIDEC()) return;

			//ipuRegs.ctrl.OFC = 0;
			ipuRegs.topbusy = 0;
			ipuRegs.cmd.BUSY = 0;

			// CHECK!: IPU0dma remains when IDEC is done, so we need to clear it
			// Check Mana Khemia 1 "off campus" to trigger a GUST IDEC messup.
			// This hackfixes it :/
			//if (ipu0ch.qwc > 0 && ipu0ch.chcr.STR) ipu0Interrupt();
			break;

		case SCE_IPU_BDEC:
			if (!mpeg2_slice()) return;

			ipuRegs.topbusy = 0;
			ipuRegs.cmd.BUSY = 0;

			//if (ipuRegs.ctrl.SCD || ipuRegs.ctrl.ECD) hwIntcIrq(INTC_IPU);
			break;

		case SCE_IPU_VDEC:
			if (!ipuVDEC(ipu_cmd.current)) return;

			ipuRegs.topbusy = 0;
			ipuRegs.cmd.BUSY = 0;
			break;

		case SCE_IPU_FDEC:
			if (!ipuFDEC(ipu_cmd.current)) return;

			ipuRegs.topbusy = 0;
			ipuRegs.cmd.BUSY = 0;
			break;

		case SCE_IPU_SETIQ:
			if (!ipuSETIQ(ipu_cmd.current)) return;
			break;

		case SCE_IPU_SETVQ:
			if (!ipuSETVQ(ipu_cmd.current)) return;
			break;

		case SCE_IPU_CSC:
			if (!ipuCSC(ipu_cmd.current)) return;
			break;

		case SCE_IPU_PACK:
			if (!ipuPACK(ipu_cmd.current)) return;
			break;

		jNO_DEFAULT
			}

	// success
	ipuRegs.ctrl.BUSY = 0;
	//ipu_cmd.current = 0xffffffff;
	hwIntcIrq(INTC_IPU);

	// Fill the FIFO ready for the next command
	if (ipu1ch.chcr.STR && cpuRegs.eCycle[4] == 0x9999)
	{
		CPU_INT(DMAC_TO_IPU, 32);
	}
}

static const DCTtab* tab;
static int mbaCount = 0;

static int get_macroblock_modes()
{
	int macroblock_modes;
	const MBtab* tab;

	switch (decoder.coding_type)
	{
	case I_TYPE:
		macroblock_modes = UBITS(2);

		if (macroblock_modes == 0) return 0;   // error

		tab = MB_I + (macroblock_modes >> 1);
		REMOVEBITS(tab->len);
		macroblock_modes = tab->modes;

		if ((!(decoder.frame_pred_frame_dct)) &&
			(decoder.picture_structure == FRAME_PICTURE))
		{
			macroblock_modes |= GETBITS(1) * DCT_TYPE_INTERLACED;
		}
		return macroblock_modes;

	case P_TYPE:
		macroblock_modes = UBITS(6);

		if (macroblock_modes == 0) return 0;   // error

		tab = MB_P + (macroblock_modes >> 1);
		REMOVEBITS(tab->len);
		macroblock_modes = tab->modes;

		if (decoder.picture_structure != FRAME_PICTURE)
		{
			if (macroblock_modes & MACROBLOCK_MOTION_FORWARD)
			{
				macroblock_modes |= GETBITS(2) * MOTION_TYPE_BASE;
			}

			return macroblock_modes;
		}
		else if (decoder.frame_pred_frame_dct)
		{
			if (macroblock_modes & MACROBLOCK_MOTION_FORWARD)
				macroblock_modes |= MC_FRAME;

			return macroblock_modes;
		}
		else
		{
			if (macroblock_modes & MACROBLOCK_MOTION_FORWARD)
			{
				macroblock_modes |= GETBITS(2) * MOTION_TYPE_BASE;
			}

			if (macroblock_modes & (MACROBLOCK_INTRA | MACROBLOCK_PATTERN))
			{
				macroblock_modes |= GETBITS(1) * DCT_TYPE_INTERLACED;
			}

			return macroblock_modes;
		}

	case B_TYPE:
		macroblock_modes = UBITS(6);

		if (macroblock_modes == 0) return 0;   // error

		tab = MB_B + macroblock_modes;
		REMOVEBITS(tab->len);
		macroblock_modes = tab->modes;

		if (decoder.picture_structure != FRAME_PICTURE)
		{
			if (!(macroblock_modes & MACROBLOCK_INTRA))
			{
				macroblock_modes |= GETBITS(2) * MOTION_TYPE_BASE;
			}
			return (macroblock_modes | (tab->len << 16));
		}
		else if (decoder.frame_pred_frame_dct)
		{
			/* if (! (macroblock_modes & MACROBLOCK_INTRA)) */
			macroblock_modes |= MC_FRAME;
			return (macroblock_modes | (tab->len << 16));
		}
		else
		{
			if (macroblock_modes & MACROBLOCK_INTRA) goto intra;

			macroblock_modes |= GETBITS(2) * MOTION_TYPE_BASE;

			if (macroblock_modes & (MACROBLOCK_INTRA | MACROBLOCK_PATTERN))
			{
			intra:
				macroblock_modes |= GETBITS(1) * DCT_TYPE_INTERLACED;
			}
			return (macroblock_modes | (tab->len << 16));
		}

	case D_TYPE:
		macroblock_modes = GETBITS(1);
		//I suspect (as this is actually a 2 bit command) that this should be getbits(2)
		//additionally, we arent dumping any bits here when i think we should be, need a game to test. (Refraction)
		DevCon.Warning(" Rare MPEG command! ");
		if (macroblock_modes == 0) return 0;   // error
		return (MACROBLOCK_INTRA | (1 << 16));

	default:
		return 0;
	}
}

static __fi int get_quantizer_scale()
{
	int quantizer_scale_code;

	quantizer_scale_code = GETBITS(5);

	if (decoder.q_scale_type)
		return non_linear_quantizer_scale[quantizer_scale_code];
	else
		return quantizer_scale_code << 1;
}

static __fi int get_coded_block_pattern()
{
	const CBPtab* tab;
	u16 code = UBITS(16);

	if (code >= 0x2000)
		tab = CBP_7 + (UBITS(7) - 16);
	else
		tab = CBP_9 + UBITS(9);

	REMOVEBITS(tab->len);
	return tab->cbp;
}

int __fi get_motion_delta(const int f_code)
{
	int delta;
	int sign;
	const MVtab* tab;
	u16 code = UBITS(16);

	if ((code & 0x8000))
	{
		REMOVEBITS(1);
		return 0x00010000;
	}
	else if ((code & 0xf000) || ((code & 0xfc00) == 0x0c00))
	{
		tab = MV_4 + UBITS(4);
	}
	else
	{
		tab = MV_10 + UBITS(10);
	}

	delta = tab->delta + 1;
	REMOVEBITS(tab->len);

	sign = SBITS(1);
	REMOVEBITS(1);

	return (((delta ^ sign) - sign) | (tab->len << 16));
}

int __fi get_dmv()
{
	const DMVtab* tab = DMV_2 + UBITS(2);
	REMOVEBITS(tab->len);
	return (tab->dmv | (tab->len << 16));
}

int get_macroblock_address_increment()
{
	const MBAtab* mba;

	u16 code = UBITS(16);

	if (code >= 4096)
		mba = MBA.mba5 + (UBITS(5) - 2);
	else if (code >= 768)
		mba = MBA.mba11 + (UBITS(11) - 24);
	else switch (UBITS(11))
	{
	case 8:		/* macroblock_escape */
		REMOVEBITS(11);
		return 0xb0023;

	case 15:	/* macroblock_stuffing (MPEG1 only) */
		if (decoder.mpeg1)
		{
			REMOVEBITS(11);
			return 0xb0022;
		}
		[[fallthrough]];

	default:
		return 0;//error
	}

	REMOVEBITS(mba->len);

	return ((mba->mba + 1) | (mba->len << 16));
}

static __fi int get_luma_dc_dct_diff()
{
	int size;
	int dc_diff;
	u16 code = UBITS(5);

	if (code < 31)
	{
		size = DCtable.lum0[code].size;
		REMOVEBITS(DCtable.lum0[code].len);

		// 5 bits max
	}
	else
	{
		code = UBITS(9) - 0x1f0;
		size = DCtable.lum1[code].size;
		REMOVEBITS(DCtable.lum1[code].len);

		// 9 bits max
	}

	if (size == 0)
		dc_diff = 0;
	else
	{
		dc_diff = GETBITS(size);

		// 6 for tab0 and 11 for tab1
		if ((dc_diff & (1 << (size - 1))) == 0)
			dc_diff -= (1 << size) - 1;
	}

	return dc_diff;
}

static __fi int get_chroma_dc_dct_diff()
{
	int size;
	int dc_diff;
	u16 code = UBITS(5);

	if (code < 31)
	{
		size = DCtable.chrom0[code].size;
		REMOVEBITS(DCtable.chrom0[code].len);
	}
	else
	{
		code = UBITS(10) - 0x3e0;
		size = DCtable.chrom1[code].size;
		REMOVEBITS(DCtable.chrom1[code].len);
	}

	if (size == 0)
		dc_diff = 0;
	else
	{
		dc_diff = GETBITS(size);

		if ((dc_diff & (1 << (size - 1))) == 0)
		{
			dc_diff -= (1 << size) - 1;
		}
	}

	return dc_diff;
}

static __fi void SATURATE(int& val)
{
	if ((u32)(val + 2048) > 4095)
		val = (val >> 31) ^ 2047;
}

static bool get_intra_block()
{
	const u8* scan = decoder.scantype ? mpeg2_scan_alt.data() : mpeg2_scan_norm.data();
	const u8(&quant_matrix)[64] = decoder.iq;
	int quantizer_scale = decoder.quantizer_scale;
	s16* dest = decoder.DCTblock;
	u16 code;

	/* decode AC coefficients */
	for (int i = 1 + ipu_cmd.pos[4]; ; i++)
	{
		switch (ipu_cmd.pos[5])
		{
		case 0:
			if (!GETWORD())
			{
				ipu_cmd.pos[4] = i - 1;
				return false;
			}

			code = UBITS(16);

			if (code >= 16384 && (!decoder.intra_vlc_format || decoder.mpeg1))
			{
				tab = &DCT.next[(code >> 12) - 4];
			}
			else if (code >= 1024)
			{
				if (decoder.intra_vlc_format && !decoder.mpeg1)
				{
					tab = &DCT.tab0a[(code >> 8) - 4];
				}
				else
				{
					tab = &DCT.tab0[(code >> 8) - 4];
				}
			}
			else if (code >= 512)
			{
				if (decoder.intra_vlc_format && !decoder.mpeg1)
				{
					tab = &DCT.tab1a[(code >> 6) - 8];
				}
				else
				{
					tab = &DCT.tab1[(code >> 6) - 8];
				}
			}

			// [TODO] Optimization: Following codes can all be done by a single "expedited" lookup
			// that should use a single unrolled DCT table instead of five separate tables used
			// here.  Multiple conditional statements are very slow, while modern CPU data caches
			// have lots of room to spare.

			else if (code >= 256)
			{
				tab = &DCT.tab2[(code >> 4) - 16];
			}
			else if (code >= 128)
			{
				tab = &DCT.tab3[(code >> 3) - 16];
			}
			else if (code >= 64)
			{
				tab = &DCT.tab4[(code >> 2) - 16];
			}
			else if (code >= 32)
			{
				tab = &DCT.tab5[(code >> 1) - 16];
			}
			else if (code >= 16)
			{
				tab = &DCT.tab6[code - 16];
			}
			else
			{
				ipu_cmd.pos[4] = 0;
				return true;
			}

			REMOVEBITS(tab->len);

			if (tab->run == 64) /* end_of_block */
			{
				ipu_cmd.pos[4] = 0;
				return true;
			}

			i += (tab->run == 65) ? GETBITS(6) : tab->run;
			if (i >= 64)
			{
				ipu_cmd.pos[4] = 0;
				return true;
			}
			[[fallthrough]];

		case 1:
			{
				if (!GETWORD())
				{
					ipu_cmd.pos[4] = i - 1;
					ipu_cmd.pos[5] = 1;
					return false;
				}

				uint j = scan[i];
				int val;

				if (tab->run == 65) /* escape */
				{
					if (!decoder.mpeg1)
					{
						val = (SBITS(12) * quantizer_scale * quant_matrix[i]) >> 4;
						REMOVEBITS(12);
					}
					else
					{
						val = SBITS(8);
						REMOVEBITS(8);

						if (!(val & 0x7f))
						{
							val = GETBITS(8) + 2 * val;
						}

						val = (val * quantizer_scale * quant_matrix[i]) >> 4;
						val = (val + ~(((s32)val) >> 31)) | 1;
					}
				}
				else
				{
					val = (tab->level * quantizer_scale * quant_matrix[i]) >> 4;
					if (decoder.mpeg1)
					{
						/* oddification */
						val = (val - 1) | 1;
					}

					/* if (bitstream_get (1)) val = -val; */
					int bit1 = SBITS(1);
					val = (val ^ bit1) - bit1;
					REMOVEBITS(1);
				}

				SATURATE(val);
				dest[j] = val;
				ipu_cmd.pos[5] = 0;
			}
		}
	}

	ipu_cmd.pos[4] = 0;
	return true;
}

static bool get_non_intra_block(int* last)
{
	int i;
	int j;
	int val;
	const u8* scan = decoder.scantype ? mpeg2_scan_alt.data() : mpeg2_scan_norm.data();
	const u8(&quant_matrix)[64] = decoder.niq;
	int quantizer_scale = decoder.quantizer_scale;
	s16* dest = decoder.DCTblock;
	u16 code;

	/* decode AC coefficients */
	for (i = ipu_cmd.pos[4]; ; i++)
	{
		switch (ipu_cmd.pos[5])
		{
		case 0:
			if (!GETWORD())
			{
				ipu_cmd.pos[4] = i;
				return false;
			}

			code = UBITS(16);

			if (code >= 16384)
			{
				if (i == 0)
				{
					tab = &DCT.first[(code >> 12) - 4];
				}
				else
				{
					tab = &DCT.next[(code >> 12) - 4];
				}
			}
			else if (code >= 1024)
			{
				tab = &DCT.tab0[(code >> 8) - 4];
			}
			else if (code >= 512)
			{
				tab = &DCT.tab1[(code >> 6) - 8];
			}

			// [TODO] Optimization: Following codes can all be done by a single "expedited" lookup
			// that should use a single unrolled DCT table instead of five separate tables used
			// here.  Multiple conditional statements are very slow, while modern CPU data caches
			// have lots of room to spare.

			else if (code >= 256)
			{
				tab = &DCT.tab2[(code >> 4) - 16];
			}
			else if (code >= 128)
			{
				tab = &DCT.tab3[(code >> 3) - 16];
			}
			else if (code >= 64)
			{
				tab = &DCT.tab4[(code >> 2) - 16];
			}
			else if (code >= 32)
			{
				tab = &DCT.tab5[(code >> 1) - 16];
			}
			else if (code >= 16)
			{
				tab = &DCT.tab6[code - 16];
			}
			else
			{
				ipu_cmd.pos[4] = 0;
				return true;
			}

			REMOVEBITS(tab->len);

			if (tab->run == 64) /* end_of_block */
			{
				*last = i;
				ipu_cmd.pos[4] = 0;
				return true;
			}

			i += (tab->run == 65) ? GETBITS(6) : tab->run;
			if (i >= 64)
			{
				*last = i;
				ipu_cmd.pos[4] = 0;
				return true;
			}
			[[fallthrough]];

		case 1:
			if (!GETWORD())
			{
				ipu_cmd.pos[4] = i;
				ipu_cmd.pos[5] = 1;
				return false;
			}

			j = scan[i];

			if (tab->run == 65) /* escape */
			{
				if (!decoder.mpeg1)
				{
					val = ((2 * (SBITS(12) + SBITS(1)) + 1) * quantizer_scale * quant_matrix[i]) >> 5;
					REMOVEBITS(12);
				}
				else
				{
					val = SBITS(8);
					REMOVEBITS(8);

					if (!(val & 0x7f))
					{
						val = GETBITS(8) + 2 * val;
					}

					val = ((2 * (val + (((s32)val) >> 31)) + 1) * quantizer_scale * quant_matrix[i]) / 32;
					val = (val + ~(((s32)val) >> 31)) | 1;
				}
			}
			else
			{
				int bit1 = SBITS(1);
				val = ((2 * tab->level + 1) * quantizer_scale * quant_matrix[i]) >> 5;
				val = (val ^ bit1) - bit1;
				REMOVEBITS(1);
			}

			SATURATE(val);
			dest[j] = val;
			ipu_cmd.pos[5] = 0;
		}
	}

	ipu_cmd.pos[4] = 0;
	return true;
}

static __fi bool slice_intra_DCT(const int cc, u8* const dest, const int stride, const bool skip)
{
	if (!skip || ipu_cmd.pos[3])
	{
		ipu_cmd.pos[3] = 0;
		if (!GETWORD())
		{
			ipu_cmd.pos[3] = 1;
			return false;
		}

		/* Get the intra DC coefficient and inverse quantize it */
		if (cc == 0)
			decoder.dc_dct_pred[0] += get_luma_dc_dct_diff();
		else
			decoder.dc_dct_pred[cc] += get_chroma_dc_dct_diff();

		decoder.DCTblock[0] = decoder.dc_dct_pred[cc] << (3 - decoder.intra_dc_precision);
	}

	if (!get_intra_block())
	{
		return false;
	}

	mpeg2_idct_copy(decoder.DCTblock, dest, stride);

	return true;
}

static __fi bool slice_non_intra_DCT(s16* const dest, const int stride, const bool skip)
{
	int last;

	if (!skip)
	{
		memzero_sse_a(decoder.DCTblock);
	}

	if (!get_non_intra_block(&last))
	{
		return false;
	}

	mpeg2_idct_add(last, decoder.DCTblock, dest, stride);

	return true;
}

void __fi finishmpeg2sliceIDEC()
{
	ipuRegs.ctrl.SCD = 0;
	coded_block_pattern = decoder.coded_block_pattern;
}

__fi bool mpeg2sliceIDEC()
{
	u16 code;

	switch (ipu_cmd.pos[0])
	{
	case 0:
		decoder.dc_dct_pred[0] =
			decoder.dc_dct_pred[1] =
			decoder.dc_dct_pred[2] = 128 << decoder.intra_dc_precision;

		ipuRegs.top = 0;
		ipuRegs.ctrl.ECD = 0;
		[[fallthrough]];

	case 1:
		ipu_cmd.pos[0] = 1;
		if (!bitstream_init())
		{
			return false;
		}
		[[fallthrough]];

	case 2:
		ipu_cmd.pos[0] = 2;
		while (1)
		{
			macroblock_8& mb8 = decoder.mb8;
			macroblock_rgb16& rgb16 = decoder.rgb16;
			macroblock_rgb32& rgb32 = decoder.rgb32;

			int DCT_offset, DCT_stride;
			const MBAtab* mba;

			switch (ipu_cmd.pos[1])
			{
			case 0:
				decoder.macroblock_modes = get_macroblock_modes();

				if (decoder.macroblock_modes & MACROBLOCK_QUANT) //only IDEC
				{
					decoder.quantizer_scale = get_quantizer_scale();
				}

				decoder.coded_block_pattern = 0x3F;//all 6 blocks
				memzero_sse_a(mb8);
				memzero_sse_a(rgb32);
				[[fallthrough]];

			case 1:
				ipu_cmd.pos[1] = 1;

				if (decoder.macroblock_modes & DCT_TYPE_INTERLACED)
				{
					DCT_offset = decoder_stride;
					DCT_stride = decoder_stride * 2;
				}
				else
				{
					DCT_offset = decoder_stride * 8;
					DCT_stride = decoder_stride;
				}

				switch (ipu_cmd.pos[2])
				{
				case 0:
				case 1:
					if (!slice_intra_DCT(0, (u8*)mb8.Y, DCT_stride, ipu_cmd.pos[2] == 1))
					{
						ipu_cmd.pos[2] = 1;
						return false;
					}
					[[fallthrough]];

				case 2:
					if (!slice_intra_DCT(0, (u8*)mb8.Y + 8, DCT_stride, ipu_cmd.pos[2] == 2))
					{
						ipu_cmd.pos[2] = 2;
						return false;
					}
					[[fallthrough]];

				case 3:
					if (!slice_intra_DCT(0, (u8*)mb8.Y + DCT_offset, DCT_stride, ipu_cmd.pos[2] == 3))
					{
						ipu_cmd.pos[2] = 3;
						return false;
					}
					[[fallthrough]];

				case 4:
					if (!slice_intra_DCT(0, (u8*)mb8.Y + DCT_offset + 8, DCT_stride, ipu_cmd.pos[2] == 4))
					{
						ipu_cmd.pos[2] = 4;
						return false;
					}
					[[fallthrough]];

				case 5:
					if (!slice_intra_DCT(1, (u8*)mb8.Cb, decoder_stride >> 1, ipu_cmd.pos[2] == 5))
					{
						ipu_cmd.pos[2] = 5;
						return false;
					}
					[[fallthrough]];

				case 6:
					if (!slice_intra_DCT(2, (u8*)mb8.Cr, decoder_stride >> 1, ipu_cmd.pos[2] == 6))
					{
						ipu_cmd.pos[2] = 6;
						return false;
					}
					break;

					jNO_DEFAULT;
				}

				// Send The MacroBlock via DmaIpuFrom
				ipu_csc(mb8, rgb32, decoder.sgn);

				if (decoder.ofm == 0)
					decoder.SetOutputTo(rgb32);
				else
				{
					ipu_dither(rgb32, rgb16, decoder.dte);
					decoder.SetOutputTo(rgb16);
				}
				[[fallthrough]];

			case 2:
				{
					pxAssert(decoder.ipu0_data > 0);

					uint read = ipu_fifo.out.write((u32*)decoder.GetIpuDataPtr(), decoder.ipu0_data);
					decoder.AdvanceIpuDataBy(read);

					if (decoder.ipu0_data != 0)
					{
						// IPU FIFO filled up -- Will have to finish transferring later.
						ipu_cmd.pos[1] = 2;
						return false;
					}

					mbaCount = 0;
				}
				[[fallthrough]];

			case 3:
				while (1)
				{
					if (!GETWORD())
					{
						ipu_cmd.pos[1] = 3;
						return false;
					}

					code = UBITS(16);
					if (code >= 0x1000)
					{
						mba = MBA.mba5 + (UBITS(5) - 2);
						break;
					}
					else if (code >= 0x0300)
					{
						mba = MBA.mba11 + (UBITS(11) - 24);
						break;
					}
					else switch (UBITS(11))
					{
					case 8:		/* macroblock_escape */
						mbaCount += 33;
						[[fallthrough]];

					case 15:	/* macroblock_stuffing (MPEG1 only) */
						REMOVEBITS(11);
						continue;

					default:	/* end of slice/frame, or error? */
						{
							goto finish_idec;
						}
					}
				}

				REMOVEBITS(mba->len);
				mbaCount += mba->mba;

				if (mbaCount)
				{
					decoder.dc_dct_pred[0] =
						decoder.dc_dct_pred[1] =
						decoder.dc_dct_pred[2] = 128 << decoder.intra_dc_precision;
				}
				[[fallthrough]];

			case 4:
				if (!GETWORD())
				{
					ipu_cmd.pos[1] = 4;
					return false;
				}
				break;

				jNO_DEFAULT;
			}

			ipu_cmd.pos[1] = 0;
			ipu_cmd.pos[2] = 0;
		}

	finish_idec:
		finishmpeg2sliceIDEC();
		[[fallthrough]];

	case 3:
		{
			u8 bit8;
			if (!getBits8((u8*)&bit8, 0))
			{
				ipu_cmd.pos[0] = 3;
				return false;
			}

			if (bit8 == 0)
			{
				g_BP.Align();
				ipuRegs.ctrl.SCD = 1;
			}
		}
		[[fallthrough]];

	case 4:
		if (!getBits32((u8*)&ipuRegs.top, 0))
		{
			ipu_cmd.pos[0] = 4;
			return false;
		}

		ipuRegs.top = BigEndian(ipuRegs.top);
		break;

		jNO_DEFAULT;
	}

	return true;
}

__fi bool mpeg2_slice()
{
	int DCT_offset, DCT_stride;

	macroblock_8& mb8 = decoder.mb8;
	macroblock_16& mb16 = decoder.mb16;

	switch (ipu_cmd.pos[0])
	{
	case 0:
		if (decoder.dcr)
		{
			decoder.dc_dct_pred[0] =
				decoder.dc_dct_pred[1] =
				decoder.dc_dct_pred[2] = 128 << decoder.intra_dc_precision;
		}

		ipuRegs.ctrl.ECD = 0;
		ipuRegs.top = 0;
		memzero_sse_a(mb8);
		memzero_sse_a(mb16);
		[[fallthrough]];

	case 1:
		if (!bitstream_init())
		{
			ipu_cmd.pos[0] = 1;
			return false;
		}
		[[fallthrough]];

	case 2:
		ipu_cmd.pos[0] = 2;

		if (decoder.macroblock_modes & DCT_TYPE_INTERLACED)
		{
			DCT_offset = decoder_stride;
			DCT_stride = decoder_stride * 2;
		}
		else
		{
			DCT_offset = decoder_stride * 8;
			DCT_stride = decoder_stride;
		}

		if (decoder.macroblock_modes & MACROBLOCK_INTRA)
		{
			switch (ipu_cmd.pos[1])
			{
			case 0:
				decoder.coded_block_pattern = 0x3F;
				[[fallthrough]];

			case 1:
				if (!slice_intra_DCT(0, (u8*)mb8.Y, DCT_stride, ipu_cmd.pos[1] == 1))
				{
					ipu_cmd.pos[1] = 1;
					return false;
				}
				[[fallthrough]];

			case 2:
				if (!slice_intra_DCT(0, (u8*)mb8.Y + 8, DCT_stride, ipu_cmd.pos[1] == 2))
				{
					ipu_cmd.pos[1] = 2;
					return false;
				}
				[[fallthrough]];

			case 3:
				if (!slice_intra_DCT(0, (u8*)mb8.Y + DCT_offset, DCT_stride, ipu_cmd.pos[1] == 3))
				{
					ipu_cmd.pos[1] = 3;
					return false;
				}
				[[fallthrough]];

			case 4:
				if (!slice_intra_DCT(0, (u8*)mb8.Y + DCT_offset + 8, DCT_stride, ipu_cmd.pos[1] == 4))
				{
					ipu_cmd.pos[1] = 4;
					return false;
				}
				[[fallthrough]];

			case 5:
				if (!slice_intra_DCT(1, (u8*)mb8.Cb, decoder_stride >> 1, ipu_cmd.pos[1] == 5))
				{
					ipu_cmd.pos[1] = 5;
					return false;
				}
				[[fallthrough]];

			case 6:
				if (!slice_intra_DCT(2, (u8*)mb8.Cr, decoder_stride >> 1, ipu_cmd.pos[1] == 6))
				{
					ipu_cmd.pos[1] = 6;
					return false;
				}
				break;

				jNO_DEFAULT;
			}

			// Copy macroblock8 to macroblock16 - without sign extension.
			// Manually inlined due to MSVC refusing to inline the SSE-optimized version.
			{
				const u8* s = (const u8*)&mb8;
				u16* d = (u16*)&mb16;

				//Y  bias	- 16 * 16
				//Cr bias	- 8 * 8
				//Cb bias	- 8 * 8

#if defined(_M_X86_32) || defined(_M_X86_64)

				__m128i zeroreg = _mm_setzero_si128();

				for (uint i = 0; i < (256 + 64 + 64) / 32; ++i)
				{
					//*d++ = *s++;
					__m128i woot1 = _mm_load_si128((__m128i*)s);
					__m128i woot2 = _mm_load_si128((__m128i*)s + 1);
					_mm_store_si128((__m128i*)d, _mm_unpacklo_epi8(woot1, zeroreg));
					_mm_store_si128((__m128i*)d + 1, _mm_unpackhi_epi8(woot1, zeroreg));
					_mm_store_si128((__m128i*)d + 2, _mm_unpacklo_epi8(woot2, zeroreg));
					_mm_store_si128((__m128i*)d + 3, _mm_unpackhi_epi8(woot2, zeroreg));
					s += 32;
					d += 32;
				}

#elif defined(_M_ARM64)

				uint8x16_t zeroreg = vmovq_n_u8(0);

				for (uint i = 0; i < (256 + 64 + 64) / 32; ++i)
				{
					//*d++ = *s++;
					uint8x16_t woot1 = vld1q_u8((uint8_t*)s);
					uint8x16_t woot2 = vld1q_u8((uint8_t*)s + 16);
					vst1q_u8((uint8_t*)d, vzip1q_u8(woot1, zeroreg));
					vst1q_u8((uint8_t*)d + 16, vzip2q_u8(woot1, zeroreg));
					vst1q_u8((uint8_t*)d + 32, vzip1q_u8(woot2, zeroreg));
					vst1q_u8((uint8_t*)d + 48, vzip2q_u8(woot2, zeroreg));
					s += 32;
					d += 32;
				}

#else
#error Unsupported arch
#endif
			}
		}
		else
		{
			if (decoder.macroblock_modes & MACROBLOCK_PATTERN)
			{
				switch (ipu_cmd.pos[1])
				{
				case 0:
					decoder.coded_block_pattern = get_coded_block_pattern();  // max 9bits
					[[fallthrough]];

				case 1:
					if (decoder.coded_block_pattern & 0x20)
					{
						if (!slice_non_intra_DCT((s16*)mb16.Y, DCT_stride, ipu_cmd.pos[1] == 1))
						{
							ipu_cmd.pos[1] = 1;
							return false;
						}
					}
					[[fallthrough]];

				case 2:
					if (decoder.coded_block_pattern & 0x10)
					{
						if (!slice_non_intra_DCT((s16*)mb16.Y + 8, DCT_stride, ipu_cmd.pos[1] == 2))
						{
							ipu_cmd.pos[1] = 2;
							return false;
						}
					}
					[[fallthrough]];

				case 3:
					if (decoder.coded_block_pattern & 0x08)
					{
						if (!slice_non_intra_DCT((s16*)mb16.Y + DCT_offset, DCT_stride, ipu_cmd.pos[1] == 3))
						{
							ipu_cmd.pos[1] = 3;
							return false;
						}
					}
					[[fallthrough]];

				case 4:
					if (decoder.coded_block_pattern & 0x04)
					{
						if (!slice_non_intra_DCT((s16*)mb16.Y + DCT_offset + 8, DCT_stride, ipu_cmd.pos[1] == 4))
						{
							ipu_cmd.pos[1] = 4;
							return false;
						}
					}
					[[fallthrough]];

				case 5:
					if (decoder.coded_block_pattern & 0x2)
					{
						if (!slice_non_intra_DCT((s16*)mb16.Cb, decoder_stride >> 1, ipu_cmd.pos[1] == 5))
						{
							ipu_cmd.pos[1] = 5;
							return false;
						}
					}
					[[fallthrough]];

				case 6:
					if (decoder.coded_block_pattern & 0x1)
					{
						if (!slice_non_intra_DCT((s16*)mb16.Cr, decoder_stride >> 1, ipu_cmd.pos[1] == 6))
						{
							ipu_cmd.pos[1] = 6;
							return false;
						}
					}
					break;

					jNO_DEFAULT;
				}
			}
		}

		// Send The MacroBlock via DmaIpuFrom
		ipuRegs.ctrl.SCD = 0;
		coded_block_pattern = decoder.coded_block_pattern;

		decoder.SetOutputTo(mb16);
		[[fallthrough]];

	case 3:
		{
			pxAssert(decoder.ipu0_data > 0);

			uint read = ipu_fifo.out.write((u32*)decoder.GetIpuDataPtr(), decoder.ipu0_data);
			decoder.AdvanceIpuDataBy(read);

			if (decoder.ipu0_data != 0)
			{
				// IPU FIFO filled up -- Will have to finish transferring later.
				ipu_cmd.pos[0] = 3;
				return false;
			}

			mbaCount = 0;
		}
		[[fallthrough]];

	case 4:
		{
			u8 bit8;
			if (!getBits8((u8*)&bit8, 0))
			{
				ipu_cmd.pos[0] = 4;
				return false;
			}

			if (bit8 == 0)
			{
				g_BP.Align();
				ipuRegs.ctrl.SCD = 1;
			}
		}
		[[fallthrough]];

	case 5:
		if (!getBits32((u8*)&ipuRegs.top, 0))
		{
			ipu_cmd.pos[0] = 5;
			return false;
		}

		ipuRegs.top = BigEndian(ipuRegs.top);
		break;
	}

	return true;
}


#define W1 2841 /* 2048*sqrt (2)*cos (1*pi/16) */
#define W2 2676 /* 2048*sqrt (2)*cos (2*pi/16) */
#define W3 2408 /* 2048*sqrt (2)*cos (3*pi/16) */
#define W5 1609 /* 2048*sqrt (2)*cos (5*pi/16) */
#define W6 1108 /* 2048*sqrt (2)*cos (6*pi/16) */
#define W7 565 /* 2048*sqrt (2)*cos (7*pi/16) */

/*
 * In legal streams, the IDCT output should be between -384 and +384.
 * In corrupted streams, it is possible to force the IDCT output to go
 * to +-3826 - this is the worst case for a column IDCT where the
 * column inputs are 16-bit values.
 */
static constexpr std::array<u8, 1024> ComputeClipLUT()
{
	std::array<u8, 1024> ret = {};
	for (int i = -384; i < 640; i++)
		ret[i + 384] = (i < 0) ? 0 : ((i > 255) ? 255 : i);
	return ret;
}
static constexpr __aligned16 std::array<u8, 1024> clip_lut = ComputeClipLUT();

static __fi void BUTTERFLY(int& t0, int& t1, int w0, int w1, int d0, int d1)
{
#if 0
    t0 = w0*d0 + w1*d1;
    t1 = w0*d1 - w1*d0;
#else
	int tmp = w0 * (d0 + d1);
	t0 = tmp + (w1 - w0) * d1;
	t1 = tmp - (w1 + w0) * d0;
#endif
}

__ri void mpeg2_idct(s16* block)
{
	// TODO-some-time: SIMD-ify this
	for (int i = 0; i < 8; i++)
	{
		s16* const rblock = block + 8 * i;
		if (!(rblock[1] | ((s32*)rblock)[1] | ((s32*)rblock)[2] |
				((s32*)rblock)[3]))
		{
			u32 tmp = (u16)(rblock[0] << 3);
			tmp |= tmp << 16;
			((s32*)rblock)[0] = tmp;
			((s32*)rblock)[1] = tmp;
			((s32*)rblock)[2] = tmp;
			((s32*)rblock)[3] = tmp;
			continue;
		}

		int a0, a1, a2, a3;
		{
			const int d0 = (rblock[0] << 11) + 128;
			const int d1 = rblock[1];
			const int d2 = rblock[2] << 11;
			const int d3 = rblock[3];
			int t0 = d0 + d2;
			int t1 = d0 - d2;
			int t2, t3;
			BUTTERFLY(t2, t3, W6, W2, d3, d1);
			a0 = t0 + t2;
			a1 = t1 + t3;
			a2 = t1 - t3;
			a3 = t0 - t2;
		}

		int b0, b1, b2, b3;
		{
			const int d0 = rblock[4];
			const int d1 = rblock[5];
			const int d2 = rblock[6];
			const int d3 = rblock[7];
			int t0, t1, t2, t3;
			BUTTERFLY(t0, t1, W7, W1, d3, d0);
			BUTTERFLY(t2, t3, W3, W5, d1, d2);
			b0 = t0 + t2;
			b3 = t1 + t3;
			t0 -= t2;
			t1 -= t3;
			b1 = ((t0 + t1) * 181) >> 8;
			b2 = ((t0 - t1) * 181) >> 8;
		}

		rblock[0] = (a0 + b0) >> 8;
		rblock[1] = (a1 + b1) >> 8;
		rblock[2] = (a2 + b2) >> 8;
		rblock[3] = (a3 + b3) >> 8;
		rblock[4] = (a3 - b3) >> 8;
		rblock[5] = (a2 - b2) >> 8;
		rblock[6] = (a1 - b1) >> 8;
		rblock[7] = (a0 - b0) >> 8;
	}

	for (int i = 0; i < 8; i++)
	{
		s16* const cblock = block + i;

		int a0, a1, a2, a3;
		{
			const int d0 = (cblock[8 * 0] << 11) + 65536;
			const int d1 = cblock[8 * 1];
			const int d2 = cblock[8 * 2] << 11;
			const int d3 = cblock[8 * 3];
			const int t0 = d0 + d2;
			const int t1 = d0 - d2;
			int t2;
			int t3;
			BUTTERFLY(t2, t3, W6, W2, d3, d1);
			a0 = t0 + t2;
			a1 = t1 + t3;
			a2 = t1 - t3;
			a3 = t0 - t2;
		}

		int b0, b1, b2, b3;
		{
			const int d0 = cblock[8 * 4];
			const int d1 = cblock[8 * 5];
			const int d2 = cblock[8 * 6];
			const int d3 = cblock[8 * 7];
			int t0, t1, t2, t3;
			BUTTERFLY(t0, t1, W7, W1, d3, d0);
			BUTTERFLY(t2, t3, W3, W5, d1, d2);
			b0 = t0 + t2;
			b3 = t1 + t3;
			t0 = (t0 - t2) >> 8;
			t1 = (t1 - t3) >> 8;
			b1 = (t0 + t1) * 181;
			b2 = (t0 - t1) * 181;
		}

		cblock[8 * 0] = (a0 + b0) >> 17;
		cblock[8 * 1] = (a1 + b1) >> 17;
		cblock[8 * 2] = (a2 + b2) >> 17;
		cblock[8 * 3] = (a3 + b3) >> 17;
		cblock[8 * 4] = (a3 - b3) >> 17;
		cblock[8 * 5] = (a2 - b2) >> 17;
		cblock[8 * 6] = (a1 - b1) >> 17;
		cblock[8 * 7] = (a0 - b0) >> 17;
	}
}

__ri void mpeg2_idct_copy(s16* block, u8* dest, const int stride)
{
	mpeg2_idct(block);

	for (int i = 0; i < 8; i++)
	{
		dest[0] = (clip_lut.data() + 384)[block[0]];
		dest[1] = (clip_lut.data() + 384)[block[1]];
		dest[2] = (clip_lut.data() + 384)[block[2]];
		dest[3] = (clip_lut.data() + 384)[block[3]];
		dest[4] = (clip_lut.data() + 384)[block[4]];
		dest[5] = (clip_lut.data() + 384)[block[5]];
		dest[6] = (clip_lut.data() + 384)[block[6]];
		dest[7] = (clip_lut.data() + 384)[block[7]];

		std::memset(block, 0, 16);

		dest += stride;
		block += 8;
	}
}


// stride = increment for dest in 16-bit units (typically either 8 [128 bits] or 16 [256 bits]).
__ri void mpeg2_idct_add(const int last, s16* block, s16* dest, const int stride)
{
	// on the IPU, stride is always assured to be multiples of QWC (bottom 3 bits are 0).

	if (last != 129 || (block[0] & 7) == 4)
	{
		mpeg2_idct(block);

#if defined(_M_X86_32) || defined(_M_X86_64)
		__m128 zero = _mm_setzero_ps();
#elif defined(_M_ARM64)
		int16x8_t zero = vdupq_n_s16(0);
#endif
		for (int i = 0; i < 8; i++)
		{
#if defined(_M_X86_32) || defined(_M_X86_64)
			_mm_store_ps((float*)dest, _mm_load_ps((float*)block));
			_mm_store_ps((float*)block, zero);
#elif defined(_M_ARM64)
			vst1q_s16(dest, vld1q_s16(block));
			vst1q_s16(block, zero);
#else
			std::memcpy(dest, block, 16);
			std::memset(block, 0, 16);
#endif

			dest += stride;
			block += 8;
		}
	}
	else
	{
		s16 DC = ((int)block[0] + 4) >> 3;
		s16 dcf[2] = {DC, DC};
		block[0] = block[63] = 0;

#if defined(_M_X86_32) || defined(_M_X86_64)
		__m128 dc128 = _mm_set_ps1(*(float*)dcf);

		for (int i = 0; i < 8; ++i)
			_mm_store_ps((float*)(dest + (stride * i)), dc128);
#elif defined(_M_ARM64)
		float32x4_t dc128 = vld1q_dup_f32((float*)dcf);

		for (int i = 0; i < 8; ++i)
			vst1q_f32((float*)(dest + (stride * i)), dc128);
#endif
	}
}


void ipu_dither_reference(const macroblock_rgb32& rgb32, macroblock_rgb16& rgb16, int dte);

#if defined(_M_X86_32) || defined(_M_X86_64)
void ipu_dither_sse2(const macroblock_rgb32& rgb32, macroblock_rgb16& rgb16, int dte);
#endif

__ri void ipu_dither(const macroblock_rgb32& rgb32, macroblock_rgb16& rgb16, int dte)
{
#if defined(_M_X86_32) || defined(_M_X86_64)
	ipu_dither_sse2(rgb32, rgb16, dte);
#else
	ipu_dither_reference(rgb32, rgb16, dte);
#endif
}

__ri void ipu_dither_reference(const macroblock_rgb32& rgb32, macroblock_rgb16& rgb16, int dte)
{
	if (dte) {
		// I'm guessing values are rounded down when clamping.
		const int dither_coefficient[4][4] = {
				{-4, 0, -3, 1},
				{2, -2, 3, -1},
				{-3, 1, -4, 0},
				{3, -1, 2, -2},
		};
		for (int i = 0; i < 16; ++i) {
			for (int j = 0; j < 16; ++j) {
				const int dither = dither_coefficient[i & 3][j & 3];
				const int r = std::max(0, std::min(rgb32.c[i][j].r + dither, 255));
				const int g = std::max(0, std::min(rgb32.c[i][j].g + dither, 255));
				const int b = std::max(0, std::min(rgb32.c[i][j].b + dither, 255));

				rgb16.c[i][j].r = r >> 3;
				rgb16.c[i][j].g = g >> 3;
				rgb16.c[i][j].b = b >> 3;
				rgb16.c[i][j].a = rgb32.c[i][j].a == 0x40;
			}
		}
	}
	else {
		for (int i = 0; i < 16; ++i) {
			for (int j = 0; j < 16; ++j) {
				rgb16.c[i][j].r = rgb32.c[i][j].r >> 3;
				rgb16.c[i][j].g = rgb32.c[i][j].g >> 3;
				rgb16.c[i][j].b = rgb32.c[i][j].b >> 3;
				rgb16.c[i][j].a = rgb32.c[i][j].a == 0x40;
			}
		}
	}
}

#if defined(_M_X86_32) || defined(_M_X86_64)

__ri void ipu_dither_sse2(const macroblock_rgb32& rgb32, macroblock_rgb16& rgb16, int dte)
{
	const __m128i alpha_test = _mm_set1_epi16(0x40);
	const __m128i dither_add_matrix[] = {
			_mm_setr_epi32(0x00000000, 0x00000000, 0x00000000, 0x00010101),
			_mm_setr_epi32(0x00020202, 0x00000000, 0x00030303, 0x00000000),
			_mm_setr_epi32(0x00000000, 0x00010101, 0x00000000, 0x00000000),
			_mm_setr_epi32(0x00030303, 0x00000000, 0x00020202, 0x00000000),
	};
	const __m128i dither_sub_matrix[] = {
			_mm_setr_epi32(0x00040404, 0x00000000, 0x00030303, 0x00000000),
			_mm_setr_epi32(0x00000000, 0x00020202, 0x00000000, 0x00010101),
			_mm_setr_epi32(0x00030303, 0x00000000, 0x00040404, 0x00000000),
			_mm_setr_epi32(0x00000000, 0x00010101, 0x00000000, 0x00020202),
	};
	for (int i = 0; i < 16; ++i) {
		const __m128i dither_add = dither_add_matrix[i & 3];
		const __m128i dither_sub = dither_sub_matrix[i & 3];
		for (int n = 0; n < 2; ++n) {
			__m128i rgba_8_0123 = _mm_load_si128(reinterpret_cast<const __m128i*>(&rgb32.c[i][n * 8]));
			__m128i rgba_8_4567 = _mm_load_si128(reinterpret_cast<const __m128i*>(&rgb32.c[i][n * 8 + 4]));

			// Dither and clamp
			if (dte) {
				rgba_8_0123 = _mm_adds_epu8(rgba_8_0123, dither_add);
				rgba_8_0123 = _mm_subs_epu8(rgba_8_0123, dither_sub);
				rgba_8_4567 = _mm_adds_epu8(rgba_8_4567, dither_add);
				rgba_8_4567 = _mm_subs_epu8(rgba_8_4567, dither_sub);
			}

			// Split into channel components and extend to 16 bits
			const __m128i rgba_16_0415 = _mm_unpacklo_epi8(rgba_8_0123, rgba_8_4567);
			const __m128i rgba_16_2637 = _mm_unpackhi_epi8(rgba_8_0123, rgba_8_4567);
			const __m128i rgba_32_0246 = _mm_unpacklo_epi8(rgba_16_0415, rgba_16_2637);
			const __m128i rgba_32_1357 = _mm_unpackhi_epi8(rgba_16_0415, rgba_16_2637);
			const __m128i rg_64_01234567 = _mm_unpacklo_epi8(rgba_32_0246, rgba_32_1357);
			const __m128i ba_64_01234567 = _mm_unpackhi_epi8(rgba_32_0246, rgba_32_1357);

			const __m128i zero = _mm_setzero_si128();
			__m128i r = _mm_unpacklo_epi8(rg_64_01234567, zero);
			__m128i g = _mm_unpackhi_epi8(rg_64_01234567, zero);
			__m128i b = _mm_unpacklo_epi8(ba_64_01234567, zero);
			__m128i a = _mm_unpackhi_epi8(ba_64_01234567, zero);

			// Create RGBA
			r = _mm_srli_epi16(r, 3);
			g = _mm_slli_epi16(_mm_srli_epi16(g, 3), 5);
			b = _mm_slli_epi16(_mm_srli_epi16(b, 3), 10);
			a = _mm_slli_epi16(_mm_cmpeq_epi16(a, alpha_test), 15);

			const __m128i rgba16 = _mm_or_si128(_mm_or_si128(r, g), _mm_or_si128(b, a));

			_mm_store_si128(reinterpret_cast<__m128i*>(&rgb16.c[i][n * 8]), rgba16);
		}
	}
}

#endif


// The IPU's colour space conversion conforms to ITU-R Recommendation BT.601 if anyone wants to make a
// faster or "more accurate" implementation, but this is the precise documented integer method used by
// the hardware and is fast enough with SSE2.

#define IPU_Y_BIAS    16
#define IPU_C_BIAS    128
#define IPU_Y_COEFF   0x95	//  1.1640625
#define IPU_GCR_COEFF (-0x68)	// -0.8125
#define IPU_GCB_COEFF (-0x32)	// -0.390625
#define IPU_RCR_COEFF 0xcc	//  1.59375
#define IPU_BCB_COEFF 0x102	//  2.015625

// conforming implementation for reference, do not optimise
void yuv2rgb_reference(void)
{
	const macroblock_8& mb8 = decoder.mb8;
	macroblock_rgb32& rgb32 = decoder.rgb32;

	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++)
		{
			s32 lum = (IPU_Y_COEFF * (std::max(0, (s32)mb8.Y[y][x] - IPU_Y_BIAS))) >> 6;
			s32 rcr = (IPU_RCR_COEFF * ((s32)mb8.Cr[y >> 1][x >> 1] - 128)) >> 6;
			s32 gcr = (IPU_GCR_COEFF * ((s32)mb8.Cr[y >> 1][x >> 1] - 128)) >> 6;
			s32 gcb = (IPU_GCB_COEFF * ((s32)mb8.Cb[y >> 1][x >> 1] - 128)) >> 6;
			s32 bcb = (IPU_BCB_COEFF * ((s32)mb8.Cb[y >> 1][x >> 1] - 128)) >> 6;

			rgb32.c[y][x].r = std::max(0, std::min(255, (lum + rcr + 1) >> 1));
			rgb32.c[y][x].g = std::max(0, std::min(255, (lum + gcr + gcb + 1) >> 1));
			rgb32.c[y][x].b = std::max(0, std::min(255, (lum + bcb + 1) >> 1));
			rgb32.c[y][x].a = 0x80; // the norm to save doing this on the alpha pass
		}
}

#if defined(_M_X86_32) || defined(_M_X86_64)

// Suikoden Tactics FMV speed results: Reference - ~72fps, SSE2 - ~120fps
// An AVX2 version is only slightly faster than an SSE2 version (+2-3fps)
// (or I'm a poor optimiser), though it might be worth attempting again
// once we've ported to 64 bits (the extra registers should help).
__ri void yuv2rgb_sse2()
{
	const __m128i c_bias = _mm_set1_epi8(s8(IPU_C_BIAS));
	const __m128i y_bias = _mm_set1_epi8(IPU_Y_BIAS);
	const __m128i y_mask = _mm_set1_epi16(s16(0xFF00));
	// Specifying round off instead of round down as everywhere else
	// implies that this is right
	const __m128i round_1bit = _mm_set1_epi16(0x0001);;

	const __m128i y_coefficient = _mm_set1_epi16(s16(IPU_Y_COEFF << 2));
	const __m128i gcr_coefficient = _mm_set1_epi16(s16(u16(IPU_GCR_COEFF) << 2));
	const __m128i gcb_coefficient = _mm_set1_epi16(s16(u16(IPU_GCB_COEFF) << 2));
	const __m128i rcr_coefficient = _mm_set1_epi16(s16(IPU_RCR_COEFF << 2));
	const __m128i bcb_coefficient = _mm_set1_epi16(s16(IPU_BCB_COEFF << 2));

	// Alpha set to 0x80 here. The threshold stuff is done later.
	const __m128i& alpha = c_bias;

	for (int n = 0; n < 8; ++n) {
		// could skip the loadl_epi64 but most SSE instructions require 128-bit
		// alignment so two versions would be needed.
		__m128i cb = _mm_loadl_epi64(reinterpret_cast<__m128i*>(&decoder.mb8.Cb[n][0]));
		__m128i cr = _mm_loadl_epi64(reinterpret_cast<__m128i*>(&decoder.mb8.Cr[n][0]));

		// (Cb - 128) << 8, (Cr - 128) << 8
		cb = _mm_xor_si128(cb, c_bias);
		cr = _mm_xor_si128(cr, c_bias);
		cb = _mm_unpacklo_epi8(_mm_setzero_si128(), cb);
		cr = _mm_unpacklo_epi8(_mm_setzero_si128(), cr);

		__m128i rc = _mm_mulhi_epi16(cr, rcr_coefficient);
		__m128i gc = _mm_adds_epi16(_mm_mulhi_epi16(cr, gcr_coefficient), _mm_mulhi_epi16(cb, gcb_coefficient));
		__m128i bc = _mm_mulhi_epi16(cb, bcb_coefficient);

		for (int m = 0; m < 2; ++m) {
			__m128i y = _mm_load_si128(reinterpret_cast<__m128i*>(&decoder.mb8.Y[n * 2 + m][0]));
			y = _mm_subs_epu8(y, y_bias);
			// Y << 8 for pixels 0, 2, 4, 6, 8, 10, 12, 14
			__m128i y_even = _mm_slli_epi16(y, 8);
			// Y << 8 for pixels 1, 3, 5, 7 ,9, 11, 13, 15
			__m128i y_odd = _mm_and_si128(y, y_mask);

			y_even = _mm_mulhi_epu16(y_even, y_coefficient);
			y_odd = _mm_mulhi_epu16(y_odd, y_coefficient);

			__m128i r_even = _mm_adds_epi16(rc, y_even);
			__m128i r_odd = _mm_adds_epi16(rc, y_odd);
			__m128i g_even = _mm_adds_epi16(gc, y_even);
			__m128i g_odd = _mm_adds_epi16(gc, y_odd);
			__m128i b_even = _mm_adds_epi16(bc, y_even);
			__m128i b_odd = _mm_adds_epi16(bc, y_odd);

			// round
			r_even = _mm_srai_epi16(_mm_add_epi16(r_even, round_1bit), 1);
			r_odd = _mm_srai_epi16(_mm_add_epi16(r_odd, round_1bit), 1);
			g_even = _mm_srai_epi16(_mm_add_epi16(g_even, round_1bit), 1);
			g_odd = _mm_srai_epi16(_mm_add_epi16(g_odd, round_1bit), 1);
			b_even = _mm_srai_epi16(_mm_add_epi16(b_even, round_1bit), 1);
			b_odd = _mm_srai_epi16(_mm_add_epi16(b_odd, round_1bit), 1);

			// combine even and odd bytes in original order
			__m128i r = _mm_packus_epi16(r_even, r_odd);
			__m128i g = _mm_packus_epi16(g_even, g_odd);
			__m128i b = _mm_packus_epi16(b_even, b_odd);

			r = _mm_unpacklo_epi8(r, _mm_shuffle_epi32(r, _MM_SHUFFLE(3, 2, 3, 2)));
			g = _mm_unpacklo_epi8(g, _mm_shuffle_epi32(g, _MM_SHUFFLE(3, 2, 3, 2)));
			b = _mm_unpacklo_epi8(b, _mm_shuffle_epi32(b, _MM_SHUFFLE(3, 2, 3, 2)));

			// Create RGBA (we could generate A here, but we don't) quads
			__m128i rg_l = _mm_unpacklo_epi8(r, g);
			__m128i ba_l = _mm_unpacklo_epi8(b, alpha);
			__m128i rgba_ll = _mm_unpacklo_epi16(rg_l, ba_l);
			__m128i rgba_lh = _mm_unpackhi_epi16(rg_l, ba_l);

			__m128i rg_h = _mm_unpackhi_epi8(r, g);
			__m128i ba_h = _mm_unpackhi_epi8(b, alpha);
			__m128i rgba_hl = _mm_unpacklo_epi16(rg_h, ba_h);
			__m128i rgba_hh = _mm_unpackhi_epi16(rg_h, ba_h);

			_mm_store_si128(reinterpret_cast<__m128i*>(&decoder.rgb32.c[n * 2 + m][0]), rgba_ll);
			_mm_store_si128(reinterpret_cast<__m128i*>(&decoder.rgb32.c[n * 2 + m][4]), rgba_lh);
			_mm_store_si128(reinterpret_cast<__m128i*>(&decoder.rgb32.c[n * 2 + m][8]), rgba_hl);
			_mm_store_si128(reinterpret_cast<__m128i*>(&decoder.rgb32.c[n * 2 + m][12]), rgba_hh);
		}
	}
}

#endif

#ifdef _M_ARM64

#define MULHI16(a, b) vshrq_n_s16(vqdmulhq_s16((a), (b)), 1)

__ri void yuv2rgb_neon()
{
	const int8x16_t c_bias = vdupq_n_s8(s8(IPU_C_BIAS));
	const uint8x16_t y_bias = vdupq_n_u8(IPU_Y_BIAS);
	const int16x8_t y_mask = vdupq_n_s16(s16(0xFF00));
	// Specifying round off instead of round down as everywhere else
	// implies that this is right
	const int16x8_t round_1bit = vdupq_n_s16(0x0001);
	;

	const int16x8_t y_coefficient = vdupq_n_s16(s16(IPU_Y_COEFF << 2));
	const int16x8_t gcr_coefficient = vdupq_n_s16(s16(u16(IPU_GCR_COEFF) << 2));
	const int16x8_t gcb_coefficient = vdupq_n_s16(s16(u16(IPU_GCB_COEFF) << 2));
	const int16x8_t rcr_coefficient = vdupq_n_s16(s16(IPU_RCR_COEFF << 2));
	const int16x8_t bcb_coefficient = vdupq_n_s16(s16(IPU_BCB_COEFF << 2));

	// Alpha set to 0x80 here. The threshold stuff is done later.
	const uint8x16_t alpha = vreinterpretq_u8_s8(c_bias);

	for (int n = 0; n < 8; ++n)
	{
		// could skip the loadl_epi64 but most SSE instructions require 128-bit
		// alignment so two versions would be needed.
		int8x16_t cb = vcombine_s8(vld1_s8(reinterpret_cast<s8*>(&decoder.mb8.Cb[n][0])), vdup_n_s8(0));
		int8x16_t cr = vcombine_s8(vld1_s8(reinterpret_cast<s8*>(&decoder.mb8.Cr[n][0])), vdup_n_s8(0));

		// (Cb - 128) << 8, (Cr - 128) << 8
		cb = veorq_s8(cb, c_bias);
		cr = veorq_s8(cr, c_bias);
		cb = vzip1q_s8(vdupq_n_s8(0), cb);
		cr = vzip1q_s8(vdupq_n_s8(0), cr);

		int16x8_t rc = MULHI16(vreinterpretq_s16_s8(cr), rcr_coefficient);
		int16x8_t gc = vqaddq_s16(MULHI16(vreinterpretq_s16_s8(cr), gcr_coefficient), MULHI16(vreinterpretq_s16_s8(cb), gcb_coefficient));
		int16x8_t bc = MULHI16(vreinterpretq_s16_s8(cb), bcb_coefficient);

		for (int m = 0; m < 2; ++m)
		{
			uint8x16_t y = vld1q_u8(&decoder.mb8.Y[n * 2 + m][0]);
			y = vqsubq_u8(y, y_bias);
			// Y << 8 for pixels 0, 2, 4, 6, 8, 10, 12, 14
			int16x8_t y_even = vshlq_n_s16(vreinterpretq_s16_u8(y), 8);
			// Y << 8 for pixels 1, 3, 5, 7 ,9, 11, 13, 15
			int16x8_t y_odd = vandq_s16(vreinterpretq_s16_u8(y), y_mask);

			// y_even = _mm_mulhi_epu16(y_even, y_coefficient);
			// y_odd = _mm_mulhi_epu16(y_odd, y_coefficient);

			uint16x4_t a3210 = vget_low_u16(vreinterpretq_u16_s16(y_even));
			uint16x4_t b3210 = vget_low_u16(vreinterpretq_u16_s16(y_coefficient));
			uint32x4_t ab3210 = vmull_u16(a3210, b3210);
			uint32x4_t ab7654 = vmull_high_u16(vreinterpretq_u16_s16(y_even), vreinterpretq_u16_s16(y_coefficient));
			y_even = vreinterpretq_s16_u16(vuzp2q_u16(vreinterpretq_u16_u32(ab3210), vreinterpretq_u16_u32(ab7654)));

			a3210 = vget_low_u16(vreinterpretq_u16_s16(y_odd));
			b3210 = vget_low_u16(vreinterpretq_u16_s16(y_coefficient));
			ab3210 = vmull_u16(a3210, b3210);
			ab7654 = vmull_high_u16(vreinterpretq_u16_s16(y_odd), vreinterpretq_u16_s16(y_coefficient));
			y_odd = vreinterpretq_s16_u16(vuzp2q_u16(vreinterpretq_u16_u32(ab3210), vreinterpretq_u16_u32(ab7654)));

			int16x8_t r_even = vqaddq_s16(rc, y_even);
			int16x8_t r_odd = vqaddq_s16(rc, y_odd);
			int16x8_t g_even = vqaddq_s16(gc, y_even);
			int16x8_t g_odd = vqaddq_s16(gc, y_odd);
			int16x8_t b_even = vqaddq_s16(bc, y_even);
			int16x8_t b_odd = vqaddq_s16(bc, y_odd);

			// round
			r_even = vshrq_n_s16(vaddq_s16(r_even, round_1bit), 1);
			r_odd = vshrq_n_s16(vaddq_s16(r_odd, round_1bit), 1);
			g_even = vshrq_n_s16(vaddq_s16(g_even, round_1bit), 1);
			g_odd = vshrq_n_s16(vaddq_s16(g_odd, round_1bit), 1);
			b_even = vshrq_n_s16(vaddq_s16(b_even, round_1bit), 1);
			b_odd = vshrq_n_s16(vaddq_s16(b_odd, round_1bit), 1);

			// combine even and odd bytes in original order
			uint8x16_t r = vcombine_u8(vqmovun_s16(r_even), vqmovun_s16(r_odd));
			uint8x16_t g = vcombine_u8(vqmovun_s16(g_even), vqmovun_s16(g_odd));
			uint8x16_t b = vcombine_u8(vqmovun_s16(b_even), vqmovun_s16(b_odd));

			r = vzip1q_u8(r, vreinterpretq_u8_u64(vdupq_laneq_u64(vreinterpretq_u64_u8(r), 1)));
			g = vzip1q_u8(g, vreinterpretq_u8_u64(vdupq_laneq_u64(vreinterpretq_u64_u8(g), 1)));
			b = vzip1q_u8(b, vreinterpretq_u8_u64(vdupq_laneq_u64(vreinterpretq_u64_u8(b), 1)));

			// Create RGBA (we could generate A here, but we don't) quads
			uint8x16_t rg_l = vzip1q_u8(r, g);
			uint8x16_t ba_l = vzip1q_u8(b, alpha);
			uint16x8_t rgba_ll = vzip1q_u16(vreinterpretq_u16_u8(rg_l), vreinterpretq_u16_u8(ba_l));
			uint16x8_t rgba_lh = vzip2q_u16(vreinterpretq_u16_u8(rg_l), vreinterpretq_u16_u8(ba_l));

			uint8x16_t rg_h = vzip2q_u8(r, g);
			uint8x16_t ba_h = vzip2q_u8(b, alpha);
			uint16x8_t rgba_hl = vzip1q_u16(vreinterpretq_u16_u8(rg_h), vreinterpretq_u16_u8(ba_h));
			uint16x8_t rgba_hh = vzip2q_u16(vreinterpretq_u16_u8(rg_h), vreinterpretq_u16_u8(ba_h));

			vst1q_u8(reinterpret_cast<u8*>(&decoder.rgb32.c[n * 2 + m][0]), vreinterpretq_u8_u16(rgba_ll));
			vst1q_u8(reinterpret_cast<u8*>(&decoder.rgb32.c[n * 2 + m][4]), vreinterpretq_u8_u16(rgba_lh));
			vst1q_u8(reinterpret_cast<u8*>(&decoder.rgb32.c[n * 2 + m][8]), vreinterpretq_u8_u16(rgba_hl));
			vst1q_u8(reinterpret_cast<u8*>(&decoder.rgb32.c[n * 2 + m][12]), vreinterpretq_u8_u16(rgba_hh));
		}
	}
}

#undef MULHI16

#endif