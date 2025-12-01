#include "xoshiro256.h"
#include <time.h>

static inline uint64_t rotl(const uint64_t x, int k) {
	return (x << k) | (x >> (64 - k));
}

XOShiro256 xoshiro256_init_seed(uint64_t seed){
    SplitMix64 sm64 = splitmix64_init(seed);
    return (XOShiro256){.s = {
        splitmix64_next(&sm64),
        splitmix64_next(&sm64),
        splitmix64_next(&sm64),
        splitmix64_next(&sm64)}};
}

XOShiro256 xoshiro256_init(){
    return xoshiro256_init_seed((uint64_t)time(NULL));
}

uint64_t xoshiro256_next(XOShiro256 *xos256){
    const uint64_t result = rotl(xos256->s[1] * 5, 7) * 9;

	const uint64_t t = xos256->s[1] << 17;

	xos256->s[2] ^= xos256->s[0];
	xos256->s[3] ^= xos256->s[1];
	xos256->s[1] ^= xos256->s[2];
	xos256->s[0] ^= xos256->s[3];

	xos256->s[2] ^= t;

	xos256->s[3] = rotl(xos256->s[3], 45);

	return result;
}

void xoshiro256_jump(XOShiro256 *xos256){
    static const uint64_t JUMP[] = { 0x180ec6d33cfd0aba, 0xd5a61266f0c9392c, 0xa9582618e03fc9aa, 0x39abdc4529b1661c };

	uint64_t s0 = 0;
	uint64_t s1 = 0;
	uint64_t s2 = 0;
	uint64_t s3 = 0;
	for(int i = 0; i < sizeof JUMP / sizeof *JUMP; i++)
		for(int b = 0; b < 64; b++) {
			if (JUMP[i] & UINT64_C(1) << b) {
				s0 ^= xos256->s[0];
				s1 ^= xos256->s[1];
				s2 ^= xos256->s[2];
				s3 ^= xos256->s[3];
			}
			xoshiro256_next(xos256);
		}

	xos256->s[0] = s0;
	xos256->s[1] = s1;
	xos256->s[2] = s2;
	xos256->s[3] = s3;
}

void xoshiro256_long_jump(XOShiro256 *xos256){
    static const uint64_t LONG_JUMP[] = { 0x76e15d3efefdcbbf, 0xc5004e441c522fb3, 0x77710069854ee241, 0x39109bb02acbe635 };

	uint64_t s0 = 0;
	uint64_t s1 = 0;
	uint64_t s2 = 0;
	uint64_t s3 = 0;
	for(int i = 0; i < sizeof LONG_JUMP / sizeof *LONG_JUMP; i++)
		for(int b = 0; b < 64; b++) {
			if (LONG_JUMP[i] & UINT64_C(1) << b) {
				s0 ^= xos256->s[0];
				s1 ^= xos256->s[1];
				s2 ^= xos256->s[2];
				s3 ^= xos256->s[3];
			}
			xoshiro256_next(xos256);
		}

	xos256->s[0] = s0;
	xos256->s[1] = s1;
	xos256->s[2] = s2;
	xos256->s[3] = s3;
}