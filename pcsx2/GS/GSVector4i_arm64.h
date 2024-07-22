/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2021 PCSX2 Dev Team
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

#include "GS_types.h"
#include "neon_permute.h"

class alignas(16) GSVector4i
{
	static const GSVector4i m_xff[17];
	static const GSVector4i m_x0f[17];

	constexpr static int32x4_t cxpr_setr_epi32(int x, int y, int z, int w)
	{
#ifndef _MSC_VER
		const int32x4_t v{ (int32_t)x, (int32_t)y, (int32_t)z, (int32_t)w };
#else
		int32x4_t v = {};
		v.n128_i32[0] = x;
		v.n128_i32[1] = y;
		v.n128_i32[2] = z;
		v.n128_i32[3] = w;
#endif
		return (v);
	}
	constexpr static int32x4_t cxpr_setr_epi8(char b0, char b1, char b2, char b3, char b4, char b5, char b6, char b7, char b8, char b9, char b10, char b11, char b12, char b13, char b14, char b15)
	{
#ifndef _MSC_VER
		const int8x16_t v{ (int8_t)b0, (int8_t)b1, (int8_t)b2, (int8_t)b3,
											 (int8_t)b4, (int8_t)b5, (int8_t)b6, (int8_t)b7,
											 (int8_t)b8, (int8_t)b9, (int8_t)b10, (int8_t)b11,
											 (int8_t)b12, (int8_t)b13, (int8_t)b14, (int8_t)b15 };
		return ((int32x4_t)v);
#else
		int32x4_t v = {};
		v.n128_i8[0] = b0;
		v.n128_i8[1] = b1;
		v.n128_i8[2] = b2;
		v.n128_i8[3] = b3;
		v.n128_i8[4] = b4;
		v.n128_i8[5] = b5;
		v.n128_i8[6] = b6;
		v.n128_i8[7] = b7;
		v.n128_i8[8] = b8;
		v.n128_i8[9] = b9;
		v.n128_i8[10] = b10;
		v.n128_i8[11] = b11;
		v.n128_i8[12] = b12;
		v.n128_i8[13] = b13;
		v.n128_i8[14] = b14;
		v.n128_i8[15] = b15;
		return (v);
#endif
	}

public:
	union
	{
		struct { int x, y, z, w; };
		struct { int r, g, b, a; };
		struct { int left, top, right, bottom; };
		int v[4];
		float f32[4];
		int8 i8[16];
		int16 i16[8];
		int32 i32[4];
		int64 i64[2];
		uint8 u8[16];
		uint16 u16[8];
		uint32 u32[4];
		uint64 u64[2];
		int32x4_t v4s;
	};

	__forceinline constexpr GSVector4i()
		: x(0), y(0), z(0), w(0)
	{
	}

	constexpr static GSVector4i cxpr(int x, int y, int z, int w)
	{
		return GSVector4i(cxpr_setr_epi32(x, y, z, w));
	}

	constexpr static GSVector4i cxpr(int x)
	{
		return GSVector4i(cxpr_setr_epi32(x, x, x, x));
	}

	__forceinline GSVector4i(int x, int y, int z, int w)
	{
		// 4 gprs

		// m = _mm_set_epi32(w, z, y, x);

		// 2 gprs

		GSVector4i xz = load(x).upl32(load(z));
		GSVector4i yw = load(y).upl32(load(w));

		*this = xz.upl32(yw);
	}

	__forceinline GSVector4i(int x, int y)
	{
		*this = load(x).upl32(load(y));
	}

	__forceinline GSVector4i(short s0, short s1, short s2, short s3, short s4, short s5, short s6, short s7)
	{
		//vreinterpretq_s16_s32(v4s) = _mm_set_epi16(s7, s6, s5, s4, s3, s2, s1, s0);
    const int16_t arr[8] = {s0, s1, s2, s3, s4, s5, s6, s7};
    v4s = vreinterpretq_s32_s16(vld1q_s16(arr));
	}

	constexpr GSVector4i(char b0, char b1, char b2, char b3, char b4, char b5, char b6, char b7, char b8, char b9, char b10, char b11, char b12, char b13, char b14, char b15)
		: v4s(cxpr_setr_epi8(b0, b1, b2, b3, b4, b5, b6, b7, b8, b9, b10, b11, b12, b13, b14, b15))
	{
	}

	__forceinline GSVector4i(const GSVector4i& v)
	{
		v4s = v.v4s;
	}

	__forceinline explicit GSVector4i(const GSVector2i& v)
	{
		// vreinterpretq_s8_s32(v4s) = _mm_loadl_epi64((__m128i*)&v);
		v4s = vcombine_s32(vld1_s32(v.v), vcreate_s32(0));
	}

	// MSVC has bad codegen for the constexpr version when applied to non-constexpr things (https://godbolt.org/z/h8qbn7), so leave the non-constexpr version default
	__forceinline explicit GSVector4i(int i)
	{
		*this = i;
	}

	__forceinline constexpr explicit GSVector4i(int32x4_t m)
		: v4s(m)
	{
	}

	__forceinline explicit GSVector4i(const GSVector4& v, bool truncate = true);

	__forceinline static GSVector4i cast(const GSVector4& v);

	__forceinline void operator=(const GSVector4i& v)
	{
		v4s = v.v4s;
	}

	__forceinline void operator=(int i)
	{
		v4s = vdupq_n_s32(i);
	}

	__forceinline operator int32x4_t() const
	{
		return v4s;
	}

	// rect

	__forceinline int width() const
	{
		return right - left;
	}

	__forceinline int height() const
	{
		return bottom - top;
	}

	__forceinline GSVector4i rsize() const
	{
		return *this - xyxy(); // same as GSVector4i(0, 0, width(), height());
	}

	__forceinline unsigned int rarea() const
	{
		return width() * height();
	}

	__forceinline bool rempty() const
	{
		return (*this < zwzw()).mask() != 0x00ff;
	}

	__forceinline GSVector4i runion(const GSVector4i& a) const
	{
		int i = (upl64(a) < uph64(a)).mask();

		if (i == 0xffff)
		{
			return runion_ordered(a);
		}

		if ((i & 0x00ff) == 0x00ff)
		{
			return *this;
		}

		if ((i & 0xff00) == 0xff00)
		{
			return a;
		}

		return GSVector4i::zero();
	}

	__forceinline GSVector4i runion_ordered(const GSVector4i& a) const
	{
		return min_i32(a).upl64(max_i32(a).srl<8>());
	}

	__forceinline GSVector4i rintersect(const GSVector4i& a) const
	{
		return sat_i32(a);
	}

	template <Align_Mode mode>
	GSVector4i _ralign_helper(const GSVector4i& mask) const
	{
		GSVector4i v;

		switch (mode)
		{
		case Align_Inside:  v = *this + mask;        break;
		case Align_Outside: v = *this + mask.zwxy(); break;
		case Align_NegInf:  v = *this;               break;
		case Align_PosInf:  v = *this + mask.xyxy(); break;
		default: pxAssert(0); break;
		}

		return v.andnot(mask.xyxy());
	}

	/// Align the rect using mask values that already have one subtracted (1 << n - 1 aligns to 1 << n)
	template <Align_Mode mode>
	GSVector4i ralign_presub(const GSVector2i& a) const
	{
		return _ralign_helper<mode>(GSVector4i(a));
	}

	template <Align_Mode mode>
	GSVector4i ralign(const GSVector2i& a) const
	{
		// a must be 1 << n

		return _ralign_helper<mode>(GSVector4i(a) - GSVector4i(1, 1));
	}

	GSVector4i fit(int arx, int ary) const;

	GSVector4i fit(int preset) const;

#ifdef _WIN32

	__forceinline operator LPCRECT() const
	{
		return (LPCRECT)this;
	}

	__forceinline operator LPRECT()
	{
		return (LPRECT)this;
	}

#endif

	//

	__forceinline uint32 rgba32() const
	{
		GSVector4i v = *this;

		v = v.ps32(v);
		v = v.pu16(v);

		return (uint32)store(v);
	}

	__forceinline GSVector4i sat_i8(const GSVector4i& a, const GSVector4i& b) const
	{
		return max_i8(a).min_i8(b);
	}

	__forceinline GSVector4i sat_i8(const GSVector4i& a) const
	{
		return max_i8(a.xyxy()).min_i8(a.zwzw());
	}

	__forceinline GSVector4i sat_i16(const GSVector4i& a, const GSVector4i& b) const
	{
		return max_i16(a).min_i16(b);
	}

	__forceinline GSVector4i sat_i16(const GSVector4i& a) const
	{
		return max_i16(a.xyxy()).min_i16(a.zwzw());
	}

	__forceinline GSVector4i sat_i32(const GSVector4i& a, const GSVector4i& b) const
	{
		return max_i32(a).min_i32(b);
	}

	__forceinline GSVector4i sat_i32(const GSVector4i& a) const
	{
		return max_i32(a.xyxy()).min_i32(a.zwzw());
	}

	__forceinline GSVector4i sat_u8(const GSVector4i& a, const GSVector4i& b) const
	{
		return max_u8(a).min_u8(b);
	}

	__forceinline GSVector4i sat_u8(const GSVector4i& a) const
	{
		return max_u8(a.xyxy()).min_u8(a.zwzw());
	}

	__forceinline GSVector4i sat_u16(const GSVector4i& a, const GSVector4i& b) const
	{
		return max_u16(a).min_u16(b);
	}

	__forceinline GSVector4i sat_u16(const GSVector4i& a) const
	{
		return max_u16(a.xyxy()).min_u16(a.zwzw());
	}

	__forceinline GSVector4i sat_u32(const GSVector4i& a, const GSVector4i& b) const
	{
		return max_u32(a).min_u32(b);
	}

	__forceinline GSVector4i sat_u32(const GSVector4i& a) const
	{
		return max_u32(a.xyxy()).min_u32(a.zwzw());
	}

	__forceinline GSVector4i min_i8(const GSVector4i& a) const
	{
		return GSVector4i(vreinterpretq_s32_s8(vminq_s8(vreinterpretq_s8_s32(v4s), vreinterpretq_s8_s32(a.v4s))));
	}

	__forceinline GSVector4i max_i8(const GSVector4i& a) const
	{
		return GSVector4i(vreinterpretq_s32_s8(vmaxq_s8(vreinterpretq_s8_s32(v4s), vreinterpretq_s8_s32(a.v4s))));
	}

	__forceinline GSVector4i min_i16(const GSVector4i& a) const
	{
		return GSVector4i(vreinterpretq_s32_s16(vminq_s16(vreinterpretq_s16_s32(v4s), vreinterpretq_s16_s32(a.v4s))));
	}

	__forceinline GSVector4i max_i16(const GSVector4i& a) const
	{
		return GSVector4i(vreinterpretq_s32_s16(vmaxq_s16(vreinterpretq_s16_s32(v4s), vreinterpretq_s16_s32(a.v4s))));
	}

	__forceinline GSVector4i min_i32(const GSVector4i& a) const
	{
		return GSVector4i(vminq_s32(v4s, a.v4s));
	}

	__forceinline GSVector4i max_i32(const GSVector4i& a) const
	{
		return GSVector4i(vmaxq_s32(v4s, a.v4s));
	}

	__forceinline GSVector4i min_u8(const GSVector4i& a) const
	{
		return GSVector4i(vreinterpretq_s32_u8(vminq_u8(vreinterpretq_u8_s32(v4s), vreinterpretq_u8_s32(a.v4s))));
	}

	__forceinline GSVector4i max_u8(const GSVector4i& a) const
	{
		return GSVector4i(vreinterpretq_s32_u8(vmaxq_u8(vreinterpretq_u8_s32(v4s), vreinterpretq_u8_s32(a.v4s))));
	}

	__forceinline GSVector4i min_u16(const GSVector4i& a) const
	{
		return GSVector4i(vreinterpretq_s32_u16(vminq_u16(vreinterpretq_u16_s32(v4s), vreinterpretq_u16_s32(a.v4s))));
	}

	__forceinline GSVector4i max_u16(const GSVector4i& a) const
	{
		return GSVector4i(vreinterpretq_s32_u16(vmaxq_u16(vreinterpretq_u16_s32(v4s), vreinterpretq_u16_s32(a.v4s))));
	}

	__forceinline GSVector4i min_u32(const GSVector4i& a) const
	{
		return GSVector4i(vreinterpretq_s32_u32(vminq_u32(vreinterpretq_u32_s32(v4s), vreinterpretq_u32_s32(a.v4s))));
	}

	__forceinline GSVector4i max_u32(const GSVector4i& a) const
	{
		return GSVector4i(vreinterpretq_s32_u32(vmaxq_u32(vreinterpretq_u32_s32(v4s), vreinterpretq_u32_s32(a.v4s))));
	}

	__forceinline static int min_i16(int a, int b)
	{
		return store(load(a).min_i16(load(b)));
	}

	__forceinline GSVector4i clamp8() const
	{
		return pu16().upl8();
	}

