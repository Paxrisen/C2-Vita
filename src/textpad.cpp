// Fuellung am Ende des Codesegments: vita-elf-create braucht ~2 KiB Platz vor dem Datensegment
extern "C" __attribute__((used, section(".text.zzpad"))) const unsigned char civ2_textpad[0x400] = {0};
