// Stubbed out for AetherSX2 

#include "PrecompiledHeader.h"

#include <stdio.h>
#include <string.h>

#include "IopCommon.h"
#include "Mdec.h"

void mdecInit(void) {
}


void mdecWrite0(u32 data) {
}

void mdecWrite1(u32 data) {
}

u32 mdecRead0(void) {
	return 0;
}

u32 mdecRead1(void) {
	return 0;
}

void psxDma0(u32 adr, u32 bcr, u32 chcr) {
	HW_DMA0_CHCR &= ~0x01000000;
	psxDmaInterrupt(0);
}

void psxDma1(u32 adr, u32 bcr, u32 chcr) {
	HW_DMA1_CHCR &= ~0x01000000;
	psxDmaInterrupt(1);
}