	__forceinline GSVector4i blend8(const GSVector4i& a, const GSVector4i& mask) const
	{
		uint8x16_t mask2 = vreinterpretq_u8_s8(vshrq_n_s8(vreinterpretq_s8_s32(mask.v4s), 7));
		return GSVector4i(vreinterpretq_s32_u8(vbslq_u8(mask2, vreinterpretq_u8_s32(a.v4s), vreinterpretq_u8_s32(v4s))));
	}

	template <int mask>
	__forceinline GSVector4i blend16(const GSVector4i& a) const
	{
		// return GSVector4i(_mm_blend_epi16(m, a, mask));
		const uint16_t _mask[8] = {((mask) & (1 << 0)) ? (uint16_t)-1 : 0x0,
			((mask) & (1 << 1)) ? (uint16_t)-1 : 0x0,
			((mask) & (1 << 2)) ? (uint16_t)-1 : 0x0,
			((mask) & (1 << 3)) ? (uint16_t)-1 : 0x0,
			((mask) & (1 << 4)) ? (uint16_t)-1 : 0x0,
			((mask) & (1 << 5)) ? (uint16_t)-1 : 0x0,
			((mask) & (1 << 6)) ? (uint16_t)-1 : 0x0,
			((mask) & (1 << 7)) ? (uint16_t)-1 : 0x0};
		return GSVector4i(vreinterpretq_s32_u16(vbslq_u16(vld1q_u16(_mask), vreinterpretq_u16_s32(a.v4s), vreinterpretq_u16_s32(v4s))));
	}

	template <int mask>
	__forceinline GSVector4i blend32(const GSVector4i& v) const
	{
		constexpr int bit3 = ((mask & 8) * 3) << 3;
		constexpr int bit2 = ((mask & 4) * 3) << 2;
		constexpr int bit1 = ((mask & 2) * 3) << 1;
		constexpr int bit0 = (mask & 1) * 3;
		return blend16<bit3 | bit2 | bit1 | bit0>(v);
	}

	__forceinline GSVector4i blend(const GSVector4i& a, const GSVector4i& mask) const
	{
		// return GSVector4i(_mm_or_si128(_mm_andnot_si128(mask, m), _mm_and_si128(mask, a)));
		return GSVector4i(vreinterpretq_s32_s8(vorrq_s8(vbicq_s8(vreinterpretq_s8_s32(v4s), vreinterpretq_s8_s32(mask.v4s)), vandq_s8(vreinterpretq_s8_s32(mask.v4s), vreinterpretq_s8_s32(a.v4s)))));
	}

	__forceinline GSVector4i mix16(const GSVector4i& a) const
	{
		return blend16<0xaa>(a);
	}

	__forceinline GSVector4i shuffle8(const GSVector4i& mask) const
	{
		// return GSVector4i(_mm_shuffle_epi8(m, mask));
		return GSVector4i(vreinterpretq_s32_s8(vqtbl1q_s8(vreinterpretq_s8_s32(v4s), vreinterpretq_u8_s32(mask.v4s))));
	}

	__forceinline GSVector4i ps16(const GSVector4i& a) const
	{
		// return GSVector4i(_mm_packs_epi16(m, a));
		return GSVector4i(vreinterpretq_s32_s8(vcombine_s8(vqmovn_s16(vreinterpretq_s16_s32(v4s)), vqmovn_s16(vreinterpretq_s16_s32(a.v4s)))));
	}

	__forceinline GSVector4i ps16() const
	{
		// return GSVector4i(_mm_packs_epi16(m, m));
		return GSVector4i(vreinterpretq_s32_s8(vcombine_s8(vqmovn_s16(vreinterpretq_s16_s32(v4s)), vqmovn_s16(vreinterpretq_s16_s32(v4s)))));
	}

	__forceinline GSVector4i pu16(const GSVector4i& a) const
	{
		// return GSVector4i(_mm_packus_epi16(m, a));
		return GSVector4i(vreinterpretq_s32_u8(vcombine_u8(vqmovun_s16(vreinterpretq_s16_s32(v4s)), vqmovun_s16(vreinterpretq_s16_s32(a.v4s)))));
	}

	__forceinline GSVector4i pu16() const
	{
		// return GSVector4i(_mm_packus_epi16(m, m));
		return GSVector4i(vreinterpretq_s32_u8(vcombine_u8(vqmovun_s16(vreinterpretq_s16_s32(v4s)), vqmovun_s16(vreinterpretq_s16_s32(v4s)))));
	}

	__forceinline GSVector4i ps32(const GSVector4i& a) const
	{
		// return GSVector4i(_mm_packs_epi32(m, a));
		return GSVector4i(vreinterpretq_s32_s16(vcombine_s16(vqmovn_s32(v4s), vqmovn_s32(a.v4s))));
	}

	__forceinline GSVector4i ps32() const
	{
		// return GSVector4i(_mm_packs_epi32(m, m));
		return GSVector4i(vreinterpretq_s32_s16(vcombine_s16(vqmovn_s32(v4s), vqmovn_s32(v4s))));
	}

	__forceinline GSVector4i pu32(const GSVector4i& a) const
	{
		// return GSVector4i(_mm_packus_epi32(m, a));
		return GSVector4i(vreinterpretq_s32_u16(vcombine_u16(vqmovun_s32(v4s), vqmovun_s32(a.v4s))));
	}

	__forceinline GSVector4i pu32() const
	{
		// return GSVector4i(_mm_packus_epi32(m, m));
		return GSVector4i(vreinterpretq_s32_u16(vcombine_u16(vqmovun_s32(v4s), vqmovun_s32(v4s))));
	}

	__forceinline GSVector4i upl8(const GSVector4i& a) const
	{
		// return GSVector4i(_mm_unpacklo_epi8(m, a));
		return GSVector4i(vreinterpretq_s32_s8(vzip1q_s8(vreinterpretq_s8_s32(v4s), vreinterpretq_s8_s32(a.v4s))));
	}

	__forceinline GSVector4i uph8(const GSVector4i& a) const
	{
		// return GSVector4i(_mm_unpackhi_epi8(m, a));
		return GSVector4i(vreinterpretq_s32_s8(vzip2q_s8(vreinterpretq_s8_s32(v4s), vreinterpretq_s8_s32(a.v4s))));
	}

	__forceinline GSVector4i upl16(const GSVector4i& a) const
	{
		// return GSVector4i(_mm_unpacklo_epi16(m, a));
		return GSVector4i(vreinterpretq_s32_s16(vzip1q_s16(vreinterpretq_s16_s32(v4s), vreinterpretq_s16_s32(a.v4s))));
	}

	__forceinline GSVector4i uph16(const GSVector4i& a) const
	{
		// return GSVector4i(_mm_unpackhi_epi16(m, a));
		return GSVector4i(vreinterpretq_s32_s16(vzip2q_s16(vreinterpretq_s16_s32(v4s), vreinterpretq_s16_s32(a.v4s))));
	}

	__forceinline GSVector4i upl32(const GSVector4i& a) const
	{
		// return GSVector4i(_mm_unpacklo_epi32(m, a));
		return GSVector4i(vzip1q_s32(v4s, a.v4s));
	}

	__forceinline GSVector4i uph32(const GSVector4i& a) const
	{
		// return GSVector4i(_mm_unpackhi_epi32(m, a));
		return GSVector4i(vzip2q_s32(v4s, a.v4s));
	}

	__forceinline GSVector4i upl64(const GSVector4i& a) const
	{
		// return GSVector4i(_mm_unpacklo_epi64(m, a));
		return GSVector4i(vreinterpretq_s32_s64(vcombine_s64(vget_low_s64(vreinterpretq_s64_s32(v4s)), vget_low_s64(vreinterpretq_s64_s32(a.v4s)))));
	}

	__forceinline GSVector4i uph64(const GSVector4i& a) const
	{
		// return GSVector4i(_mm_unpackhi_epi64(m, a));
		return GSVector4i(vreinterpretq_s32_s64(vcombine_s64(vget_high_s64(vreinterpretq_s64_s32(v4s)), vget_high_s64(vreinterpretq_s64_s32(a.v4s)))));
	}

	__forceinline GSVector4i upl8() const
	{
#if 0 // _M_SSE >= 0x401 // TODO: compiler bug

		return GSVector4i(_mm_cvtepu8_epi16(m));

#else

		// return GSVector4i(_mm_unpacklo_epi8(m, _mm_setzero_si128()));
		return GSVector4i(vreinterpretq_s32_s8(vzip1q_s8(vreinterpretq_s8_s32(v4s), vdupq_n_s8(0))));

#endif
	}

	__forceinline GSVector4i uph8() const
	{
		// return GSVector4i(_mm_unpackhi_epi8(m, _mm_setzero_si128()));
		return GSVector4i(vreinterpretq_s32_s8(vzip2q_s8(vreinterpretq_s8_s32(v4s), vdupq_n_s8(0))));
	}

	__forceinline GSVector4i upl16() const
	{
#if 0 //_M_SSE >= 0x401 // TODO: compiler bug

		return GSVector4i(_mm_cvtepu16_epi32(m));

#else

		// return GSVector4i(_mm_unpacklo_epi16(m, _mm_setzero_si128()));
		return GSVector4i(vreinterpretq_s32_s16(vzip1q_s16(vreinterpretq_s16_s32(v4s), vdupq_n_s16(0))));

#endif
	}

	__forceinline GSVector4i uph16() const
	{
		// return GSVector4i(_mm_unpackhi_epi16(m, _mm_setzero_si128()));
		return GSVector4i(vreinterpretq_s32_s16(vzip2q_s16(vreinterpretq_s16_s32(v4s), vdupq_n_s16(0))));
	}

	__forceinline GSVector4i upl32() const
	{
#if 0 //_M_SSE >= 0x401 // TODO: compiler bug

		return GSVector4i(_mm_cvtepu32_epi64(m));

#else

		// return GSVector4i(_mm_unpacklo_epi32(m, _mm_setzero_si128()));
		return GSVector4i(vzip1q_s32(v4s, vdupq_n_s32(0)));

#endif
	}

	__forceinline GSVector4i uph32() const
	{
		// return GSVector4i(_mm_unpackhi_epi32(m, _mm_setzero_si128()));
		return GSVector4i(vzip2q_s32(v4s, vdupq_n_s32(0)));
	}

	__forceinline GSVector4i upl64() const
	{
		// return GSVector4i(_mm_unpacklo_epi64(m, _mm_setzero_si128()));
		return GSVector4i(vreinterpretq_s32_s64(vcombine_s64(vget_low_s64(vreinterpretq_s64_s32(v4s)), vdup_n_s64(0))));
	}

	__forceinline GSVector4i uph64() const
	{
		// return GSVector4i(_mm_unpackhi_epi64(m, _mm_setzero_si128()));
		return GSVector4i(vreinterpretq_s32_s64(vcombine_s64(vget_high_s64(vreinterpretq_s64_s32(v4s)), vdup_n_s64(0))));
	}

	// WARNING!!!
	//
	// MSVC (2008, 2010 ctp) believes that there is a "mem, reg" form of the pmovz/sx* instructions,
	// turning these intrinsics into a minefield, don't spill regs when using them...

	__forceinline GSVector4i i8to16() const
	{
		// return GSVector4i(_mm_cvtepi8_epi16(m));
		return GSVector4i(vreinterpretq_s32_s16(vmovl_s8(vget_low_s8(vreinterpretq_s8_s32(v4s)))));
	}

	__forceinline GSVector4i u8to16() const
	{
		// return GSVector4i(_mm_cvtepu8_epi16(m));
		return GSVector4i(vreinterpretq_s32_u16(vmovl_u8(vget_low_u8(vreinterpretq_u8_s32(v4s)))));
	}

	__forceinline GSVector4i i8to32() const
	{
		// return GSVector4i(_mm_cvtepi8_epi32(m));
		return GSVector4i(vmovl_s16(vget_low_s16(vmovl_s8(vget_low_s8(vreinterpretq_s8_s32(v4s))))));
	}

	__forceinline GSVector4i u8to32() const
	{
		// return GSVector4i(_mm_cvtepu8_epi32(m));
		return GSVector4i(vreinterpretq_s32_u32(vmovl_u16(vget_low_u16(vmovl_u8(vget_low_u8(vreinterpretq_u8_s32(v4s)))))));
	}

	__forceinline GSVector4i i8to64() const
	{
		// return GSVector4i(_mm_cvtepi8_epi64(m));
		return GSVector4i(vreinterpretq_s32_s64(vmovl_s32(vget_low_s32(vmovl_s16(vget_low_s16(vmovl_s8(vget_low_s8(vreinterpretq_s8_s32(v4s)))))))));
	}

	__forceinline GSVector4i u8to64() const
	{
		return GSVector4i(vreinterpretq_s32_u64(vmovl_u32(vget_low_u32(vmovl_u16(vget_low_u16(vmovl_u8(vget_low_u8(vreinterpretq_u8_s32(v4s)))))))));
	}

