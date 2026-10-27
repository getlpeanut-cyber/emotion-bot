/* 宿主机验证 eaf.c 解码正确性：解 angry_20s/smile_static 若干帧到原生 390x390 大端 RGB565 */
#define EAF_HOST_TEST
#include "../lib/expressions/eaf.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int dump(const char *path, const uint8_t *data, size_t len)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        return -1;
    }
    fwrite(data, 1, len, fp);
    fclose(fp);
    return 0;
}

int main(int argc, char **argv)
{
    const char *bin_path = argc > 1 ? argv[1]
        : "D:/project_entire/project/emotion_bot/data/emote-assets.bin";
    FILE *fp = fopen(bin_path, "rb");
    if (!fp) {
        fprintf(stderr, "open bin failed\n");
        return 1;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *d = (uint8_t *)malloc((size_t)sz);
    if (fread(d, 1, (size_t)sz, fp) != (size_t)sz) {
        fprintf(stderr, "read bin failed\n");
        return 1;
    }
    fclose(fp);

    if (memcmp(d, "MMAP", 4) != 0) {
        fprintf(stderr, "not MMAP\n");
        return 1;
    }

    uint32_t name_len = rd32(d + 8), count = rd32(d + 12);
    uint32_t pos = 32;
    uint32_t data_base = pos + count * (name_len + 12);

    eaf_assets_t a;
    memset(&a, 0, sizeof(a));
    for (uint32_t i = 0; i < count && i < 16; i++) {
        a.names[i] = (const char *)(d + pos);
        uint32_t size = rd32(d + pos + name_len);
        uint32_t off = rd32(d + pos + name_len + 4);
        a.files[i] = d + data_base + off + 2;
        a.file_sizes[i] = size;
        pos += name_len + 12;
    }
    a.file_count = (int)count;

    struct { const char *file; int frames[3]; int n; } jobs[] = {
        { "angry_20s.eaf", { 0, 30, 33 }, 3 },
        { "smile_static.eaf", { 0, 12 }, 2 },
    };

    for (size_t j = 0; j < sizeof(jobs) / sizeof(jobs[0]); j++) {
        eaf_anim_t anim;
        if (!eaf_anim_open(&a, jobs[j].file, &anim)) {
            fprintf(stderr, "anim_open failed: %s\n", jobs[j].file);
            return 1;
        }
        uint8_t *out = (uint8_t *)malloc((size_t)anim.width * anim.height * 2);
        for (int k = 0; k < jobs[j].n; k++) {
            int fi = jobs[j].frames[k];
            if (!eaf_decode_frame(&anim, fi, out, anim.width, anim.height)) {
                fprintf(stderr, "decode failed: %s frame %d\n", jobs[j].file, fi);
                return 1;
            }
            char path[256];
            snprintf(path, sizeof(path), "tools/_png/%s_%d.raw", jobs[j].file, fi);
            if (dump(path, out, (size_t)anim.width * anim.height * 2) != 0) {
                fprintf(stderr, "dump failed\n");
                return 1;
            }
            printf("decoded %s frame %d (%dx%d)\n", jobs[j].file, fi, anim.width, anim.height);
        }
        free(out);
    }

    printf("OK\n");
    return 0;
}
