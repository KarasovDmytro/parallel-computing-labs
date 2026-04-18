#pragma once
#include <cstdint>

#pragma pack(push, 1) 
struct MessageHeader {
    uint8_t command;
    uint32_t payloadSize;
};
#pragma pack(pop)

enum CommandCodes : uint8_t {
    SEND_CONFIG = 0x01,
    START = 0x02,
    GET_STATUS = 0x03,

    ACK_CONFIG = 0x11,
    ACK_START = 0x12,
    STATUS = 0x13,
    RESULT = 0x14,
    ERROR_RES = 0xFF
};