
#include "noise_remover.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "timing.h"

#ifdef _WIN32

#include <Windows.h>

#else
#include <unistd.h>
#endif

//采用https://github.com/mackron/dr_libs/blob/master/dr_wav.h 解码
#define DR_WAV_IMPLEMENTATION

#include "dr_wav.h"

#ifndef nullptr
#define nullptr 0
#endif

#ifndef MIN
#define MIN(A, B)        ((A) < (B) ? (A) : (B))
#endif

#ifndef MAX
#define MAX(A, B)        ((A) > (B) ? (A) : (B))
#endif

//写wav文件
void wavWrite_int16(char *filename, short *buffer, size_t sampleRate, size_t totalSampleCount) {
    drwav_data_format format = {};
    format.container = drwav_container_riff;     // <-- drwav_container_riff = normal WAV files, drwav_container_w64 = Sony Wave64.
    format.channels = 1;
    format.sampleRate = (drwav_uint32) sampleRate;
    format.bitsPerSample = sizeof(short) * 8;
    format.format = DR_WAVE_FORMAT_PCM;

    drwav *pWav = drwav_open_file_write(filename, &format);
    if (pWav) {
        drwav_uint64 samplesWritten = drwav_write(pWav, totalSampleCount, buffer);
        drwav_uninit(pWav);
        if (samplesWritten != totalSampleCount) {
            fprintf(stderr, "ERROR\n");
            exit(1);
        }
    }
}

//读取wav文件
short *wavRead_int16(char *filename, uint32_t *sampleRate, uint64_t *totalSampleCount) {
    unsigned int channels;
    short *buffer = drwav_open_and_read_file_s16(filename, &channels, sampleRate,
                                                 totalSampleCount);
    if (buffer == nullptr) {
        printf("读取wav文件失败.");
    }
    //仅仅处理单通道音频
    if (channels != 1) {
        drwav_free(buffer);
        buffer = nullptr;
        *sampleRate = 0;
        *totalSampleCount = 0;
    }
    return buffer;
}

//分割路径函数
void splitpath(const char *path, char *drv, char *dir, char *name, char *ext) {
    const char *end;
    const char *p;
    const char *s;
    if (path[0] && path[1] == ':') {
        if (drv) {
            *drv++ = *path++;
            *drv++ = *path++;
            *drv = '\0';
        }
    } else if (drv)
        *drv = '\0';
    for (end = path; *end && *end != ':';)
        end++;
    for (p = end; p > path && *--p != '\\' && *p != '/';)
        if (*p == '.') {
            end = p;
            break;
        }
    if (ext)
        for (s = end; (*ext = *s++);)
            ext++;
    for (p = end; p > path;)
        if (*--p == '\\' || *p == '/') {
            p++;
            break;
        }
    if (name) {
        for (s = p; s < end;)
            *name++ = *s++;
        *name = '\0';
    }
    if (dir) {
        for (s = path; s < p;)
            *dir++ = *s++;
        *dir = '\0';
    }
}

int freeNross(short *buffer, size_t SampleCount) {
    struct noise_remover_s nrm;

    uint32_t processed;
    /**** initialize variables ****/
    int err = noise_remover_init(&nrm);
    if (err < 0) {
        printf("error: noise_remover_init() failed\n");
        return -1;
    }
    /**** process audio sample by sample, make noise reduction ****/
    printf("processing with free-nross...\n");
    processed = 0;
    for (int idx = 0; idx < SampleCount; ++idx) {
        int16_t x = buffer[idx];
        /* process audio */
        int t = -2;
        x = ASHIFT16(x, t);
        int16_t y = noise_remover(&nrm, x, 1);  /* training=1 */
        if (y > 8192)
            y = 32767;
        else if (y < -8192)
            y = -32768;
        else
            y = ASHIFT16(y, 2);
        buffer[idx] = y;
        processed++;
    }
    printf("%u samples has been successfully processed\n", processed);
}

void resampleData(const int16_t *sourceData, int32_t sampleRate, uint32_t srcSize, int16_t *destinationData,
                  int32_t newSampleRate) {
    if (sampleRate == newSampleRate) {
        memcpy(destinationData, sourceData, srcSize * sizeof(int16_t));
        return;
    }
    uint32_t last_pos = srcSize - 1;
    uint32_t dstSize = (uint32_t) (srcSize * ((float) newSampleRate / sampleRate));
    for (uint32_t idx = 0; idx < dstSize; idx++) {
        float index = ((float) idx * sampleRate) / (newSampleRate);
        uint32_t p1 = (uint32_t) index;
        float coef = index - p1;
        uint32_t p2 = (p1 == last_pos) ? last_pos : p1 + 1;
        destinationData[idx] = (int16_t) ((1.0f - coef) * sourceData[p1] + coef * sourceData[p2]);
    }
}

void deNoise(char *in_file, char *out_file) {
    uint32_t in_sampleRate = 0;
    uint64_t inSampleCount = 0;
    short *data_in = wavRead_int16(in_file, &in_sampleRate, &inSampleCount);
    uint32_t out_sampleRate = 8000;
    uint32_t out_size = (uint32_t) (inSampleCount * ((float) out_sampleRate / in_sampleRate));
    int16_t *data_out = (int16_t *) malloc(out_size * sizeof(int16_t));

    if (data_in != NULL && data_out != NULL) {
        resampleData(data_in, in_sampleRate, (uint32_t) inSampleCount, data_out, out_sampleRate);
        double startTime = now();
        freeNross(data_out, out_size);
        double time_interval = calcElapsed(startTime, now());
        printf("time interval: %d ms\n ", (int) (time_interval * 1000));
        wavWrite_int16(out_file, data_out, out_sampleRate, (uint32_t) out_size);
        free(data_in);
        free(data_out);
    } else {
        if (data_in) free(data_in);
        if (data_out) free(data_out);
    }
}

int main(int argc, char *argv[]) {
    printf("Free noise reduction of speech signals\n");
    printf("blog:http://cpuimage.cnblogs.com/\n");
    if (argc < 2)
        return -1;
    char *in_file = argv[1];
    char drive[3];
    char dir[256];
    char fname[256];
    char ext[256];
    char out_file[1024];
    splitpath(in_file, drive, dir, fname, ext);
    sprintf(out_file, "%s%s%s_out%s", drive, dir, fname, ext);
    deNoise(in_file, out_file);

    printf("press any key to exit.\n");
    getchar();
    return 0;
}

