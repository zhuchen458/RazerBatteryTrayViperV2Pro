/* These are also called by the compiler for aggregate copies and initialization.
   Compile this file with -fno-builtin to keep the executable independent of a CRT. */
#include <stddef.h>

void *memcpy(void *destination, const void *source, size_t length)
{
    unsigned char *out = destination;
    const unsigned char *in = source;
    for (size_t index = 0; index < length; ++index)
        out[index] = in[index];
    return destination;
}

void *memset(void *destination, int value, size_t length)
{
    unsigned char *out = destination;
    for (size_t index = 0; index < length; ++index)
        out[index] = (unsigned char)value;
    return destination;
}