	__forceinline GSVector4i i16to32() const
	{
		// return GSVector4i(_mm_cvtepi16_epi32(m));
		return GSVector4i(vmovl_s16(vget_low_s16(vreinterpretq_s16_s32(v4s))));
	}

	__forceinline GSVector4i u16to32() const
	{
		// return GSVector4i(_mm_cvtepu16_epi32(m));
		return GSVector4i(vreinterpretq_s32_u32(vmovl_u16(vget_low_u16(vreinterpretq_u16_s32(v4s)))));
	}

	__forceinline GSVector4i i16to64() const
	{
		// return GSVector4i(_mm_cvtepi16_epi64(m));
		return GSVector4i(vreinterpretq_s32_s64(vmovl_s32(vget_low_s32(vmovl_s16(vget_low_s16(vreinterpretq_s16_s32(v4s)))))));
	}

	__forceinline GSVector4i u16to64() const
	{
		// return GSVector4i(_mm_cvtepu16_epi64(m));
		return GSVector4i(vreinterpretq_s32_u64(vmovl_u32(vget_low_u32(vmovl_u16(vget_low_u16(vreinterpretq_u16_s32(v4s)))))));
	}

	__forceinline GSVector4i i32to64() const
	{
		// return GSVector4i(_mm_cvtepi32_epi64(m));
		return GSVector4i(vreinterpretq_s32_s64(vmovl_s32(vget_low_s32(v4s))));
	}

	__forceinline GSVector4i u32to64() const
	{
		// return GSVector4i(_mm_cvtepu32_epi64(m));
		return GSVector4i(vreinterpretq_s32_u64(vmovl_u32(vget_low_u32(vreinterpretq_u32_s32(v4s)))));
	}

	template <int i>
	__forceinline GSVector4i srl() const
	{
		// return GSVector4i(_mm_srli_si128(m, i));
		return GSVector4i(vreinterpretq_s32_s8(vextq_s8(vreinterpretq_s8_s32(v4s), vdupq_n_s8(0), i)));
	}

	template <int i>
	__forceinline GSVector4i srl(const GSVector4i& v)
	{
		// return GSVector4i(_mm_alignr_epi8(v.m, m, i));
		if constexpr (i >= 16)
			return GSVector4i(vreinterpretq_s32_u8(vextq_u8(vreinterpretq_u8_s32(v.v4s), vdupq_n_u8(0), i - 16)));
		else
			return GSVector4i(vreinterpretq_s32_u8(vextq_u8(vreinterpretq_u8_s32(v4s), vreinterpretq_u8_s32(v.v4s), i)));
	}

	template <int i>
	__forceinline GSVector4i sll() const
	{
		// return GSVector4i(_mm_slli_si128(m, i));
		return GSVector4i(vreinterpretq_s32_s8(vextq_s8(vdupq_n_s8(0), vreinterpretq_s8_s32(v4s), 16 - i)));
	}

	// TODO: Make templates.

	template<int i>
	__forceinline GSVector4i sra16() const
	{
		// return GSVector4i(_mm_srai_epi16(m, i));
		constexpr int count = (i & ~15) ? 15 : i;
		return GSVector4i(vreinterpretq_s32_s16(vshrq_n_s16(vreinterpretq_s16_s32(v4s), count)));
	}

// 	__forceinline GSVector4i sra16(int16x8_t i) const
// 	{
// 		// return GSVector4i(_mm_sra_epi16(m, i));
// 		const int count = vgetq_lane_s16(i, 0);
// 		return GSVector4i(vshlq_s16(vreinterpretq_s16_s32(v4s), vdupq_n_s16(-count)));
// 	}

	template<int i>
	__forceinline GSVector4i sra32() const
	{
		return GSVector4i(vshrq_n_s32(v4s, i));
	}

	__forceinline GSVector4i sra32(int i) const
	{
		return GSVector4i(vshlq_s32(v4s, vdupq_n_s32(-i)));
	}

// 	__forceinline GSVector4i sra32(int32x4_t i) const
// 	{
// 		// TODO: Differs when shift amount is greater than 64.
// 		// return GSVector4i(_mm_sra_epi32(m, i));
// 		const int count = vgetq_lane_s32(i, 0);
// 		return GSVector4i(vshlq_s32(v4s, vdupq_n_s32(-count)));
// 	}

	template<int i>
	__forceinline GSVector4i sll16() const
	{
		// return GSVector4i(_mm_slli_epi16(m, i));
		return GSVector4i(vreinterpretq_s32_s16(vshlq_n_s16(vreinterpretq_s16_s32(v4s), i)));
	}

// 	__forceinline GSVector4i sll16(int16x8_t i) const
// 	{
// 		const int count = vgetq_lane_s16(i, 0);
// 		return GSVector4i(vshlq_s16(vreinterpretq_s16_s32(v4s), vdupq_n_s16(count)));
// 	}

	template<int i>
	__forceinline GSVector4i sll32() const
	{
		// return GSVector4i(_mm_slli_epi32(m, i));
		return GSVector4i(vshlq_n_s32(v4s, i));
	}

	__forceinline GSVector4i sllv32(const GSVector4i& v) const
	{
		return GSVector4i(vshlq_s32(v4s, v.v4s));
	}

	template<int i>
	__forceinline GSVector4i sll64() const
	{
		// return GSVector4i(_mm_slli_epi64(m, i));
		return GSVector4i(vreinterpretq_s32_s64(vshlq_n_s64(vreinterpretq_s64_s32(v4s), i)));
	}

// 	__forceinline GSVector4i sll64(int64x2_t i) const
// 	{
// 		return GSVector4i(_mm_sll_epi64(m, i));
// 	}

	template<int i>
	__forceinline GSVector4i srl16() const
	{
		// return GSVector4i(_mm_srli_epi16(m, i));
		return GSVector4i(vreinterpretq_s32_u16(vshrq_n_u16(vreinterpretq_u16_s32(v4s), i)));
	}

	__forceinline GSVector4i srl16(int i) const
	{
		// return GSVector4i(_mm_srli_epi16(m, i));
		return GSVector4i(vreinterpretq_s32_u16(vshlq_u16(vreinterpretq_u16_s32(v4s), vdupq_n_s16(-i))));
	}

// 	__forceinline GSVector4i srl16(__m128i i) const
// 	{
// 		return GSVector4i(_mm_srl_epi16(m, i));
// 	}

	template<int i>
	__forceinline GSVector4i srl32() const
	{
		return GSVector4i(vreinterpretq_s32_u32(vshrq_n_u32(vreinterpretq_u32_s32(v4s), i)));
	}

	__forceinline GSVector4i srl32(int i) const
	{
		return GSVector4i(vreinterpretq_s32_u32(vshlq_u32(vreinterpretq_u32_s32(v4s), vdupq_n_s32(-i))));
	}

	__forceinline GSVector4i srlv32(const GSVector4i& v) const
	{
		return GSVector4i(vreinterpretq_s32_u32(vshlq_u32(vreinterpretq_u32_s32(v4s), vnegq_s32(v.v4s))));
	}

// 	__forceinline GSVector4i srl64(int i) const
// 	{
// 		return GSVector4i(_mm_srli_epi64(m, i));
// 	}

// 	__forceinline GSVector4i srl64(__m128i i) const
// 	{
// 		return GSVector4i(_mm_srl_epi64(m, i));
// 	}

	__forceinline GSVector4i add8(const GSVector4i& v) const
	{
		return GSVector4i(vreinterpretq_s32_s8(vaddq_s8(vreinterpretq_s8_s32(v4s), vreinterpretq_s8_s32(v.v4s))));
	}

	__forceinline GSVector4i add16(const GSVector4i& v) const
	{
		return GSVector4i(vreinterpretq_s32_s16(vaddq_s16(vreinterpretq_s16_s32(v4s), vreinterpretq_s16_s32(v.v4s))));
	}

	__forceinline GSVector4i add32(const GSVector4i& v) const
	{
		return GSVector4i(vaddq_s32(v4s, v.v4s));
	}

	__forceinline GSVector4i adds8(const GSVector4i& v) const
	{
		return GSVector4i(vreinterpretq_s32_s8(vqaddq_s8(vreinterpretq_s8_s32(v4s), vreinterpretq_s8_s32(v.v4s))));
	}

	__forceinline GSVector4i adds16(const GSVector4i& v) const
	{
		return GSVector4i(vreinterpretq_s32_s16(vqaddq_s16(vreinterpretq_s16_s32(v4s), vreinterpretq_s16_s32(v.v4s))));
	}

	__forceinline GSVector4i addus8(const GSVector4i& v) const
	{
		return GSVector4i(vreinterpretq_s32_u8(vqaddq_u8(vreinterpretq_u8_s32(v4s), vreinterpretq_u8_s32(v.v4s))));
	}

	__forceinline GSVector4i addus16(const GSVector4i& v) const
	{
		return GSVector4i(vreinterpretq_s32_u16(vqaddq_u16(vreinterpretq_u16_s32(v4s), vreinterpretq_u16_s32(v.v4s))));
	}

	__forceinline GSVector4i sub8(const GSVector4i& v) const
	{
		return GSVector4i(vreinterpretq_s32_s8(vsubq_s8(vreinterpretq_s8_s32(v4s), vreinterpretq_s8_s32(v.v4s))));
	}

	__forceinline GSVector4i sub16(const GSVector4i& v) const
	{
		return GSVector4i(vreinterpretq_s32_s16(vsubq_s16(vreinterpretq_s16_s32(v4s), vreinterpretq_s16_s32(v.v4s))));
	}

	__forceinline GSVector4i sub32(const GSVector4i& v) const
	{
		return GSVector4i(vsubq_s32(v4s, v.v4s));
	}

	__forceinline GSVector4i subs8(const GSVector4i& v) const
	{
		return GSVector4i(vreinterpretq_s32_s8(vqsubq_s8(vreinterpretq_s8_s32(v4s), vreinterpretq_s8_s32(v.v4s))));
	}

	__forceinline GSVector4i subs16(const GSVector4i& v) const
	{
		return GSVector4i(vreinterpretq_s32_s16(vqsubq_s16(vreinterpretq_s16_s32(v4s), vreinterpretq_s16_s32(v.v4s))));
	}

	__forceinline GSVector4i subus8(const GSVector4i& v) const
	{
		return GSVector4i(vreinterpretq_s32_u8(vqsubq_u8(vreinterpretq_u8_s32(v4s), vreinterpretq_u8_s32(v.v4s))));
	}

	__forceinline GSVector4i subus16(const GSVector4i& v) const
	{
		return GSVector4i(vreinterpretq_s32_u16(vqsubq_u16(vreinterpretq_u16_s32(v4s), vreinterpretq_u16_s32(v.v4s))));
	}

	__forceinline GSVector4i avg8(const GSVector4i& v) const
	{
		return GSVector4i(vreinterpretq_s32_u8(vrhaddq_u8(vreinterpretq_u8_s32(v4s), vreinterpretq_u8_s32(v.v4s))));
	}

	__forceinline GSVector4i avg16(const GSVector4i& v) const
	{
		return GSVector4i(vreinterpretq_s32_u16(vrhaddq_u16(vreinterpretq_u16_s32(v4s), vreinterpretq_u16_s32(v.v4s))));
	}

	__forceinline GSVector4i mul16hs(const GSVector4i& v) const
	{
		// return GSVector4i(_mm_mulhi_epi16(m, v.m));
		int16x4_t a3210 = vget_low_s16(vreinterpretq_s16_s32(v4s));
		int16x4_t b3210 = vget_low_s16(vreinterpretq_s16_s32(v.v4s));
		int32x4_t ab3210 = vmull_s16(a3210, b3210); /* 3333222211110000 */
		int16x4_t a7654 = vget_high_s16(vreinterpretq_s16_s32(v4s));
		int16x4_t b7654 = vget_high_s16(vreinterpretq_s16_s32(v.v4s));
		int32x4_t ab7654 = vmull_s16(a7654, b7654); /* 7777666655554444 */
		uint16x8x2_t r =
			vuzpq_u16(vreinterpretq_u16_s32(ab3210), vreinterpretq_u16_s32(ab7654));
		return GSVector4i(vreinterpretq_s32_u16(r.val[1]));
	}

// 	__forceinline GSVector4i mul16hu(const GSVector4i& v) const
// 	{
// 		return GSVector4i(_mm_mulhi_epu16(m, v.m));
// 	}

	__forceinline GSVector4i mul16l(const GSVector4i& v) const
	{
		return GSVector4i(vreinterpretq_s32_s16(vmulq_s16(vreinterpretq_s16_s32(v4s), vreinterpretq_s16_s32(v.v4s))));
	}

