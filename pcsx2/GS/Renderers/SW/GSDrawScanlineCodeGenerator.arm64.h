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

#pragma once

#include "GSScanlineEnvironment.h"

#include "vixl/aarch64/macro-assembler-aarch64.h"

class GSDrawScanlineCodeGenerator2
{
public:
	GSDrawScanlineCodeGenerator2(vixl::aarch64::MacroAssembler& armAsm_, void* param, uint64 key);
	void Generate();

private:
	void Init_NEON();
	void Step_NEON();
	void TestZ_NEON(const vixl::aarch64::VRegister& temp1, const vixl::aarch64::VRegister& temp2);
	void SampleTexture_NEON();
	void Wrap_NEON(const vixl::aarch64::VRegister& uv0);
	void Wrap_NEON(const vixl::aarch64::VRegister& uv0, const vixl::aarch64::VRegister& uv1);
	void SampleTextureLOD_NEON();
	void WrapLOD_NEON(const vixl::aarch64::VRegister& uv0);
	void WrapLOD_NEON(const vixl::aarch64::VRegister& uv0, const vixl::aarch64::VRegister& uv1);
	void AlphaTFX_NEON();
	void ReadMask_NEON();
	void TestAlpha_NEON();
	void ColorTFX_NEON();
	void Fog_NEON();
	void ReadFrame_NEON();
	void TestDestAlpha_NEON();
	void WriteMask_NEON();
	void WriteZBuf_NEON();
	void AlphaBlend_NEON();
	void WriteFrame_NEON();
	void ReadPixel_NEON(const vixl::aarch64::VRegister& dst, const vixl::aarch64::WRegister& addr);
	void WritePixel_NEON(const vixl::aarch64::VRegister& src, const vixl::aarch64::WRegister& addr, const vixl::aarch64::WRegister& mask, bool high, bool fast, int psm, int fz);
	void WritePixel_NEON(const vixl::aarch64::VRegister& src, const vixl::aarch64::WRegister& addr, uint8 i, int psm);
	void ReadTexel_NEON(int pixels, int mip_offset = 0);
	void ReadTexel_NEON(const vixl::aarch64::VRegister& dst, const vixl::aarch64::VRegister& addr, uint8 i);

	void modulate16(const vixl::aarch64::VRegister& a, const vixl::aarch64::VRegister& f, uint8 shift);
	void lerp16(const vixl::aarch64::VRegister& a, const vixl::aarch64::VRegister& b, const vixl::aarch64::VRegister& f, uint8 shift);
	void lerp16_4(const vixl::aarch64::VRegister& a, const vixl::aarch64::VRegister& b, const vixl::aarch64::VRegister& f);
	void mix16(const vixl::aarch64::VRegister& a, const vixl::aarch64::VRegister& b, const vixl::aarch64::VRegister& temp);
	void clamp16(const vixl::aarch64::VRegister& a, const vixl::aarch64::VRegister& temp);
	void alltrue(const vixl::aarch64::VRegister& test);
	void blend(const vixl::aarch64::VRegister& a, const vixl::aarch64::VRegister& b, const vixl::aarch64::VRegister& mask);
	void blendr(const vixl::aarch64::VRegister& b, const vixl::aarch64::VRegister& a, const vixl::aarch64::VRegister& mask);
	void blend8(const vixl::aarch64::VRegister& a, const vixl::aarch64::VRegister& b, const vixl::aarch64::VRegister& mask, const vixl::aarch64::VRegister& temp);
	void blend8r(const vixl::aarch64::VRegister& b, const vixl::aarch64::VRegister& a, const vixl::aarch64::VRegister& mask, const vixl::aarch64::VRegister& temp);
	void split16_2x8(const vixl::aarch64::VRegister& l, const vixl::aarch64::VRegister& h, const vixl::aarch64::VRegister& src);

	vixl::aarch64::MacroAssembler& armAsm;

	GSScanlineSelector m_sel;
	GSScanlineLocalData& m_local;

	vixl::aarch64::Label m_step_label;
};
