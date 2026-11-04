#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ella {
constexpr uint32_t kRecordMagic = 0x454c4c41;
constexpr uint32_t kFailedBootLimit = 3;
constexpr uint32_t kHealthyMs = 60000;
constexpr uint32_t kUploadIdleMs = 15000;
constexpr uint32_t kUploadMaxMs = 300000;

// A separate NVS namespace; never stored in /cfg or the filesystem.
struct BootRecord {
    uint32_t magic = kRecordMagic;
    uint32_t version = 1;
    uint32_t attempts = 0;
    uint32_t forced = 0;
    uint8_t image[16] = {};
};

inline bool validRecord(const BootRecord &record) {
    return record.magic == kRecordMagic && record.version == 1 &&
           record.attempts <= kFailedBootLimit && record.forced <= 1;
}

// Three unconfirmed starts cause recovery on the following boot. Bind the
// counter to an image so a replacement/previous image gets its own attempt.
inline bool startBoot(BootRecord &record, const uint8_t image[16]) {
    if (std::memcmp(record.image, image, sizeof(record.image)) != 0) {
        record = BootRecord{};
        std::memcpy(record.image, image, sizeof(record.image));
    }
    const bool recover = record.forced || record.attempts >= kFailedBootLimit;
    if (record.attempts < kFailedBootLimit) ++record.attempts;
    return recover;
}

inline bool elapsed(uint32_t now, uint32_t then, uint32_t duration) {
    return static_cast<uint32_t>(now - then) >= duration;
}

inline bool validChunk(size_t received, size_t expected, size_t index,
                       size_t total, size_t length) {
    return total == expected && index == received && received <= expected &&
           length > 0 && length <= expected - received;
}

// The first app segment contains the standard 256-byte esp_app_desc_t at
// offset 32. Bootloader and filesystem images do not have this descriptor.
constexpr size_t kPrefixSize = 288;
inline bool validAppPrefix(const uint8_t *prefix, size_t length) {
    if (length < kPrefixSize || prefix[0] != 0xe9 || prefix[1] == 0 ||
        prefix[1] > 16 || prefix[12] != 0 || prefix[13] != 0) return false;
    const uint8_t magic[] = {0x32, 0x54, 0xcd, 0xab};
    const char project[] = "electrodacus-esp32";
    return std::memcmp(prefix + 32, magic, sizeof(magic)) == 0 &&
           std::memcmp(prefix + 80, project, sizeof(project)) == 0;
}
} // namespace ella