	__forceinline GSVector4i mul16hrs(const GSVector4i& v) const
	{
		// return GSVector4i(_mm_mulhrs_epi16(m, v.m));
		int32x4_t mul_lo = vmull_s16(vget_low_s16(vreinterpretq_s16_s32(v4s)), vget_low_s16(vreinterpretq_s16_s32(v.v4s)));
		int32x4_t mul_hi = vmull_s16(vget_high_s16(vreinterpretq_s16_s32(v4s)), vget_high_s16(vreinterpretq_s16_s32(v.v4s)));
		int16x4_t narrow_lo = vrshrn_n_s32(mul_lo, 15);
		int16x4_t narrow_hi = vrshrn_n_s32(mul_hi, 15);
		return GSVector4i(vreinterpretq_s32_s16(vcombine_s16(narrow_lo, narrow_hi)));
	}

// 	GSVector4i madd(const GSVector4i& v) const
// 	{
// 		return GSVector4i(_mm_madd_epi16(m, v.m));
// 	}

	template <int shift>
	__forceinline GSVector4i lerp16(const GSVector4i& a, const GSVector4i& f) const
	{
		// (a - this) * f << shift + this

		return add16(a.sub16(*this).modulate16<shift>(f));
	}

	template <int shift>
	__forceinline static GSVector4i lerp16(const GSVector4i& a, const GSVector4i& b, const GSVector4i& c)
	{
		// (a - b) * c << shift

		return a.sub16(b).modulate16<shift>(c);
	}

	template <int shift>
	__forceinline static GSVector4i lerp16(const GSVector4i& a, const GSVector4i& b, const GSVector4i& c, const GSVector4i& d)
	{
		// (a - b) * c << shift + d

		return d.add16(a.sub16(b).modulate16<shift>(c));
	}

	__forceinline GSVector4i lerp16_4(const GSVector4i& a, const GSVector4i& f) const
	{
		// (a - this) * f >> 4 + this (a, this: 8-bit, f: 4-bit)

		return add16(a.sub16(*this).mul16l(f).sra16<4>());
	}

	template <int shift>
	__forceinline GSVector4i modulate16(const GSVector4i& f) const
	{
		// a * f << shift
		if (shift == 0)
		{
			return mul16hrs(f);
		}

		return sll16<shift + 1>().mul16hs(f);
	}

	__forceinline bool eq(const GSVector4i& v) const
	{
		// pxor, ptest, je

		//GSVector4i t = *this ^ v;
		//return _mm_testz_si128(t, t) != 0;
		uint64x2_t res = vceqq_u64(vreinterpretq_u64_s32(v4s), vreinterpretq_u64_s32(v.v4s));
		return (vgetq_lane_u64(res, 0) & vgetq_lane_u64(res, 1)) == 0xFFFFFFFFFFFFFFFFULL;
	}

	__forceinline GSVector4i eq8(const GSVector4i& v) const
	{
		// return GSVector4i(_mm_cmpeq_epi8(m, v.m));
		return GSVector4i(vreinterpretq_s32_u8(vceqq_s8(vreinterpretq_s8_s32(v4s), vreinterpretq_s8_s32(v.v4s))));
	}

	__forceinline GSVector4i eq16(const GSVector4i& v) const
	{
		// return GSVector4i(_mm_cmpeq_epi16(m, v.m));
		return GSVector4i(vreinterpretq_s32_u16(vceqq_s16(vreinterpretq_s16_s32(v4s), vreinterpretq_s16_s32(v.v4s))));
	}

	__forceinline GSVector4i eq32(const GSVector4i& v) const
	{
		// return GSVector4i(_mm_cmpeq_epi32(m, v.m));
		return GSVector4i(vreinterpretq_s32_u32(vceqq_s32(v4s, v.v4s)));
	}

	__forceinline GSVector4i neq8(const GSVector4i& v) const
	{
		return ~eq8(v);
	}

	__forceinline GSVector4i neq16(const GSVector4i& v) const
	{
		return ~eq16(v);
	}

	__forceinline GSVector4i neq32(const GSVector4i& v) const
	{
		return ~eq32(v);
	}

	__forceinline GSVector4i gt8(const GSVector4i& v) const
	{
		return GSVector4i(vreinterpretq_s32_u8(vcgtq_s8(vreinterpretq_s8_s32(v4s), vreinterpretq_s8_s32(v.v4s))));
	}

	__forceinline GSVector4i gt16(const GSVector4i& v) const
	{
		return GSVector4i(vreinterpretq_s32_u16(vcgtq_s16(vreinterpretq_s16_s32(v4s), vreinterpretq_s16_s32(v.v4s))));
	}

	__forceinline GSVector4i gt32(const GSVector4i& v) const
	{
		return GSVector4i(vreinterpretq_s32_u32(vcgtq_s32(v4s, v.v4s)));
	}

	__forceinline GSVector4i lt8(const GSVector4i& v) const
	{
		return GSVector4i(vreinterpretq_s32_u8(vcltq_s8(vreinterpretq_s8_s32(v4s), vreinterpretq_s8_s32(v.v4s))));
	}

	__forceinline GSVector4i lt16(const GSVector4i& v) const
	{
		return GSVector4i(vreinterpretq_s32_u16(vcltq_s16(vreinterpretq_s16_s32(v4s), vreinterpretq_s16_s32(v.v4s))));
	}

	__forceinline GSVector4i lt32(const GSVector4i& v) const
	{
		return GSVector4i(vreinterpretq_s32_u32(vcltq_s32(v4s, v.v4s)));
	}

	__forceinline GSVector4i andnot(const GSVector4i& v) const
	{
		// return GSVector4i(_mm_andnot_si128(v.m, m));
		return GSVector4i(vbicq_s32(v4s, v.v4s));
	}

	__forceinline int mask() const
	{
#if 0
		// TODO: Is there a better way to do this?
		static const int32_t shifts[] = { 0, 1, 2, 3 };
		const uint32x4_t signbits = vshrq_n_u32(v4s, 31);
		const uint32x4_t signbits_in_position = vshlq_u32(signbits, vld1q_s32(shifts));
		return static_cast<int>(vaddvq_u32(signbits_in_position));
#else
		// borrowed from sse2neon
		const uint16x8_t high_bits = vreinterpretq_u16_u8(vshrq_n_u8(vreinterpretq_u8_s32(v4s), 7));
		const uint32x4_t paired16 = vreinterpretq_u32_u16(vsraq_n_u16(high_bits, high_bits, 7));
		const uint64x2_t paired32 = vreinterpretq_u64_u32(vsraq_n_u32(paired16, paired16, 14));
		const uint8x16_t paired64 = vreinterpretq_u8_u64(vsraq_n_u64(paired32, paired32, 28));
		return static_cast<int>(vgetq_lane_u8(paired64, 0) | ((int)vgetq_lane_u8(paired64, 8) << 8));
#endif
	}

	__forceinline bool alltrue() const
	{
		// return mask() == 0xffff;
		return ~(vgetq_lane_u64(vreinterpretq_u64_s32(v4s), 0) & vgetq_lane_u64(vreinterpretq_u64_s32(v4s), 1)) == 0;
	}

	__forceinline bool allfalse() const
	{
		// return _mm_testz_si128(m, m) != 0;
		return (vgetq_lane_u64(vreinterpretq_u64_s32(v4s), 0) | vgetq_lane_u64(vreinterpretq_u64_s32(v4s), 1)) == 0;
	}

	template <int i>
	__forceinline GSVector4i insert8(int a) const
	{
		return GSVector4i(vreinterpretq_s32_u8(vsetq_lane_u8(a, vreinterpretq_u8_s32(v4s), static_cast<uint8_t>(i))));
	}

	template <int i>
	__forceinline int extract8() const
	{
		return vgetq_lane_u8(vreinterpretq_u8_s32(v4s), i);
	}

	template <int i>
	__forceinline GSVector4i insert16(int a) const
	{
		return GSVector4i(vreinterpretq_s32_u16(vsetq_lane_u16(a, vreinterpretq_u16_s32(v4s), static_cast<uint16_t>(i))));
	}

	template <int i>
	__forceinline int extract16() const
	{
		return vgetq_lane_u16(vreinterpretq_u16_s32(v4s), i);
	}

	template <int i>
	__forceinline GSVector4i insert32(int a) const
	{
		return GSVector4i(vsetq_lane_s32(a, v4s, i));
	}

	template <int i>
	__forceinline int extract32() const
	{
		return vgetq_lane_s32(v4s, i);
	}

	template <int i>
	__forceinline GSVector4i insert64(int64 a) const
	{
		return GSVector4i(vreinterpretq_s32_s64(vsetq_lane_s64(a, vreinterpretq_s64_s32(v4s), i)));
	}

	template <int i>
	__forceinline int64 extract64() const
	{
		return vgetq_lane_s64(vreinterpretq_s64_s32(v4s), i);
	}

	template <int src, class T>
	__forceinline GSVector4i gather8_4(const T* ptr) const
	{
		GSVector4i v;

		v = load((int)ptr[extract8<src + 0>() & 0xf]);
		v = v.insert8<1>((int)ptr[extract8<src + 0>() >> 4]);
		v = v.insert8<2>((int)ptr[extract8<src + 1>() & 0xf]);
		v = v.insert8<3>((int)ptr[extract8<src + 1>() >> 4]);
		v = v.insert8<4>((int)ptr[extract8<src + 2>() & 0xf]);
		v = v.insert8<5>((int)ptr[extract8<src + 2>() >> 4]);
		v = v.insert8<6>((int)ptr[extract8<src + 3>() & 0xf]);
		v = v.insert8<7>((int)ptr[extract8<src + 3>() >> 4]);
		v = v.insert8<8>((int)ptr[extract8<src + 4>() & 0xf]);
		v = v.insert8<9>((int)ptr[extract8<src + 4>() >> 4]);
		v = v.insert8<10>((int)ptr[extract8<src + 5>() & 0xf]);
		v = v.insert8<11>((int)ptr[extract8<src + 5>() >> 4]);
		v = v.insert8<12>((int)ptr[extract8<src + 6>() & 0xf]);
		v = v.insert8<13>((int)ptr[extract8<src + 6>() >> 4]);
		v = v.insert8<14>((int)ptr[extract8<src + 7>() & 0xf]);
		v = v.insert8<15>((int)ptr[extract8<src + 7>() >> 4]);

		return v;
	}

	template <class T>
	__forceinline GSVector4i gather8_8(const T* ptr) const
	{
		GSVector4i v;

		v = load((int)ptr[extract8<0>()]);
		v = v.insert8<1>((int)ptr[extract8<1>()]);
		v = v.insert8<2>((int)ptr[extract8<2>()]);
		v = v.insert8<3>((int)ptr[extract8<3>()]);
		v = v.insert8<4>((int)ptr[extract8<4>()]);
		v = v.insert8<5>((int)ptr[extract8<5>()]);
		v = v.insert8<6>((int)ptr[extract8<6>()]);
		v = v.insert8<7>((int)ptr[extract8<7>()]);
		v = v.insert8<8>((int)ptr[extract8<8>()]);
		v = v.insert8<9>((int)ptr[extract8<9>()]);
		v = v.insert8<10>((int)ptr[extract8<10>()]);
		v = v.insert8<11>((int)ptr[extract8<11>()]);
		v = v.insert8<12>((int)ptr[extract8<12>()]);
		v = v.insert8<13>((int)ptr[extract8<13>()]);
		v = v.insert8<14>((int)ptr[extract8<14>()]);
		v = v.insert8<15>((int)ptr[extract8<15>()]);

		return v;
	}

	template <int dst, class T>
	__forceinline GSVector4i gather8_16(const T* ptr, const GSVector4i& a) const
	{
		GSVector4i v = a;

		v = v.insert8<dst + 0>((int)ptr[extract16<0>()]);
		v = v.insert8<dst + 1>((int)ptr[extract16<1>()]);
		v = v.insert8<dst + 2>((int)ptr[extract16<2>()]);
		v = v.insert8<dst + 3>((int)ptr[extract16<3>()]);
		v = v.insert8<dst + 4>((int)ptr[extract16<4>()]);
		v = v.insert8<dst + 5>((int)ptr[extract16<5>()]);
		v = v.insert8<dst + 6>((int)ptr[extract16<6>()]);
		v = v.insert8<dst + 7>((int)ptr[extract16<7>()]);

		return v;
	}

	template <int dst, class T>
	__forceinline GSVector4i gather8_32(const T* ptr, const GSVector4i& a) const
	{
		GSVector4i v = a;

		v = v.insert8<dst + 0>((int)ptr[extract32<0>()]);
		v = v.insert8<dst + 1>((int)ptr[extract32<1>()]);
		v = v.insert8<dst + 2>((int)ptr[extract32<2>()]);
		v = v.insert8<dst + 3>((int)ptr[extract32<3>()]);

		return v;
	}

