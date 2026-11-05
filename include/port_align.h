#ifndef POKEPLATINUM_PORT_ALIGN_H
#define POKEPLATINUM_PORT_ALIGN_H

// A 64-bit member of a saved structure: the DS's compiler aligns it to 4 bytes, 64-bit hosts to 8. Packing
// it with 4-byte alignment keeps the port's save structures (and so its save file) the DS's.
#ifdef SDK_BUILD_ARM
#define PORT_ALIGN4
#else
#define PORT_ALIGN4 __attribute__((packed, aligned(4)))
#endif

#endif // POKEPLATINUM_PORT_ALIGN_H
