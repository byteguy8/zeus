#include "splitmix64.h"

SplitMix64 splitmix64_init(uint64_t value){
    return (SplitMix64){.x = value};
}

uint64_t splitmix64_next(SplitMix64 *sm64){
    uint64_t z = (sm64->x += 0x9e3779b97f4a7c15);
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
	z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
	return z ^ (z >> 31);
}