	template <int src, class T>
	__forceinline GSVector4i gather16_4(const T* ptr) const
	{
		GSVector4i v;

		v = load((int)ptr[extract8<src + 0>() & 0xf]);
		v = v.insert16<1>((int)ptr[extract8<src + 0>() >> 4]);
		v = v.insert16<2>((int)ptr[extract8<src + 1>() & 0xf]);
		v = v.insert16<3>((int)ptr[extract8<src + 1>() >> 4]);
		v = v.insert16<4>((int)ptr[extract8<src + 2>() & 0xf]);
		v = v.insert16<5>((int)ptr[extract8<src + 2>() >> 4]);
		v = v.insert16<6>((int)ptr[extract8<src + 3>() & 0xf]);
		v = v.insert16<7>((int)ptr[extract8<src + 3>() >> 4]);

		return v;
	}

	template <int src, class T>
	__forceinline GSVector4i gather16_8(const T* ptr) const
	{
		GSVector4i v;

		v = load((int)ptr[extract8<src + 0>()]);
		v = v.insert16<1>((int)ptr[extract8<src + 1>()]);
		v = v.insert16<2>((int)ptr[extract8<src + 2>()]);
		v = v.insert16<3>((int)ptr[extract8<src + 3>()]);
		v = v.insert16<4>((int)ptr[extract8<src + 4>()]);
		v = v.insert16<5>((int)ptr[extract8<src + 5>()]);
		v = v.insert16<6>((int)ptr[extract8<src + 6>()]);
		v = v.insert16<7>((int)ptr[extract8<src + 7>()]);

		return v;
	}

	template <class T>
	__forceinline GSVector4i gather16_16(const T* ptr) const
	{
		GSVector4i v;

		v = load((int)ptr[extract16<0>()]);
		v = v.insert16<1>((int)ptr[extract16<1>()]);
		v = v.insert16<2>((int)ptr[extract16<2>()]);
		v = v.insert16<3>((int)ptr[extract16<3>()]);
		v = v.insert16<4>((int)ptr[extract16<4>()]);
		v = v.insert16<5>((int)ptr[extract16<5>()]);
		v = v.insert16<6>((int)ptr[extract16<6>()]);
		v = v.insert16<7>((int)ptr[extract16<7>()]);

		return v;
	}

	template <class T1, class T2>
	__forceinline GSVector4i gather16_16(const T1* ptr1, const T2* ptr2) const
	{
		GSVector4i v;

		v = load((int)ptr2[ptr1[extract16<0>()]]);
		v = v.insert16<1>((int)ptr2[ptr1[extract16<1>()]]);
		v = v.insert16<2>((int)ptr2[ptr1[extract16<2>()]]);
		v = v.insert16<3>((int)ptr2[ptr1[extract16<3>()]]);
		v = v.insert16<4>((int)ptr2[ptr1[extract16<4>()]]);
		v = v.insert16<5>((int)ptr2[ptr1[extract16<5>()]]);
		v = v.insert16<6>((int)ptr2[ptr1[extract16<6>()]]);
		v = v.insert16<7>((int)ptr2[ptr1[extract16<7>()]]);

		return v;
	}

	template <int dst, class T>
	__forceinline GSVector4i gather16_32(const T* ptr, const GSVector4i& a) const
	{
		GSVector4i v = a;

		v = v.insert16<dst + 0>((int)ptr[extract32<0>()]);
		v = v.insert16<dst + 1>((int)ptr[extract32<1>()]);
		v = v.insert16<dst + 2>((int)ptr[extract32<2>()]);
		v = v.insert16<dst + 3>((int)ptr[extract32<3>()]);

		return v;
	}

	template <int src, class T>
	__forceinline GSVector4i gather32_4(const T* ptr) const
	{
		GSVector4i v;

		v = load((int)ptr[extract8<src + 0>() & 0xf]);
		v = v.insert32<1>((int)ptr[extract8<src + 0>() >> 4]);
		v = v.insert32<2>((int)ptr[extract8<src + 1>() & 0xf]);
		v = v.insert32<3>((int)ptr[extract8<src + 1>() >> 4]);
		return v;
	}

	template <int src, class T>
	__forceinline GSVector4i gather32_8(const T* ptr) const
	{
		GSVector4i v;

		v = load((int)ptr[extract8<src + 0>()]);
		v = v.insert32<1>((int)ptr[extract8<src + 1>()]);
		v = v.insert32<2>((int)ptr[extract8<src + 2>()]);
		v = v.insert32<3>((int)ptr[extract8<src + 3>()]);

		return v;
	}

	template <int src, class T>
	__forceinline GSVector4i gather32_16(const T* ptr) const
	{
		GSVector4i v;

		v = load((int)ptr[extract16<src + 0>()]);
		v = v.insert32<1>((int)ptr[extract16<src + 1>()]);
		v = v.insert32<2>((int)ptr[extract16<src + 2>()]);
		v = v.insert32<3>((int)ptr[extract16<src + 3>()]);

		return v;
	}

	template <class T>
	__forceinline GSVector4i gather32_32(const T* ptr) const
	{
		GSVector4i v;

		v = load((int)ptr[extract32<0>()]);
		v = v.insert32<1>((int)ptr[extract32<1>()]);
		v = v.insert32<2>((int)ptr[extract32<2>()]);
		v = v.insert32<3>((int)ptr[extract32<3>()]);

		return v;
	}

	template <class T1, class T2>
	__forceinline GSVector4i gather32_32(const T1* ptr1, const T2* ptr2) const
	{
		GSVector4i v;

		v = load((int)ptr2[ptr1[extract32<0>()]]);
		v = v.insert32<1>((int)ptr2[ptr1[extract32<1>()]]);
		v = v.insert32<2>((int)ptr2[ptr1[extract32<2>()]]);
		v = v.insert32<3>((int)ptr2[ptr1[extract32<3>()]]);

		return v;
	}

	template <int src, class T>
	__forceinline GSVector4i gather64_4(const T* ptr) const
	{
		GSVector4i v;

		v = loadq((int64)ptr[extract8<src + 0>() & 0xf]);
		v = v.insert64<1>((int64)ptr[extract8<src + 0>() >> 4]);

		return v;
	}

	template <int src, class T>
	__forceinline GSVector4i gather64_8(const T* ptr) const
	{
		GSVector4i v;

		v = loadq((int64)ptr[extract8<src + 0>()]);
		v = v.insert64<1>((int64)ptr[extract8<src + 1>()]);

		return v;
	}

	template <int src, class T>
	__forceinline GSVector4i gather64_16(const T* ptr) const
	{
		GSVector4i v;

		v = loadq((int64)ptr[extract16<src + 0>()]);
		v = v.insert64<1>((int64)ptr[extract16<src + 1>()]);

		return v;
	}

	template <int src, class T>
	__forceinline GSVector4i gather64_32(const T* ptr) const
	{
		GSVector4i v;

		v = loadq((int64)ptr[extract32<src + 0>()]);
		v = v.insert64<1>((int64)ptr[extract32<src + 1>()]);

		return v;
	}

	template <class T>
	__forceinline GSVector4i gather64_64(const T* ptr) const
	{
		GSVector4i v;

		v = loadq((int64)ptr[extract64<0>()]);
		v = v.insert64<1>((int64)ptr[extract64<1>()]);

		return v;
	}

	template <class T>
	__forceinline void gather8_4(const T* RESTRICT ptr, GSVector4i* RESTRICT dst) const
	{
		dst[0] = gather8_4<0>(ptr);
		dst[1] = gather8_4<8>(ptr);
	}

	__forceinline void gather8_8(const uint8* RESTRICT ptr, GSVector4i* RESTRICT dst) const
	{
		dst[0] = gather8_8<>(ptr);
	}

	template <class T>
	__forceinline void gather16_4(const T* RESTRICT ptr, GSVector4i* RESTRICT dst) const
	{
		dst[0] = gather16_4<0>(ptr);
		dst[1] = gather16_4<4>(ptr);
		dst[2] = gather16_4<8>(ptr);
		dst[3] = gather16_4<12>(ptr);
	}

	template <class T>
	__forceinline void gather16_8(const T* RESTRICT ptr, GSVector4i* RESTRICT dst) const
	{
		dst[0] = gather16_8<0>(ptr);
		dst[1] = gather16_8<8>(ptr);
	}

	template <class T>
	__forceinline void gather16_16(const T* RESTRICT ptr, GSVector4i* RESTRICT dst) const
	{
		dst[0] = gather16_16<>(ptr);
	}

	template <class T>
	__forceinline void gather32_4(const T* RESTRICT ptr, GSVector4i* RESTRICT dst) const
	{
		dst[0] = gather32_4<0>(ptr);
		dst[1] = gather32_4<2>(ptr);
		dst[2] = gather32_4<4>(ptr);
		dst[3] = gather32_4<6>(ptr);
		dst[4] = gather32_4<8>(ptr);
		dst[5] = gather32_4<10>(ptr);
		dst[6] = gather32_4<12>(ptr);
		dst[7] = gather32_4<14>(ptr);
	}

	template <class T>
	__forceinline void gather32_8(const T* RESTRICT ptr, GSVector4i* RESTRICT dst) const
	{
		dst[0] = gather32_8<0>(ptr);
		dst[1] = gather32_8<4>(ptr);
		dst[2] = gather32_8<8>(ptr);
		dst[3] = gather32_8<12>(ptr);
	}

	template <class T>
	__forceinline void gather32_16(const T* RESTRICT ptr, GSVector4i* RESTRICT dst) const
	{
		dst[0] = gather32_16<0>(ptr);
		dst[1] = gather32_16<4>(ptr);
	}

	template <class T>
	__forceinline void gather32_32(const T* RESTRICT ptr, GSVector4i* RESTRICT dst) const
	{
		dst[0] = gather32_32<>(ptr);
	}

	template <class T>
	__forceinline void gather64_4(const T* RESTRICT ptr, GSVector4i* RESTRICT dst) const
	{
		dst[0] = gather64_4<0>(ptr);
		dst[1] = gather64_4<1>(ptr);
		dst[2] = gather64_4<2>(ptr);
		dst[3] = gather64_4<3>(ptr);
		dst[4] = gather64_4<4>(ptr);
		dst[5] = gather64_4<5>(ptr);
		dst[6] = gather64_4<6>(ptr);
		dst[7] = gather64_4<7>(ptr);
		dst[8] = gather64_4<8>(ptr);
		dst[9] = gather64_4<9>(ptr);
		dst[10] = gather64_4<10>(ptr);
		dst[11] = gather64_4<11>(ptr);
		dst[12] = gather64_4<12>(ptr);
		dst[13] = gather64_4<13>(ptr);
		dst[14] = gather64_4<14>(ptr);
		dst[15] = gather64_4<15>(ptr);
	}

	template <class T>
	__forceinline void gather64_8(const T* RESTRICT ptr, GSVector4i* RESTRICT dst) const
	{
		dst[0] = gather64_8<0>(ptr);
		dst[1] = gather64_8<2>(ptr);
		dst[2] = gather64_8<4>(ptr);
		dst[3] = gather64_8<6>(ptr);
		dst[4] = gather64_8<8>(ptr);
		dst[5] = gather64_8<10>(ptr);
		dst[6] = gather64_8<12>(ptr);
		dst[7] = gather64_8<14>(ptr);
	}

	template <class T>
	__forceinline void gather64_16(const T* RESTRICT ptr, GSVector4i* RESTRICT dst) const
	{
		dst[0] = gather64_16<0>(ptr);
		dst[1] = gather64_16<2>(ptr);
		dst[2] = gather64_16<4>(ptr);
		dst[3] = gather64_16<8>(ptr);
	}

	template <class T>
	__forceinline void gather64_32(const T* RESTRICT ptr, GSVector4i* RESTRICT dst) const
	{
		dst[0] = gather64_32<0>(ptr);
		dst[1] = gather64_32<2>(ptr);
	}

	template <class T>
	__forceinline void gather64_64(const T* RESTRICT ptr, GSVector4i* RESTRICT dst) const
	{
		dst[0] = gather64_64<>(ptr);
	}

	__forceinline static GSVector4i loadnt(const void* p)
	{
		// return GSVector4i(_mm_stream_load_si128((__m128i*)p));
		return GSVector4i(vreinterpretq_s32_s64(vld1q_s64((int64_t*)p)));
	}

	__forceinline static GSVector4i loadl(const void* p)
	{
		return GSVector4i(vcombine_s32(vld1_s32((const int32_t*)p), vcreate_s32(0)));
	}

	__forceinline static GSVector4i loadh(const void* p)
	{
		// return GSVector4i(_mm_castps_si128(_mm_loadh_pi(_mm_setzero_ps(), (__m64*)p)));
		return GSVector4i(vreinterpretq_s32_s64(vcombine_s64(vdup_n_s64(0), vld1_s64((int64_t*)p))));
	}

	__forceinline static GSVector4i loadh(const void* p, const GSVector4i& v)
	{
		// return GSVector4i(_mm_castps_si128(_mm_loadh_pi(_mm_castsi128_ps(v.m), (__m64*)p)));
		return GSVector4i(vreinterpretq_s32_s64(vcombine_s64(vget_low_s64(vreinterpretq_s64_s32(v.v4s)), vld1_s64((int64_t*)p))));
	}

