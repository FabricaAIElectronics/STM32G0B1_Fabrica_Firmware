#include "canopen_pdo.h"

CanOpen_Frame CanOpen_Pdo_CreateOutFrame(uint8_t node_id, uint16_t rpdo_id, uint8_t data_len, CanOpen_FrameData data) {
    CanOpen_Frame frame;
    CanOpen_Frame_CreateFrame(&frame, node_id, rpdo_id, data_len, &data, 0);

    return frame;
}
