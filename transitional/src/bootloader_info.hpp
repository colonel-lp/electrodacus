#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ella {
// Original ESP32 second-stage bootloader only. Never read beyond the standard
// partition-table address; an unrecognised/larger image is reported as unknown.
constexpr size_t kBootloaderAddress = 0x1000;
constexpr size_t kBootloaderRegionSize = 0x7000;

struct BootloaderInfo {
    const char *source = "unknown";
    const char *error = "No identifiable version metadata";
    char idfVersion[64] = {};
    char compiled[24] = {};
    uint32_t projectVersion = 0;
    bool hasProjectVersion = false;
};

inline uint32_t little32(const uint8_t *p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

inline bool printableField(const uint8_t *p, size_t length, char *out) {
    for (size_t i = 0; i < length; ++i) {
        if (p[i] == 0) { out[i] = 0; return i != 0; }
        if (p[i] < 32 || p[i] > 126) return false;
        out[i] = static_cast<char>(p[i]);
    }
    return false;
}

inline bool versionText(const char *s) {
    if (*s++ != 'v' || *s < '0' || *s > '9') return false;
    while (*s >= '0' && *s <= '9') ++s;
    if (*s++ != '.' || *s < '0' || *s > '9') return false;
    for (; *s; ++s) {
        if (!((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'z') ||
              (*s >= 'A' && *s <= 'Z') || *s == '.' || *s == '-' || *s == '_')) return false;
    }
    return true;
}

// Reader accepts offsets relative to 0x1000. No flash writes or SDK version
// substitution. Legacy text is explicitly marked as inferred, not metadata.
template <typename Reader>
BootloaderInfo readBootloaderInfo(Reader read) {
    BootloaderInfo result;
    uint8_t header[24];
    if (!read(0, header, sizeof(header))) { result.error = "Bootloader read failed"; return result; }
    if (header[0] != 0xe9 || header[1] == 0 || header[1] > 16 || header[12] || header[13]) {
        result.error = "Unrecognised ESP32 bootloader header"; return result;
    }
    size_t offset = sizeof(header);
    uint8_t checksum = 0xef;
    bool marker = false, ambiguous = false;
    char candidate[64] = {};
    for (unsigned segment = 0; segment < header[1]; ++segment) {
        uint8_t segmentHeader[8];
        if (offset > kBootloaderRegionSize - sizeof(segmentHeader) || !read(offset, segmentHeader, sizeof(segmentHeader)))
            return BootloaderInfo{};
        offset += sizeof(segmentHeader);
        const size_t length = little32(segmentHeader + 4);
        if (length > kBootloaderRegionSize - offset) return BootloaderInfo{};
        if (segment == 0 && length >= 80) {
            uint8_t desc[80];
            if (!read(offset, desc, sizeof(desc))) return BootloaderInfo{};
            char idf[32] = {}, compiled[24] = {};
            // ESP-IDF 5.x descriptor layout, decoded without using a newer SDK.
            if (desc[0] == 80 && desc[1] == 0 && desc[2] == 0 && desc[3] == 0 &&
                printableField(desc + 8, 32, idf) && versionText(idf) &&
                printableField(desc + 40, 24, compiled)) {
                std::strcpy(result.idfVersion, idf);
                std::strcpy(result.compiled, compiled);
                result.projectVersion = little32(desc + 4);
                result.hasProjectVersion = true;
                result.source = "descriptor";
            }
        }
        char token[192] = {};
        size_t used = 0;
        bool printable = true;
        size_t remaining = length;
        while (remaining) {
            uint8_t chunk[128];
            const size_t count = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
            if (!read(offset, chunk, count)) return BootloaderInfo{};
            for (size_t i = 0; i < count; ++i) {
                const uint8_t byte = chunk[i];
                checksum ^= byte;
                if (byte == 0) {
                    token[used] = 0;
                    if (printable) {
                        if (std::strstr(token, "2nd stage bootloader")) marker = true;
                        if (used < sizeof(candidate) && versionText(token)) {
                            if (candidate[0] && std::strcmp(candidate, token) != 0) ambiguous = true;
                            else std::strcpy(candidate, token);
                        }
                    }
                    used = 0; printable = true;
                } else if (byte < 32 || byte > 126 || used == sizeof(token) - 1) {
                    printable = false;
                } else if (printable) token[used++] = static_cast<char>(byte);
            }
            offset += count; remaining -= count;
        }
    }
    const size_t checksumOffset = offset + (15 - offset % 16);
    uint8_t stored;
    if (checksumOffset >= kBootloaderRegionSize || !read(checksumOffset, &stored, 1) || stored != checksum) {
        BootloaderInfo bad; bad.error = "Bootloader image checksum could not be validated"; return bad;
    }
    if (result.hasProjectVersion) { result.error = ""; return result; }
    if (marker && candidate[0] && !ambiguous) {
        std::strcpy(result.idfVersion, candidate);
        result.source = "embedded-text-inferred";
        result.error = "";
    } else if (ambiguous) result.error = "Multiple embedded versions; cannot identify reliably";
    return result;
}
} // namespace ella
