#include <stdint.h>
#include <array>

constexpr uint8_t hexCharToByte(char c) {
    return (c >= '0' && c <= '9') ? (c - '0') :
           (c >= 'A' && c <= 'F') ? (c - 'A' + 10) :
           (c >= 'a' && c <= 'f') ? (c - 'a' + 10) : 0;
}

constexpr uint8_t parseHexByte(const char* str) {
    return (hexCharToByte(str[0]) << 4) | hexCharToByte(str[1]);
}

constexpr std::array<uint8_t, 6> str2mac(const char macStr[17]) {
    return {
        parseHexByte(macStr),
        parseHexByte(macStr + 3),
        parseHexByte(macStr + 6),
        parseHexByte(macStr + 9),
        parseHexByte(macStr + 12),
        parseHexByte(macStr + 15)
    };
}

#define STR2MAC(mac) str2mac(mac).data()



char macStrBuf[19];
const char *mac2str(const uint8_t *mac) {
    for(int i = 0; i < 6; i++) {
        snprintf(macStrBuf + i*3, sizeof(macStrBuf)-1-i*3, "%02X%s", mac[i], i < 5 ? ":" : "");
    }
    return macStrBuf;
}