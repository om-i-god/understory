#include "us_math.h"

static uint32_t hash3(int x, int y, int z)
{
	uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)z * 2147483647u;
	h = (h ^ (h >> 13)) * 1274126177u;
	return h ^ (h >> 16);
}

static float grad3(uint32_t h, float x, float y, float z)
{
	switch (h & 15u) {
	case 0: return x + y;
	case 1: return -x + y;
	case 2: return x - y;
	case 3: return -x - y;
	case 4: return x + z;
	case 5: return -x + z;
	case 6: return x - z;
	case 7: return -x - z;
	case 8: return y + z;
	case 9: return -y + z;
	case 10: return y - z;
	case 11: return -y - z;
	case 12: return x + y;
	case 13: return -x + y;
	case 14: return -y + z;
	default: return -y - z;
	}
}

static inline float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

float us_noise3(float x, float y, float z)
{
	int xi = (int)floorf(x), yi = (int)floorf(y), zi = (int)floorf(z);
	float xf = x - xi, yf = y - yi, zf = z - zi;
	float u = fade(xf), v = fade(yf), w = fade(zf);
	float n000 = grad3(hash3(xi, yi, zi), xf, yf, zf);
	float n100 = grad3(hash3(xi + 1, yi, zi), xf - 1, yf, zf);
	float n010 = grad3(hash3(xi, yi + 1, zi), xf, yf - 1, zf);
	float n110 = grad3(hash3(xi + 1, yi + 1, zi), xf - 1, yf - 1, zf);
	float n001 = grad3(hash3(xi, yi, zi + 1), xf, yf, zf - 1);
	float n101 = grad3(hash3(xi + 1, yi, zi + 1), xf - 1, yf, zf - 1);
	float n011 = grad3(hash3(xi, yi + 1, zi + 1), xf, yf - 1, zf - 1);
	float n111 = grad3(hash3(xi + 1, yi + 1, zi + 1), xf - 1, yf - 1, zf - 1);
	float x00 = us_lerp(n000, n100, u), x10 = us_lerp(n010, n110, u);
	float x01 = us_lerp(n001, n101, u), x11 = us_lerp(n011, n111, u);
	float y0 = us_lerp(x00, x10, v), y1 = us_lerp(x01, x11, v);
	return us_lerp(y0, y1, w) * 0.9f;
}

float us_noise2(float x, float y) { return us_noise3(x, y, 0.5f); }

float us_fbm3(float x, float y, float z, int oct)
{
	float sum = 0, amp = 0.5f, norm = 0;
	for (int i = 0; i < oct; i++) {
		sum += amp * us_noise3(x, y, z);
		norm += amp;
		x = x * 2.03f + 17.1f;
		y = y * 2.03f + 3.7f;
		z = z * 2.03f + 9.2f;
		amp *= 0.5f;
	}
	return norm > 0 ? sum / norm : 0;
}

float us_fbm2(float x, float y, int oct) { return us_fbm3(x, y, 0.37f, oct); }
