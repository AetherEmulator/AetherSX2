#pragma once

extern void mdecInit();
extern void mdecWrite0(u32 data);
extern void mdecWrite1(u32 data);
extern u32  mdecRead0();
extern u32  mdecRead1();
extern void psxDma0(u32 madr, u32 bcr, u32 chcr);
extern void psxDma1(u32 madr, u32 bcr, u32 chcr);