	__forceinline static GSVector4i load(const void* pl, const void* ph)
	{
		// return loadh(ph, loadl(pl));
		return GSVector4i(vreinterpretq_s32_s64(vcombine_s64(vld1_s64((int64_t*)pl), vld1_s64((int64_t*)ph))));
	}
/*
	__forceinline static GSVector4i load(const void* pl, const void* ph)
	{
		__m128i lo = _mm_loadl_epi64((__m128i*)pl);
		__m128i hi = _mm_loadl_epi64((__m128i*)ph);

		return GSVector4i(_mm_unpacklo_epi64(lo, hi));
	}
*/
	template <bool aligned>
	__forceinline static GSVector4i load(const void* p)
	{
		// return GSVector4i(aligned ? _mm_load_si128((__m128i*)p) : _mm_loadu_si128((__m128i*)p));
		return GSVector4i(vreinterpretq_s32_s64(vld1q_s64((int64_t*)p)));
	}

	__forceinline static GSVector4i load(int i)
	{
		// return GSVector4i(_mm_cvtsi32_si128(i));
		return GSVector4i(vsetq_lane_s32(i, vdupq_n_s32(0), 0));
	}

	__forceinline static GSVector4i loadq(int64 i)
	{
		// return GSVector4i(_mm_cvtsi64_si128(i));
		return GSVector4i(vreinterpretq_s32_s64(vsetq_lane_s64(i, vdupq_n_s64(0), 0)));
	}

	__forceinline static void storent(void* p, const GSVector4i& v)
	{
		vst1q_s64((int64_t*)p, vreinterpretq_s64_s32(v.v4s));
	}

	__forceinline static void storel(void* p, const GSVector4i& v)
	{
		// _mm_storel_epi64((__m128i*)p, v.m);
		vst1_s64((int64_t*)p, vget_low_s64(vreinterpretq_s64_s32(v.v4s)));
	}

	__forceinline static void storeh(void* p, const GSVector4i& v)
	{
		// _mm_storeh_pi((__m64*)p, _mm_castsi128_ps(v.m));
		vst1_s64((int64_t*)p, vget_high_s64(vreinterpretq_s64_s32(v.v4s)));
	}

	__forceinline static void store(void* pl, void* ph, const GSVector4i& v)
	{
		GSVector4i::storel(pl, v);
		GSVector4i::storeh(ph, v);
	}

	template <bool aligned>
	__forceinline static void store(void* p, const GSVector4i& v)
	{
		vst1q_s64((int64_t*)p, vreinterpretq_s64_s32(v.v4s));
	}

	__forceinline static int store(const GSVector4i& v)
	{
		return vgetq_lane_s32(v.v4s, 0);
	}

	__forceinline static int64 storeq(const GSVector4i& v)
	{
		return vgetq_lane_s64(vreinterpretq_s64_s32(v.v4s), 0);
	}

	__forceinline static void storent(void* RESTRICT dst, const void* RESTRICT src, size_t size)
	{
		const GSVector4i* s = (const GSVector4i*)src;
		GSVector4i* d = (GSVector4i*)dst;

		if (size == 0)
			return;

		size_t i = 0;
		size_t j = size >> 6;

		for (; i < j; i++, s += 4, d += 4)
		{
			storent(&d[0], s[0]);
			storent(&d[1], s[1]);
			storent(&d[2], s[2]);
			storent(&d[3], s[3]);
		}

		size &= 63;

		if (size == 0)
			return;

		memcpy(d, s, size);
	}

// 	__forceinline static void transpose(GSVector4i& a, GSVector4i& b, GSVector4i& c, GSVector4i& d)
// 	{
// 		_MM_TRANSPOSE4_SI128(a.m, b.m, c.m, d.m);
// 	}

	__forceinline static void sw4(GSVector4i& a, GSVector4i& b, GSVector4i& c, GSVector4i& d)
	{
		const GSVector4i mask(0x0f0f0f0f);
		const GSVector4i e((b << 4).blend(a, mask));
		const GSVector4i f(b.blend(a >> 4, mask));
		const GSVector4i g((d << 4).blend(c, mask));
		const GSVector4i h(d.blend(c >> 4, mask));

		a = e.upl8(f);
		c = e.uph8(f);
		b = g.upl8(h);
		d = g.uph8(h);
	}

	__forceinline static void sw8(GSVector4i& a, GSVector4i& b, GSVector4i& c, GSVector4i& d)
	{
		GSVector4i e = a;
		GSVector4i f = c;

		a = e.upl8(b);
		c = e.uph8(b);
		b = f.upl8(d);
		d = f.uph8(d);
	}

	__forceinline static void sw16(GSVector4i& a, GSVector4i& b, GSVector4i& c, GSVector4i& d)
	{
		GSVector4i e = a;
		GSVector4i f = c;

		a = e.upl16(b);
		c = e.uph16(b);
		b = f.upl16(d);
		d = f.uph16(d);
	}

	__forceinline static void sw16rl(GSVector4i& a, GSVector4i& b, GSVector4i& c, GSVector4i& d)
	{
		GSVector4i e = a;
		GSVector4i f = c;

		a = b.upl16(e);
		c = e.uph16(b);
		b = d.upl16(f);
		d = f.uph16(d);
	}

	__forceinline static void sw16rh(GSVector4i& a, GSVector4i& b, GSVector4i& c, GSVector4i& d)
	{
		GSVector4i e = a;
		GSVector4i f = c;

		a = e.upl16(b);
		c = b.uph16(e);
		b = f.upl16(d);
		d = d.uph16(f);
	}

	__forceinline static void sw32(GSVector4i& a, GSVector4i& b, GSVector4i& c, GSVector4i& d)
	{
		GSVector4i e = a;
		GSVector4i f = c;

		a = e.upl32(b);
		c = e.uph32(b);
		b = f.upl32(d);
		d = f.uph32(d);
	}

	__forceinline static void sw64(GSVector4i& a, GSVector4i& b, GSVector4i& c, GSVector4i& d)
	{
		GSVector4i e = a;
		GSVector4i f = c;

		a = e.upl64(b);
		c = e.uph64(b);
		b = f.upl64(d);
		d = f.uph64(d);
	}

	__forceinline static bool compare16(const void* dst, const void* src, size_t size)
	{
		pxAssert((size & 15) == 0);

		size >>= 4;

		GSVector4i* s = (GSVector4i*)src;
		GSVector4i* d = (GSVector4i*)dst;

		for (size_t i = 0; i < size; i++)
		{
			if (!d[i].eq(s[i]))
			{
				return false;
			}
		}

		return true;
	}

	__forceinline static bool compare64(const void* dst, const void* src, size_t size)
	{
		pxAssert((size & 63) == 0);

		size >>= 6;

		GSVector4i* s = (GSVector4i*)src;
		GSVector4i* d = (GSVector4i*)dst;

		for (size_t i = 0; i < size; ++i)
		{
			GSVector4i v0 = (d[i * 4 + 0] == s[i * 4 + 0]);
			GSVector4i v1 = (d[i * 4 + 1] == s[i * 4 + 1]);
			GSVector4i v2 = (d[i * 4 + 2] == s[i * 4 + 2]);
			GSVector4i v3 = (d[i * 4 + 3] == s[i * 4 + 3]);

			v0 = v0 & v1;
			v2 = v2 & v3;

			if (!(v0 & v2).alltrue())
			{
				return false;
			}
		}

		return true;
	}

	__forceinline static bool update(const void* dst, const void* src, size_t size)
	{
		pxAssert((size & 15) == 0);

		size >>= 4;

		GSVector4i* s = (GSVector4i*)src;
		GSVector4i* d = (GSVector4i*)dst;

		GSVector4i v = GSVector4i::xffffffff();

		for (size_t i = 0; i < size; i++)
		{
			v &= d[i] == s[i];

			d[i] = s[i];
		}

		return v.alltrue();
	}

	__forceinline void operator+=(const GSVector4i& v)
	{
		v4s = vaddq_s32(v4s, v.v4s);
	}

	__forceinline void operator-=(const GSVector4i& v)
	{
		v4s = vsubq_s32(v4s, v.v4s);
	}

	__forceinline void operator+=(int i)
	{
		*this += GSVector4i(i);
	}

	__forceinline void operator-=(int i)
	{
		*this -= GSVector4i(i);
	}

	__forceinline void operator<<=(const int i)
	{
		v4s = vshlq_s32(v4s, vdupq_n_s32(i));
	}

	__forceinline void operator>>=(const int i)
	{
		v4s = vreinterpretq_s32_u32(vshlq_u32(vreinterpretq_u32_s32(v4s), vdupq_n_s32(-i)));
	}

	__forceinline void operator&=(const GSVector4i& v)
	{
		v4s = vreinterpretq_s32_s8(vandq_s8(vreinterpretq_s8_s32(v4s), vreinterpretq_s8_s32(v.v4s)));
	}

	__forceinline void operator|=(const GSVector4i& v)
	{
		v4s = vreinterpretq_s32_s8(vorrq_s8(vreinterpretq_s8_s32(v4s), vreinterpretq_s8_s32(v.v4s)));
	}

	__forceinline void operator^=(const GSVector4i& v)
	{
		v4s = vreinterpretq_s32_s8(veorq_s8(vreinterpretq_s8_s32(v4s), vreinterpretq_s8_s32(v.v4s)));
	}

	__forceinline friend GSVector4i operator+(const GSVector4i& v1, const GSVector4i& v2)
	{
		return GSVector4i(vaddq_s32(v1.v4s, v2.v4s));
	}

	__forceinline friend GSVector4i operator-(const GSVector4i& v1, const GSVector4i& v2)
	{
		return GSVector4i(vsubq_s32(v1.v4s, v2.v4s));
	}

	__forceinline friend GSVector4i operator+(const GSVector4i& v, int i)
	{
		return v + GSVector4i(i);
	}

	__forceinline friend GSVector4i operator-(const GSVector4i& v, int i)
	{
		return v - GSVector4i(i);
	}

	__forceinline friend GSVector4i operator<<(const GSVector4i& v, const int i)
	{
		return GSVector4i(vshlq_s32(v.v4s, vdupq_n_s32(i)));
	}

	__forceinline friend GSVector4i operator>>(const GSVector4i& v, const int i)
	{
		return GSVector4i(vreinterpretq_s32_u32(vshlq_u32(vreinterpretq_u32_s32(v.v4s), vdupq_n_s32(-i))));
	}

	__forceinline friend GSVector4i operator&(const GSVector4i& v1, const GSVector4i& v2)
	{
		return GSVector4i(vreinterpretq_s32_s8(vandq_s8(vreinterpretq_s8_s32(v1.v4s), vreinterpretq_s8_s32(v2.v4s))));
	}

	__forceinline friend GSVector4i operator|(const GSVector4i& v1, const GSVector4i& v2)
	{
		return GSVector4i(vreinterpretq_s32_s8(vorrq_s8(vreinterpretq_s8_s32(v1.v4s), vreinterpretq_s8_s32(v2.v4s))));
	}

	__forceinline friend GSVector4i operator^(const GSVector4i& v1, const GSVector4i& v2)
	{
		return GSVector4i(vreinterpretq_s32_s8(veorq_s8(vreinterpretq_s8_s32(v1.v4s), vreinterpretq_s8_s32(v2.v4s))));
	}

	__forceinline friend GSVector4i operator&(const GSVector4i& v, int i)
	{
		return v & GSVector4i(i);
	}

	__forceinline friend GSVector4i operator|(const GSVector4i& v, int i)
	{
		return v | GSVector4i(i);
	}

	__forceinline friend GSVector4i operator^(const GSVector4i& v, int i)
	{
		return v ^ GSVector4i(i);
	}

	__forceinline friend GSVector4i operator~(const GSVector4i& v)
	{
		return v ^ (v == v);
	}

	__forceinline friend GSVector4i operator==(const GSVector4i& v1, const GSVector4i& v2)
	{
		return GSVector4i(vreinterpretq_s32_u32(vceqq_s32(v1.v4s, v2.v4s)));
	}

	__forceinline friend GSVector4i operator!=(const GSVector4i& v1, const GSVector4i& v2)
	{
		return ~(v1 == v2);
	}

	__forceinline friend GSVector4i operator>(const GSVector4i& v1, const GSVector4i& v2)
	{
		return GSVector4i(vreinterpretq_s32_u32(vcgtq_s32(v1.v4s, v2.v4s)));
	}

	__forceinline friend GSVector4i operator<(const GSVector4i& v1, const GSVector4i& v2)
	{
		return GSVector4i(vreinterpretq_s32_u32(vcltq_s32(v1.v4s, v2.v4s)));
	}

	__forceinline friend GSVector4i operator>=(const GSVector4i& v1, const GSVector4i& v2)
	{
		return (v1 > v2) | (v1 == v2);
	}

	__forceinline friend GSVector4i operator<=(const GSVector4i& v1, const GSVector4i& v2)
	{
		return (v1 < v2) | (v1 == v2);
	}

