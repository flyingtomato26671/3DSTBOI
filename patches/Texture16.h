#pragma once

namespace IsaacOld {
    extern "C" {
    int SwizzleTexture(unsigned int format, unsigned int width, unsigned int height,
        const unsigned char *source, unsigned short *destination);
    unsigned short *PixelAddress(unsigned char *texture, unsigned int x, unsigned int y);
    float *ReadPixel(float *result, unsigned char *texture, unsigned int x, unsigned int y);
    int WritePixel(unsigned char *texture, unsigned int x, unsigned int y, const float *color);
    int UploadTexels(unsigned char *texture, unsigned char **source);
    unsigned char *AllocateProceduralScratch(unsigned int allocator, unsigned int size);
    void BindTexture(unsigned char *texture, unsigned char *state, unsigned int unit);
    }
}
