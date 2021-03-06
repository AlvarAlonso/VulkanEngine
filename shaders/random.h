// GLSL file with functions about randoms.
#ifndef VK_ENGINE_RANDOM_GENERATION_H
#define VK_ENGINE_RANDOM_GENERATION_H

// Generate a random unsigned int from two unsigned int values, using 16 pairs
// of rounds of the Tiny Encryption Algorithm. See Zafar, Olano, and Curtis,
// "GPU Random Numbers via the Tiny Encryption Algorithm"
uint tea(uint val0, uint val1)
{
	uint v0 = val0;
	uint v1 = val1;
	uint s0 = 0;

	for(uint n = 0; n < 16; n++)
	{
		s0 += 0x9e3779b9;
		v0 += ((v1 << 4) + 0xa341316c) ^ (v1 + s0) ^ ((v1 >> 5) + 0xc8013ea4);
		v1 += ((v0 << 4) + 0xad90777d) ^ (v0 + s0) ^ ((v0 >> 5) + 0x7e95761e);
	}

	return v0;
}

// Generate a random unsigned int in [0, 2^24) given the previous RNG state
// using the Numerial Recipes linear congruential generator
uint lcg(inout uint prev)
{
	uint LCG_A = 1664525u;
	uint LCG_C = 1013904223u;
	prev = (LCG_A * prev + LCG_C);
	return prev & 0x00FFFFFF;
}

// Generate a random float in [0, 1) given the previous RNG state
float rnd(inout uint prev)
{
	return (float(lcg(prev)) / float(0x01000000));
}

// Steps the RNG and returns a floating-point value between 0 and 1 inclusive.
float nextRand(inout uint rngState)
{
	// Condensed version of pcg_output_rxs_m_xs_32_32, with simple conversion to floating-point [0,1].
	rngState = rngState * 747796405 + 1;
	uint word = ((rngState >> ((rngState >> 28) + 4)) ^ rngState) * 277803737;
	word = (word >> 22) ^ word;
	return float(word) / 4294967295.0f;
}

#endif