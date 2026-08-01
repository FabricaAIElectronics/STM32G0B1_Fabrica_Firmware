/* Asserts CAN frame packing/unpacking behaviour and exports the layout table.
 *
 * The pack/unpack helpers below mirror what the firmware does. They are
 * deliberately duplicated rather than #included from the firmware, because the
 * firmware versions are entangled with HAL types. Any divergence between these
 * and the firmware is caught by the conformance stage comparing both against
 * the DBC. */
#include <stdio.h>
#include <stdint.h>
#include "harness.h"
#include "layouts.h"

static uint16_t unpack_le16(const uint8_t *d) { return (uint16_t)((d[1] << 8) | d[0]); }
static uint16_t unpack_be16(const uint8_t *d) { return (uint16_t)((d[0] << 8) | d[1]); }

static void write_layouts_json(void)
{
    FILE *fh = fopen("layouts.json", "w");
    if (!fh) { printf("TEST FAIL layouts_json_open\n"); return; }

    fprintf(fh, "{\n  \"boards\": {\n");
    const char *boards[] = {"kincodrive", "powerstage", "leddriver", "knob"};
    for (unsigned b = 0; b < 4; b++) {
        fprintf(fh, "    \"%s\": [\n", boards[b]);
        int first = 1;
        for (unsigned i = 0; i < VV_LAYOUT_COUNT; i++) {
            if (strcmp(VV_LAYOUTS[i].board, boards[b]) != 0) continue;
            fprintf(fh, "%s      {\"id\": %u, \"name\": \"%s\", \"dlc\": %u, \"byte_order\": \"%s\"}",
                    first ? "" : ",\n", VV_LAYOUTS[i].id, VV_LAYOUTS[i].name,
                    VV_LAYOUTS[i].dlc, VV_LAYOUTS[i].byte_order);
            first = 0;
        }
        fprintf(fh, "\n    ]%s\n", b == 3 ? "" : ",");
    }
    fprintf(fh, "  }\n}\n");
    fclose(fh);
    printf("TEST PASS layouts_json_written\n");
}

int main(void)
{
    /* Byte-order helpers behave as the two conventions require. */
    const uint8_t sample[2] = {0x34, 0x12};
    VV_EQ_U32("unpack_le16", unpack_le16(sample), 0x1234u);
    VV_EQ_U32("unpack_be16", unpack_be16(sample), 0x3412u);

    /* KincoDrive Cmd_OC_Threshold: 3 little-endian uint16 in 6 bytes. */
    const uint8_t oc_kinco[6] = {0xE8, 0x03, 0xD0, 0x07, 0xB8, 0x0B};
    VV_EQ_U32("kinco_oc_drive", unpack_le16(&oc_kinco[0]), 1000u);
    VV_EQ_U32("kinco_oc_ext",   unpack_le16(&oc_kinco[2]), 2000u);
    VV_EQ_U32("kinco_oc_sc",    unpack_le16(&oc_kinco[4]), 3000u);

    /* PowerStage Cmd_OC: 4 big-endian uint16 in 8 bytes. */
    const uint8_t oc_ps[8] = {0x03, 0xE8, 0x07, 0xD0, 0x0B, 0xB8, 0x0F, 0xA0};
    VV_EQ_U32("ps_oc_aux",   unpack_be16(&oc_ps[0]), 1000u);
    VV_EQ_U32("ps_oc_led",   unpack_be16(&oc_ps[2]), 2000u);
    VV_EQ_U32("ps_oc_drive", unpack_be16(&oc_ps[4]), 3000u);
    VV_EQ_U32("ps_oc_cap",   unpack_be16(&oc_ps[6]), 4000u);

    /* Every declared layout has a sane DLC. */
    for (unsigned i = 0; i < VV_LAYOUT_COUNT; i++) {
        char label[96];
        snprintf(label, sizeof label, "layout_dlc_%s_%s",
                 VV_LAYOUTS[i].board, VV_LAYOUTS[i].name);
        VV_CHECK(label, VV_LAYOUTS[i].dlc >= 1 && VV_LAYOUTS[i].dlc <= 8);
    }

    write_layouts_json();
    VV_REPORT();
}
