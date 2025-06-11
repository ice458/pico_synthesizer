#ifndef COMMON_FUNCTION_H
#define COMMON_FUNCTION_H

#include <stdint.h>

typedef int16_t fp_t; // Fixed-point type, 16-bit signed integer
#define fp_mul(a, b) ((int16_t)(((int32_t)(a) * (int32_t)(b)) >> 15))
#define fp_div(a, b) ((int16_t)(((int32_t)(a) << 15) / (int32_t)(b)))
#define fp_to_float(a) ((float)(a) / 32768.0f)
#define float_to_fp(a) ((int16_t)((a) * 32768.0f))
#define FP_MAX ((int16_t)32767)
#define FP_MIN ((int16_t)-32768)

typedef union
{
    uint32_t u32; // Access as a single 32-bit unsigned integer
    struct
    {
        fp_t left;  // Left channel (16-bit)
        fp_t right; // Right channel (16-bit)
    } ch;
} stereo_t;

typedef int32_t q8_t;
#define q8_to_int32_t(a) (a >> 8)
#define int32_t_to_q8(a) (a << 8)
#define q8_to_float(a) ((float)(a) / 256.0f)
#define float_to_q8(a) ((int32_t)((a) * 256.0f))
#define q8_mul(a, b) ((int32_t)(((int64_t)(a) * (int64_t)(b)) >> 8))
#define q8_div(a, b) ((int32_t)(((int64_t)(a) << 8) / (int64_t)(b)))
#define Q8_MAX ((int32_t)2147483647)
#define Q8_MIN ((int32_t)-2147483648)

#endif // COMMON_FUNCTION_H