#include "recovery_policy.hpp"
#include <cassert>
#include <cstdio>
#include <limits>

int main() {
    using namespace ella;
    uint8_t image[16] = {1};
    BootRecord r;
    assert(validRecord(r));
    assert(!startBoot(r, image) && r.attempts == 1);
    assert(!startBoot(r, image) && r.attempts == 2);
    assert(!startBoot(r, image) && r.attempts == 3);
    assert(startBoot(r, image) && r.attempts == 3);
    r.attempts = 0; // confirmed healthy
    assert(!startBoot(r, image));
    r.forced = 1;
    assert(startBoot(r, image));
    ++image[0];
    assert(!startBoot(r, image) && r.attempts == 1 && r.forced == 0);
    r.version = 99;
    assert(!validRecord(r));
    r = BootRecord{};
    r.forced = 2;
    assert(!validRecord(r));
    assert(!elapsed(59999, 0, kHealthyMs));
    assert(elapsed(60000, 0, kHealthyMs));
    assert(elapsed(1000, std::numeric_limits<uint32_t>::max() - 999, 2000));
    assert(!elapsed(999, std::numeric_limits<uint32_t>::max() - 999, 2000));
    assert(validChunk(0, 100, 0, 100, 30));
    assert(validChunk(30, 100, 30, 100, 70));
    assert(!validChunk(30, 100, 29, 100, 70));
    assert(!validChunk(30, 100, 30, 101, 70));
    assert(!validChunk(30, 100, 30, 100, 71));
    assert(!validChunk(101, 100, 101, 100, 1));
    assert(!validChunk(0, 100, 0, 100, 0));
    uint8_t p[kPrefixSize] = {};
    p[0] = 0xe9; p[1] = 4;
    const uint8_t magic[] = {0x32, 0x54, 0xcd, 0xab};
    std::memcpy(p + 32, magic, 4);
    std::memcpy(p + 80, "electrodacus-esp32", 18);
    assert(validAppPrefix(p, sizeof(p)));
    assert(!validAppPrefix(p, sizeof(p) - 1));
    p[12] = 2; assert(!validAppPrefix(p, sizeof(p))); p[12] = 0;
    p[32] = 0; assert(!validAppPrefix(p, sizeof(p))); p[32] = magic[0];
    p[80] = 'x'; assert(!validAppPrefix(p, sizeof(p)));
    std::puts("Recovery policy: all assertions passed");
}
