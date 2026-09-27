// Test-only interposer: fix the official evaluator's srand seed, not its PRNG,
// clock, observations, scoring rules, or executable. Never linked into client.
#include <cstdlib>
#include <dlfcn.h>
#include <cstdio>
extern "C" void srand(unsigned int seed) noexcept {
    typedef void (*Seed)(unsigned int);
    static Seed original = reinterpret_cast<Seed>(dlsym(RTLD_NEXT, "srand"));
    const char* fixed = std::getenv("RDFW_TEST_SEED");
    if (fixed) std::fprintf(stderr, "[RDFW_TEST_SEED] %s\n", fixed);
    original(fixed ? static_cast<unsigned int>(std::strtoul(fixed, nullptr, 10)) : seed);
}
