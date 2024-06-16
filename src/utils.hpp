#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <arpa/inet.h>

/* Fletcher checksum algorithm. */
static inline uint16_t fletcher_checksum(const void *data, size_t len, size_t off) {
    const uint8_t *ptr = static_cast<const uint8_t *>(data);
    int length = len;

    int32_t x, y;
    uint32_t mul;
    uint32_t c0 = 0, c1 = 0;
    uint16_t checksum = 0;

    for (auto index = 0; index < length; index++) {
        if (index == off || index == off + 1) {
            c1 += c0;
            ptr++;
        } else {
            c0 = c0 + *(ptr++);
            c1 += c0;
        }
    }

    c0 = c0 % 255;
    c1 = c1 % 255;
    mul = (length - off) * (c0);

    x = mul - c0 - c1;
    y = c1 - mul - 1;

    if (y >= 0) {
        y++;
    }
    if (x < 0) {
        x--;
    }

    x %= 255;
    y %= 255;

    if (x == 0) {
        x = 255;
    }
    if (y == 0) {
        y = 255;
    }

    y &= 0x00FF;

    return (x << 8) | y;
}

/* CRC checksum algorithm. */
static inline uint16_t crc_checksum(const void *data, size_t len) {
    uint32_t sum = 0;
    const uint16_t *addr = static_cast<const uint16_t *>(data);

    for (size_t i = 0; i < len / 2; ++i) {
        sum += *addr++;
    }
    if (len & 1) {
        sum += *reinterpret_cast<const uint8_t *>(addr);
    }

    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    return static_cast<uint16_t>(~sum);
}

constexpr bool is_little_endian() noexcept {
    return __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__;
}

#ifndef htonll
static inline uint64_t htonll(uint64_t value) noexcept {
    static const int num = 1;
    if (is_little_endian()) {
        return ((uint64_t)htonl(value & 0xFFFFFFFF) << 32) | htonl(value >> 32);
    } else {
        return value;
    }
}
#endif

#ifndef ntohll
static inline uint64_t ntohll(uint64_t value) noexcept {
    return htonll(value);
}
#endif

/* 将宿主机字节序的IP地址转换字符串 */
static inline std::string ip_to_str(uint32_t ip) noexcept {
    char buf[INET_ADDRSTRLEN];
    uint32_t ip_network_order = htonl(ip);
    inet_ntop(AF_INET, &ip_network_order, buf, INET_ADDRSTRLEN);
    return std::string(buf);
}

/* 将二进制位掩码转换为掩码位数 */
static inline uint32_t mask_to_num(uint32_t mask) noexcept {
    return 32 - __builtin_ctz(mask);
}