	__forceinline GSVector4i xxzzlh() const { return GSVector4i(vreinterpretq_s32_s16(vtrn1q_s16(vreinterpretq_s16_s32(v4s), vreinterpretq_s16_s32(v4s)))); }
	__forceinline GSVector4i yywwlh() const { return GSVector4i(vreinterpretq_s32_s16(vtrn2q_s16(vreinterpretq_s16_s32(v4s), vreinterpretq_s16_s32(v4s)))); }
	__forceinline GSVector4i yxwzlh() const { return GSVector4i(vreinterpretq_s32_s16(vrev32q_s16(vreinterpretq_s16_s32(v4s)))); }
	__forceinline GSVector4i xxxxlh() const
	{
		static constexpr uint8_t t1[] = {0,1,0,1,0,1,0,1,8,9,254,255,254,255,245,255};
		static constexpr uint8_t t2[] = {0,1,2,3,4,5,6,7,8,9,8,9,8,9,8,9};
		return GSVector4i(vreinterpretq_s32_u8(vqtbl1q_u8(vqtbl1q_u8(vreinterpretq_u8_s32(v4s), vld1q_u8(t1)), vld1q_u8(t2))));
	}

	__forceinline GSVector4i xxxxl() const
	{
		static constexpr uint8_t t1[] = {0,1,0,1,0,1,0,1,8,9,10,11,12,13,14,15};
		return GSVector4i(vreinterpretq_s32_u8(vqtbl1q_u8(vreinterpretq_u8_s32(v4s), vld1q_u8(t1))));
	}
	__forceinline GSVector4i zwxyl() const
	{
		static constexpr uint8_t t1[] = {4,5,6,7,0,1,2,3,8,9,10,11,12,13,14,15};
		return GSVector4i(vreinterpretq_s32_u8(vqtbl1q_u8(vreinterpretq_u8_s32(v4s), vld1q_u8(t1))));
	}
	__forceinline GSVector4i yxwzl() const
	{
		static constexpr uint8_t t1[] = {2,3,0,1,6,7,4,5,8,9,10,11,12,13,14,15};
		return GSVector4i(vreinterpretq_s32_u8(vqtbl1q_u8(vreinterpretq_u8_s32(v4s), vld1q_u8(t1))));
	}
	__forceinline GSVector4i zwzwl() const
	{
		static constexpr uint8_t t1[] = {4,5,6,7,4,5,6,7,8,9,10,11,12,13,14,15};
		return GSVector4i(vreinterpretq_s32_u8(vqtbl1q_u8(vreinterpretq_u8_s32(v4s), vld1q_u8(t1))));
	}

	__forceinline GSVector4i zzzzh() const
	{
		static constexpr uint8_t t1[] = {0,1,2,3,4,5,6,7,12,13,12,13,12,13,12,13};
		return GSVector4i(vreinterpretq_s32_u8(vqtbl1q_u8(vreinterpretq_u8_s32(v4s), vld1q_u8(t1))));
	}

	// clang-format off

	#define VECTOR4i_SHUFFLE_4(xs, xn, ys, yn, zs, zn, ws, wn) \
		__forceinline GSVector4i xs##ys##zs##ws() const {return GSVector4i(_neon_permute<xn, yn, zn, wn>(v4s));}

		// __forceinline GSVector4i xs##ys##zs##ws() const {return GSVector4i(_neon_permute<xn, yn, zn, wn>(v4s));}
		// __forceinline GSVector4i xs##ys##zs##ws() const {return GSVector4i(_mm_shuffle_epi32(m, _MM_SHUFFLE(wn, zn, yn, xn)));}
		// __forceinline GSVector4i xs##ys##zs##ws##l() const {return GSVector4i(_mm_shufflelo_epi16(m, _MM_SHUFFLE(wn, zn, yn, xn)));}
		// __forceinline GSVector4i xs##ys##zs##ws##h() const {return GSVector4i(_mm_shufflehi_epi16(m, _MM_SHUFFLE(wn, zn, yn, xn)));}
		// __forceinline GSVector4i xs##ys##zs##ws##lh() const {return GSVector4i(_mm_shufflehi_epi16(_mm_shufflelo_epi16(m, _MM_SHUFFLE(wn, zn, yn, xn)), _MM_SHUFFLE(wn, zn, yn, xn)));}

	#define VECTOR4i_SHUFFLE_3(xs, xn, ys, yn, zs, zn) \
		VECTOR4i_SHUFFLE_4(xs, xn, ys, yn, zs, zn, x, 0) \
		VECTOR4i_SHUFFLE_4(xs, xn, ys, yn, zs, zn, y, 1) \
		VECTOR4i_SHUFFLE_4(xs, xn, ys, yn, zs, zn, z, 2) \
		VECTOR4i_SHUFFLE_4(xs, xn, ys, yn, zs, zn, w, 3) \

	#define VECTOR4i_SHUFFLE_2(xs, xn, ys, yn) \
		VECTOR4i_SHUFFLE_3(xs, xn, ys, yn, x, 0) \
		VECTOR4i_SHUFFLE_3(xs, xn, ys, yn, y, 1) \
		VECTOR4i_SHUFFLE_3(xs, xn, ys, yn, z, 2) \
		VECTOR4i_SHUFFLE_3(xs, xn, ys, yn, w, 3) \

	#define VECTOR4i_SHUFFLE_1(xs, xn) \
		VECTOR4i_SHUFFLE_2(xs, xn, x, 0) \
		VECTOR4i_SHUFFLE_2(xs, xn, y, 1) \
		VECTOR4i_SHUFFLE_2(xs, xn, z, 2) \
		VECTOR4i_SHUFFLE_2(xs, xn, w, 3) \

	VECTOR4i_SHUFFLE_1(x, 0)
	VECTOR4i_SHUFFLE_1(y, 1)
	VECTOR4i_SHUFFLE_1(z, 2)
	VECTOR4i_SHUFFLE_1(w, 3)

	// clang-format on

	__forceinline static GSVector4i zero() { return GSVector4i(0); }

	__forceinline static GSVector4i xffffffff() { return GSVector4i(0xFFFFFFFF); }

	__forceinline static GSVector4i x00000001() { return xffffffff().srl32<31>(); }
	__forceinline static GSVector4i x00000003() { return xffffffff().srl32<30>(); }
	__forceinline static GSVector4i x00000007() { return xffffffff().srl32<29>(); }
	__forceinline static GSVector4i x0000000f() { return xffffffff().srl32<28>(); }
	__forceinline static GSVector4i x0000001f() { return xffffffff().srl32<27>(); }
	__forceinline static GSVector4i x0000003f() { return xffffffff().srl32<26>(); }
	__forceinline static GSVector4i x0000007f() { return xffffffff().srl32<25>(); }
	__forceinline static GSVector4i x000000ff() { return xffffffff().srl32<24>(); }
	__forceinline static GSVector4i x000001ff() { return xffffffff().srl32<23>(); }
	__forceinline static GSVector4i x000003ff() { return xffffffff().srl32<22>(); }
	__forceinline static GSVector4i x000007ff() { return xffffffff().srl32<21>(); }
	__forceinline static GSVector4i x00000fff() { return xffffffff().srl32<20>(); }
	__forceinline static GSVector4i x00001fff() { return xffffffff().srl32<19>(); }
	__forceinline static GSVector4i x00003fff() { return xffffffff().srl32<18>(); }
	__forceinline static GSVector4i x00007fff() { return xffffffff().srl32<17>(); }
	__forceinline static GSVector4i x0000ffff() { return xffffffff().srl32<16>(); }
	__forceinline static GSVector4i x0001ffff() { return xffffffff().srl32<15>(); }
	__forceinline static GSVector4i x0003ffff() { return xffffffff().srl32<14>(); }
	__forceinline static GSVector4i x0007ffff() { return xffffffff().srl32<13>(); }
	__forceinline static GSVector4i x000fffff() { return xffffffff().srl32<12>(); }
	__forceinline static GSVector4i x001fffff() { return xffffffff().srl32<11>(); }
	__forceinline static GSVector4i x003fffff() { return xffffffff().srl32<10>(); }
	__forceinline static GSVector4i x007fffff() { return xffffffff().srl32< 9>(); }
	__forceinline static GSVector4i x00ffffff() { return xffffffff().srl32< 8>(); }
	__forceinline static GSVector4i x01ffffff() { return xffffffff().srl32< 7>(); }
	__forceinline static GSVector4i x03ffffff() { return xffffffff().srl32< 6>(); }
	__forceinline static GSVector4i x07ffffff() { return xffffffff().srl32< 5>(); }
	__forceinline static GSVector4i x0fffffff() { return xffffffff().srl32< 4>(); }
	__forceinline static GSVector4i x1fffffff() { return xffffffff().srl32< 3>(); }
	__forceinline static GSVector4i x3fffffff() { return xffffffff().srl32< 2>(); }
	__forceinline static GSVector4i x7fffffff() { return xffffffff().srl32< 1>(); }

	__forceinline static GSVector4i x80000000() { return xffffffff().sll32<31>(); }
	__forceinline static GSVector4i xc0000000() { return xffffffff().sll32<30>(); }
	__forceinline static GSVector4i xe0000000() { return xffffffff().sll32<29>(); }
	__forceinline static GSVector4i xf0000000() { return xffffffff().sll32<28>(); }
	__forceinline static GSVector4i xf8000000() { return xffffffff().sll32<27>(); }
	__forceinline static GSVector4i xfc000000() { return xffffffff().sll32<26>(); }
	__forceinline static GSVector4i xfe000000() { return xffffffff().sll32<25>(); }
	__forceinline static GSVector4i xff000000() { return xffffffff().sll32<24>(); }
	__forceinline static GSVector4i xff800000() { return xffffffff().sll32<23>(); }
	__forceinline static GSVector4i xffc00000() { return xffffffff().sll32<22>(); }
	__forceinline static GSVector4i xffe00000() { return xffffffff().sll32<21>(); }
	__forceinline static GSVector4i xfff00000() { return xffffffff().sll32<20>(); }
	__forceinline static GSVector4i xfff80000() { return xffffffff().sll32<19>(); }
	__forceinline static GSVector4i xfffc0000() { return xffffffff().sll32<18>(); }
	__forceinline static GSVector4i xfffe0000() { return xffffffff().sll32<17>(); }
	__forceinline static GSVector4i xffff0000() { return xffffffff().sll32<16>(); }
	__forceinline static GSVector4i xffff8000() { return xffffffff().sll32<15>(); }
	__forceinline static GSVector4i xffffc000() { return xffffffff().sll32<14>(); }
	__forceinline static GSVector4i xffffe000() { return xffffffff().sll32<13>(); }
	__forceinline static GSVector4i xfffff000() { return xffffffff().sll32<12>(); }
	__forceinline static GSVector4i xfffff800() { return xffffffff().sll32<11>(); }
	__forceinline static GSVector4i xfffffc00() { return xffffffff().sll32<10>(); }
	__forceinline static GSVector4i xfffffe00() { return xffffffff().sll32< 9>(); }
	__forceinline static GSVector4i xffffff00() { return xffffffff().sll32< 8>(); }
	__forceinline static GSVector4i xffffff80() { return xffffffff().sll32< 7>(); }
	__forceinline static GSVector4i xffffffc0() { return xffffffff().sll32< 6>(); }
	__forceinline static GSVector4i xffffffe0() { return xffffffff().sll32< 5>(); }
	__forceinline static GSVector4i xfffffff0() { return xffffffff().sll32< 4>(); }
	__forceinline static GSVector4i xfffffff8() { return xffffffff().sll32< 3>(); }
	__forceinline static GSVector4i xfffffffc() { return xffffffff().sll32< 2>(); }
	__forceinline static GSVector4i xfffffffe() { return xffffffff().sll32< 1>(); }

	__forceinline static GSVector4i x0001() { return xffffffff().srl16<15>(); }
	__forceinline static GSVector4i x0003() { return xffffffff().srl16<14>(); }
	__forceinline static GSVector4i x0007() { return xffffffff().srl16<13>(); }
	__forceinline static GSVector4i x000f() { return xffffffff().srl16<12>(); }
	__forceinline static GSVector4i x001f() { return xffffffff().srl16<11>(); }
	__forceinline static GSVector4i x003f() { return xffffffff().srl16<10>(); }
	__forceinline static GSVector4i x007f() { return xffffffff().srl16< 9>(); }
	__forceinline static GSVector4i x00ff() { return xffffffff().srl16< 8>(); }
	__forceinline static GSVector4i x01ff() { return xffffffff().srl16< 7>(); }
	__forceinline static GSVector4i x03ff() { return xffffffff().srl16< 6>(); }
	__forceinline static GSVector4i x07ff() { return xffffffff().srl16< 5>(); }
	__forceinline static GSVector4i x0fff() { return xffffffff().srl16< 4>(); }
	__forceinline static GSVector4i x1fff() { return xffffffff().srl16< 3>(); }
	__forceinline static GSVector4i x3fff() { return xffffffff().srl16< 2>(); }
	__forceinline static GSVector4i x7fff() { return xffffffff().srl16< 1>(); }

