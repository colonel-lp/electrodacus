#include "bootloader_info.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
#include <string>
#include <limits>
using namespace ella;
using Bytes = std::vector<uint8_t>;
void put32(Bytes &v, size_t at, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) v.at(at + i) = value >> (i * 8);
}
Bytes image(const Bytes &payload) {
    Bytes v(32); v[0] = 0xe9; v[1] = 1;
    put32(v, 24, 0x3fff0030); put32(v, 28, payload.size());
    v.insert(v.end(), payload.begin(), payload.end());
    uint8_t check = 0xef;
    for (auto c : payload) check ^= c;
    const auto end = v.size(); v.resize(end + 15 - end % 16 + 1);
    v.back() = check;
    return v;
}
Bytes text(const char *s) { return Bytes(s, s + std::strlen(s) + 1); }
BootloaderInfo inspect(const Bytes &v, size_t failAt = std::numeric_limits<size_t>::max()) {
    return readBootloaderInfo([&](size_t offset, void *out, size_t count) {
        assert(offset <= kBootloaderRegionSize && count <= kBootloaderRegionSize - offset);
        if (offset >= failAt || offset > v.size() || count > v.size() - offset) return false;
        std::memcpy(out, v.data() + offset, count); return true;
    });
}
int main() {
    Bytes desc(80);
    desc[0] = 80; put32(desc, 4, 7);
    std::memcpy(desc.data() + 8, "v5.2.1", 7);
    std::memcpy(desc.data() + 40, "Mar 30 2023 19:14:17", 21);
    auto modern = image(desc);
    auto info = inspect(modern);
    assert(info.hasProjectVersion && info.projectVersion == 7);
    assert(std::strcmp(info.source, "descriptor") == 0);
    assert(std::strcmp(info.idfVersion, "v5.2.1") == 0);
    // Legacy standalone IDF string plus bootloader log marker, crossing a read chunk.
    Bytes legacy(120);
    auto version = text("v4.0.1-23-g1234567"); legacy.insert(legacy.end(), version.begin(), version.end());
    auto marker = text("ESP-IDF %s 2nd stage bootloader"); legacy.insert(legacy.end(), marker.begin(), marker.end());
    info = inspect(image(legacy));
    assert(std::strcmp(info.source, "embedded-text-inferred") == 0);
    assert(std::strcmp(info.idfVersion, "v4.0.1-23-g1234567") == 0 && !info.hasProjectVersion);
    auto other = text("v3.3.0"); legacy.insert(legacy.end(), other.begin(), other.end());
    assert(!inspect(image(legacy)).idfVersion[0]); // ambiguous strings are not a version
    assert(!inspect(image(version)).idfVersion[0]); // random version alone is not sufficient
    assert(!inspect(image(Bytes(256))).idfVersion[0]); // stripped logging => Unknown
    modern.back() ^= 1; assert(!inspect(modern).idfVersion[0]);
    modern = image(desc); modern.pop_back(); assert(!inspect(modern).idfVersion[0]);
    modern = image(desc); put32(modern, 28, 0xffffffff); assert(!inspect(modern).idfVersion[0]);
    modern = image(desc); modern[12] = 2; assert(!inspect(modern).idfVersion[0]);
    modern = image(desc); assert(!inspect(modern, 32).idfVersion[0]);
    std::memset(desc.data() + 8, 'v', 32); assert(!inspect(image(desc)).idfVersion[0]);
    // Exercise malformed/truncated input under ASan/UBSan without out-of-region reads.
    uint32_t random = 11;
    for (unsigned n = 0; n < 3000; ++n) {
        Bytes bytes(n % 400);
        for (auto &b : bytes) { random = random * 1664525u + 1013904223u; b = random >> 24; }
        inspect(bytes);
    }
    std::puts("Bootloader identification: metadata, legacy, unknown, corrupt and bounded-read tests passed");
}
