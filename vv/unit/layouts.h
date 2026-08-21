/* CAN message byte layouts, asserted by test_can_layout.c and exported to
 * layouts.json for the conformance stage to check the DBC files against.
 *
 * byte_order is the order of multi-byte fields IN THE FRAME:
 *   "little" - KincoDrive packs uint16 as data[lo], data[hi]
 *   "big"    - PowerStage and LEDDriver pack uint16 as data[hi], data[lo]
 * This inconsistency is real and deliberate to record; see review finding 8. */
#ifndef VV_LAYOUTS_H
#define VV_LAYOUTS_H

typedef struct {
    const char *board;
    unsigned    id;
    const char *name;
    unsigned    dlc;
    const char *byte_order;
} vv_layout_t;

static const vv_layout_t VV_LAYOUTS[] = {
    /* KincoDrive - little endian uint16 */
    {"kincodrive", 0x101, "Bootloader_RX",     2, "little"},
    {"kincodrive", 0x110, "Cmd_HS_Power",      1, "little"},
    {"kincodrive", 0x111, "Cmd_Fan_PWM",       5, "little"},
    {"kincodrive", 0x112, "Cmd_EEPROM",        1, "little"},
    {"kincodrive", 0x113, "Cmd_OC_Threshold",  6, "little"},
    {"kincodrive", 0x114, "Cmd_UV_Threshold",  4, "little"},
    {"kincodrive", 0x120, "Bcast_Status",      8, "little"},
    {"kincodrive", 0x121, "Bcast_Currents",    8, "little"},
    {"kincodrive", 0x122, "Bcast_Temps",       6, "little"},
    {"kincodrive", 0x123, "Bcast_Fans",        5, "little"},
    {"kincodrive", 0x124, "Bcast_GPIO",        8, "little"},
    {"kincodrive", 0x125, "Bcast_Raw_ADC",     6, "little"},
    {"kincodrive", 0x126, "Bcast_Config_A",    8, "little"},
    {"kincodrive", 0x127, "Bcast_Config_B",    8, "little"},

    /* PowerStage - big endian uint16 */
    {"powerstage", 0x130, "Device_Addr",       2, "big"},
    {"powerstage", 0x140, "Cmd_Fan",           5, "big"},
    {"powerstage", 0x141, "Cmd_HS",            1, "big"},
    {"powerstage", 0x142, "Cmd_OC",            8, "big"},
    {"powerstage", 0x143, "Cmd_EEPROM",        1, "big"},
    {"powerstage", 0x144, "Cmd_UV",            6, "big"},
    {"powerstage", 0x145, "Cmd_Ctrl",          2, "big"},
    {"powerstage", 0x146, "Cmd_Page_Dwell",    3, "big"},
    {"powerstage", 0x147, "Cmd_Bat_Cfg",       1, "big"},
    {"powerstage", 0x148, "Cmd_OC_Reset",      1, "big"},
    {"powerstage", 0x150, "Bcast_HS_State",    5, "big"},
    {"powerstage", 0x151, "Bcast_HS_Curr_A",   8, "big"},
    {"powerstage", 0x152, "Bcast_Voltage",     8, "big"},
    {"powerstage", 0x153, "Bcast_Fan",         7, "big"},
    {"powerstage", 0x154, "Bcast_EEPROM",      8, "big"},
    {"powerstage", 0x155, "Bcast_HS_Curr_B",   4, "big"},
    {"powerstage", 0x156, "Bcast_UV",          6, "big"},
    {"powerstage", 0x157, "Bcast_OC_Cfg_A",    8, "big"},
    {"powerstage", 0x159, "Bcast_IO_Status",   3, "big"},
    {"powerstage", 0x15A, "Bcast_Battery_Cfg", 8, "big"},

    /* LEDDriver - big endian uint16 */
    {"leddriver",  0x160, "DeviceID",          2, "big"},
    {"leddriver",  0x170, "LightSet",          3, "big"},
    {"leddriver",  0x171, "VoltageSet",        5, "big"},
    {"leddriver",  0x172, "EEPROMSet",         1, "big"},
    {"leddriver",  0x178, "EEPROMData",        8, "big"},
    {"leddriver",  0x179, "LightStatus",       8, "big"},
    {"leddriver",  0x17A, "DevStatus",         2, "big"},

    /* ButtonBoard - little endian uint16, matching the knob's Knob.dbc
     * convention rather than PowerStage/LEDDriver's big endian. */
    {"buttonboard", 0x780, "Bootloader_RX",    2, "little"},
    {"buttonboard", 0x790, "Cmd_Led",          2, "little"},
    {"buttonboard", 0x791, "Cmd_Buffer",       1, "little"},
    {"buttonboard", 0x792, "Cmd_Encoder",      3, "little"},
    {"buttonboard", 0x793, "Cmd_EEPROM",       1, "little"},
    {"buttonboard", 0x7A0, "Knob_State",       8, "little"},
    {"buttonboard", 0x7A1, "Buttons",          2, "little"},
    {"buttonboard", 0x7A2, "Led_State",        3, "little"},
    {"buttonboard", 0x7A3, "Dev_Status",       2, "little"},
    {"buttonboard", 0x7A4, "EEPROM_Data",      8, "little"},

    /* Knob - no DBC, recorded for completeness only */
    {"knob",       0x661, "KnobState",         8, "big"},
    {"knob",       0x662, "ErrorState",        1, "big"},
    {"knob",       0x664, "ErrorCount",        3, "big"},
    {"knob",       0x665, "KnobCommand",       8, "big"},
    {"knob",       0x667, "Bootloader_RX",     2, "big"},
};

#define VV_LAYOUT_COUNT (sizeof(VV_LAYOUTS) / sizeof(VV_LAYOUTS[0]))

#endif /* VV_LAYOUTS_H */