	__forceinline static GSVector4i x8000() { return xffffffff().sll16<15>(); }
	__forceinline static GSVector4i xc000() { return xffffffff().sll16<14>(); }
	__forceinline static GSVector4i xe000() { return xffffffff().sll16<13>(); }
	__forceinline static GSVector4i xf000() { return xffffffff().sll16<12>(); }
	__forceinline static GSVector4i xf800() { return xffffffff().sll16<11>(); }
	__forceinline static GSVector4i xfc00() { return xffffffff().sll16<10>(); }
	__forceinline static GSVector4i xfe00() { return xffffffff().sll16< 9>(); }
	__forceinline static GSVector4i xff00() { return xffffffff().sll16< 8>(); }
	__forceinline static GSVector4i xff80() { return xffffffff().sll16< 7>(); }
	__forceinline static GSVector4i xffc0() { return xffffffff().sll16< 6>(); }
	__forceinline static GSVector4i xffe0() { return xffffffff().sll16< 5>(); }
	__forceinline static GSVector4i xfff0() { return xffffffff().sll16< 4>(); }
	__forceinline static GSVector4i xfff8() { return xffffffff().sll16< 3>(); }
	__forceinline static GSVector4i xfffc() { return xffffffff().sll16< 2>(); }
	__forceinline static GSVector4i xfffe() { return xffffffff().sll16< 1>(); }

	__forceinline static GSVector4i xffffffff(const GSVector4i& v) { return v == v; }

	__forceinline static GSVector4i x00000001(const GSVector4i& v) { return xffffffff(v).srl32<31>(); }
	__forceinline static GSVector4i x00000003(const GSVector4i& v) { return xffffffff(v).srl32<30>(); }
	__forceinline static GSVector4i x00000007(const GSVector4i& v) { return xffffffff(v).srl32<29>(); }
	__forceinline static GSVector4i x0000000f(const GSVector4i& v) { return xffffffff(v).srl32<28>(); }
	__forceinline static GSVector4i x0000001f(const GSVector4i& v) { return xffffffff(v).srl32<27>(); }
	__forceinline static GSVector4i x0000003f(const GSVector4i& v) { return xffffffff(v).srl32<26>(); }
	__forceinline static GSVector4i x0000007f(const GSVector4i& v) { return xffffffff(v).srl32<25>(); }
	__forceinline static GSVector4i x000000ff(const GSVector4i& v) { return xffffffff(v).srl32<24>(); }
	__forceinline static GSVector4i x000001ff(const GSVector4i& v) { return xffffffff(v).srl32<23>(); }
	__forceinline static GSVector4i x000003ff(const GSVector4i& v) { return xffffffff(v).srl32<22>(); }
	__forceinline static GSVector4i x000007ff(const GSVector4i& v) { return xffffffff(v).srl32<21>(); }
	__forceinline static GSVector4i x00000fff(const GSVector4i& v) { return xffffffff(v).srl32<20>(); }
	__forceinline static GSVector4i x00001fff(const GSVector4i& v) { return xffffffff(v).srl32<19>(); }
	__forceinline static GSVector4i x00003fff(const GSVector4i& v) { return xffffffff(v).srl32<18>(); }
	__forceinline static GSVector4i x00007fff(const GSVector4i& v) { return xffffffff(v).srl32<17>(); }
	__forceinline static GSVector4i x0000ffff(const GSVector4i& v) { return xffffffff(v).srl32<16>(); }
	__forceinline static GSVector4i x0001ffff(const GSVector4i& v) { return xffffffff(v).srl32<15>(); }
	__forceinline static GSVector4i x0003ffff(const GSVector4i& v) { return xffffffff(v).srl32<14>(); }
	__forceinline static GSVector4i x0007ffff(const GSVector4i& v) { return xffffffff(v).srl32<13>(); }
	__forceinline static GSVector4i x000fffff(const GSVector4i& v) { return xffffffff(v).srl32<12>(); }
	__forceinline static GSVector4i x001fffff(const GSVector4i& v) { return xffffffff(v).srl32<11>(); }
	__forceinline static GSVector4i x003fffff(const GSVector4i& v) { return xffffffff(v).srl32<10>(); }
	__forceinline static GSVector4i x007fffff(const GSVector4i& v) { return xffffffff(v).srl32< 9>(); }
	__forceinline static GSVector4i x00ffffff(const GSVector4i& v) { return xffffffff(v).srl32< 8>(); }
	__forceinline static GSVector4i x01ffffff(const GSVector4i& v) { return xffffffff(v).srl32< 7>(); }
	__forceinline static GSVector4i x03ffffff(const GSVector4i& v) { return xffffffff(v).srl32< 6>(); }
	__forceinline static GSVector4i x07ffffff(const GSVector4i& v) { return xffffffff(v).srl32< 5>(); }
	__forceinline static GSVector4i x0fffffff(const GSVector4i& v) { return xffffffff(v).srl32< 4>(); }
	__forceinline static GSVector4i x1fffffff(const GSVector4i& v) { return xffffffff(v).srl32< 3>(); }
	__forceinline static GSVector4i x3fffffff(const GSVector4i& v) { return xffffffff(v).srl32< 2>(); }
	__forceinline static GSVector4i x7fffffff(const GSVector4i& v) { return xffffffff(v).srl32< 1>(); }

	__forceinline static GSVector4i x80000000(const GSVector4i& v) { return xffffffff(v).sll32<31>(); }
	__forceinline static GSVector4i xc0000000(const GSVector4i& v) { return xffffffff(v).sll32<30>(); }
	__forceinline static GSVector4i xe0000000(const GSVector4i& v) { return xffffffff(v).sll32<29>(); }
	__forceinline static GSVector4i xf0000000(const GSVector4i& v) { return xffffffff(v).sll32<28>(); }
	__forceinline static GSVector4i xf8000000(const GSVector4i& v) { return xffffffff(v).sll32<27>(); }
	__forceinline static GSVector4i xfc000000(const GSVector4i& v) { return xffffffff(v).sll32<26>(); }
	__forceinline static GSVector4i xfe000000(const GSVector4i& v) { return xffffffff(v).sll32<25>(); }
	__forceinline static GSVector4i xff000000(const GSVector4i& v) { return xffffffff(v).sll32<24>(); }
	__forceinline static GSVector4i xff800000(const GSVector4i& v) { return xffffffff(v).sll32<23>(); }
	__forceinline static GSVector4i xffc00000(const GSVector4i& v) { return xffffffff(v).sll32<22>(); }
	__forceinline static GSVector4i xffe00000(const GSVector4i& v) { return xffffffff(v).sll32<21>(); }
	__forceinline static GSVector4i xfff00000(const GSVector4i& v) { return xffffffff(v).sll32<20>(); }
	__forceinline static GSVector4i xfff80000(const GSVector4i& v) { return xffffffff(v).sll32<19>(); }
	__forceinline static GSVector4i xfffc0000(const GSVector4i& v) { return xffffffff(v).sll32<18>(); }
	__forceinline static GSVector4i xfffe0000(const GSVector4i& v) { return xffffffff(v).sll32<17>(); }
	__forceinline static GSVector4i xffff0000(const GSVector4i& v) { return xffffffff(v).sll32<16>(); }
	__forceinline static GSVector4i xffff8000(const GSVector4i& v) { return xffffffff(v).sll32<15>(); }
	__forceinline static GSVector4i xffffc000(const GSVector4i& v) { return xffffffff(v).sll32<14>(); }
	__forceinline static GSVector4i xffffe000(const GSVector4i& v) { return xffffffff(v).sll32<13>(); }
	__forceinline static GSVector4i xfffff000(const GSVector4i& v) { return xffffffff(v).sll32<12>(); }
	__forceinline static GSVector4i xfffff800(const GSVector4i& v) { return xffffffff(v).sll32<11>(); }
	__forceinline static GSVector4i xfffffc00(const GSVector4i& v) { return xffffffff(v).sll32<10>(); }
	__forceinline static GSVector4i xfffffe00(const GSVector4i& v) { return xffffffff(v).sll32< 9>(); }
	__forceinline static GSVector4i xffffff00(const GSVector4i& v) { return xffffffff(v).sll32< 8>(); }
	__forceinline static GSVector4i xffffff80(const GSVector4i& v) { return xffffffff(v).sll32< 7>(); }
	__forceinline static GSVector4i xffffffc0(const GSVector4i& v) { return xffffffff(v).sll32< 6>(); }
	__forceinline static GSVector4i xffffffe0(const GSVector4i& v) { return xffffffff(v).sll32< 5>(); }
	__forceinline static GSVector4i xfffffff0(const GSVector4i& v) { return xffffffff(v).sll32< 4>(); }
	__forceinline static GSVector4i xfffffff8(const GSVector4i& v) { return xffffffff(v).sll32< 3>(); }
	__forceinline static GSVector4i xfffffffc(const GSVector4i& v) { return xffffffff(v).sll32< 2>(); }
	__forceinline static GSVector4i xfffffffe(const GSVector4i& v) { return xffffffff(v).sll32< 1>(); }

	__forceinline static GSVector4i x0001(const GSVector4i& v) { return xffffffff(v).srl16<15>(); }
	__forceinline static GSVector4i x0003(const GSVector4i& v) { return xffffffff(v).srl16<14>(); }
	__forceinline static GSVector4i x0007(const GSVector4i& v) { return xffffffff(v).srl16<13>(); }
	__forceinline static GSVector4i x000f(const GSVector4i& v) { return xffffffff(v).srl16<12>(); }
	__forceinline static GSVector4i x001f(const GSVector4i& v) { return xffffffff(v).srl16<11>(); }
	__forceinline static GSVector4i x003f(const GSVector4i& v) { return xffffffff(v).srl16<10>(); }
	__forceinline static GSVector4i x007f(const GSVector4i& v) { return xffffffff(v).srl16< 9>(); }
	__forceinline static GSVector4i x00ff(const GSVector4i& v) { return xffffffff(v).srl16< 8>(); }
	__forceinline static GSVector4i x01ff(const GSVector4i& v) { return xffffffff(v).srl16< 7>(); }
	__forceinline static GSVector4i x03ff(const GSVector4i& v) { return xffffffff(v).srl16< 6>(); }
	__forceinline static GSVector4i x07ff(const GSVector4i& v) { return xffffffff(v).srl16< 5>(); }
	__forceinline static GSVector4i x0fff(const GSVector4i& v) { return xffffffff(v).srl16< 4>(); }
	__forceinline static GSVector4i x1fff(const GSVector4i& v) { return xffffffff(v).srl16< 3>(); }
	__forceinline static GSVector4i x3fff(const GSVector4i& v) { return xffffffff(v).srl16< 2>(); }
	__forceinline static GSVector4i x7fff(const GSVector4i& v) { return xffffffff(v).srl16< 1>(); }

	__forceinline static GSVector4i x8000(const GSVector4i& v) { return xffffffff(v).sll16<15>(); }
	__forceinline static GSVector4i xc000(const GSVector4i& v) { return xffffffff(v).sll16<14>(); }
	__forceinline static GSVector4i xe000(const GSVector4i& v) { return xffffffff(v).sll16<13>(); }
	__forceinline static GSVector4i xf000(const GSVector4i& v) { return xffffffff(v).sll16<12>(); }
	__forceinline static GSVector4i xf800(const GSVector4i& v) { return xffffffff(v).sll16<11>(); }
	__forceinline static GSVector4i xfc00(const GSVector4i& v) { return xffffffff(v).sll16<10>(); }
	__forceinline static GSVector4i xfe00(const GSVector4i& v) { return xffffffff(v).sll16< 9>(); }
	__forceinline static GSVector4i xff00(const GSVector4i& v) { return xffffffff(v).sll16< 8>(); }
	__forceinline static GSVector4i xff80(const GSVector4i& v) { return xffffffff(v).sll16< 7>(); }
	__forceinline static GSVector4i xffc0(const GSVector4i& v) { return xffffffff(v).sll16< 6>(); }
	__forceinline static GSVector4i xffe0(const GSVector4i& v) { return xffffffff(v).sll16< 5>(); }
	__forceinline static GSVector4i xfff0(const GSVector4i& v) { return xffffffff(v).sll16< 4>(); }
	__forceinline static GSVector4i xfff8(const GSVector4i& v) { return xffffffff(v).sll16< 3>(); }
	__forceinline static GSVector4i xfffc(const GSVector4i& v) { return xffffffff(v).sll16< 2>(); }
	__forceinline static GSVector4i xfffe(const GSVector4i& v) { return xffffffff(v).sll16< 1>(); }

	__forceinline static GSVector4i xff(int n) { return m_xff[n]; }
	__forceinline static GSVector4i x0f(int n) { return m_x0f[n]; }
};
