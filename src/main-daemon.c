/**
 * @file main.c
 *
 * @brief the main application
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "msiklm.h"

/**
 * @brief prints help information
 */
void show_help()
{
}

//['us', 'ni', 'sy', 'id', 'wa', 'hi', 'si', 'st']
struct stat_entry {
    unsigned long user;
    unsigned long nice;
    unsigned long sys;
    unsigned long idle;
    unsigned long iowait;
    unsigned long irq;
    unsigned long softirq;
    unsigned long steal;
    unsigned long guest;
    unsigned long guest_nice;
};

static void read_proc_stat(struct stat_entry *out)
{
    int ret = EXIT_SUCCESS;

    assert(out);

    FILE *proc_stat_hdl = fopen("/proc/stat", "r");
    assert(proc_stat_hdl);

    ret = fscanf(proc_stat_hdl, "cpu  %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
           &out->user,
           &out->nice,
           &out->sys,
           &out->idle,
           &out->iowait,
           &out->irq,
           &out->softirq,
           &out->steal,
           &out->guest,
           &out->guest_nice);
    if (ret < 0) {
        fprintf(stderr, "fscanf() failed.\n");
        exit(EXIT_FAILURE);
    }

    fclose(proc_stat_hdl);
}

static void stat_entry_calc_delta(const struct stat_entry *curr,
                                  const struct stat_entry *prev,
                                  unsigned long *usage,
                                  unsigned long *total)
{
    assert(prev);
    assert(curr);
    assert(usage);
    assert(total);

    unsigned long d_us = curr->user - prev->user;
    unsigned long d_ni = curr->nice - prev->nice;
    unsigned long d_sy = curr->sys - prev->sys;
    unsigned long d_id = curr->idle - prev->idle;
    unsigned long d_io = curr->iowait - prev->iowait;
    unsigned long d_hi = curr->irq - prev->irq;
    unsigned long d_si = curr->softirq - prev->softirq;
    unsigned long d_st = curr->steal - prev->steal;
    unsigned long d_gu = curr->guest - prev->guest;
    unsigned long d_gn = curr->guest_nice - prev->guest_nice;

    //printf("us: %lu ni: %lu sy: %lu id: %lu ", d_us, d_ni, d_sy, d_id);

    *usage = d_us + d_ni + d_sy;
    *total = d_us + d_ni + d_sy + d_id + d_io
           + d_hi + d_si + d_st + d_gu + d_gn;
}

typedef struct RgbColor
{
    unsigned char r;
    unsigned char g;
    unsigned char b;
} RgbColor;

typedef struct HsvColor
{
    unsigned char h;
    unsigned char s;
    unsigned char v;
} HsvColor;

static RgbColor HsvToRgb(HsvColor hsv)
{
    RgbColor rgb;
    unsigned char region, remainder, p, q, t;

    if (hsv.s == 0)
    {
        rgb.r = hsv.v;
        rgb.g = hsv.v;
        rgb.b = hsv.v;
        return rgb;
    }

    region = hsv.h / 43;
    remainder = (hsv.h - (region * 43)) * 6; 

    p = (hsv.v * (255 - hsv.s)) >> 8;
    q = (hsv.v * (255 - ((hsv.s * remainder) >> 8))) >> 8;
    t = (hsv.v * (255 - ((hsv.s * (255 - remainder)) >> 8))) >> 8;

    switch (region)
    {
        case 0:
            rgb.r = hsv.v; rgb.g = t; rgb.b = p;
            break;
        case 1:
            rgb.r = q; rgb.g = hsv.v; rgb.b = p;
            break;
        case 2:
            rgb.r = p; rgb.g = hsv.v; rgb.b = t;
            break;
        case 3:
            rgb.r = p; rgb.g = q; rgb.b = hsv.v;
            break;
        case 4:
            rgb.r = t; rgb.g = p; rgb.b = hsv.v;
            break;
        default:
            rgb.r = hsv.v; rgb.g = p; rgb.b = q;
            break;
    }

    return rgb;
}

# if 0
static HsvColor RgbToHsv(RgbColor rgb)
{
    HsvColor hsv;
    unsigned char rgbMin, rgbMax;

    rgbMin = rgb.r < rgb.g ? (rgb.r < rgb.b ? rgb.r : rgb.b) : (rgb.g < rgb.b ? rgb.g : rgb.b);
    rgbMax = rgb.r > rgb.g ? (rgb.r > rgb.b ? rgb.r : rgb.b) : (rgb.g > rgb.b ? rgb.g : rgb.b);

    hsv.v = rgbMax;
    if (hsv.v == 0)
    {
        hsv.h = 0;
        hsv.s = 0;
        return hsv;
    }

    hsv.s = 255 * (rgbMax - rgbMin) / hsv.v;
    if (hsv.s == 0)
    {
        hsv.h = 0;
        return hsv;
    }

    if (rgbMax == rgb.r)
        hsv.h = 0 + 43 * (rgb.g - rgb.b) / (rgbMax - rgbMin);
    else if (rgbMax == rgb.g)
        hsv.h = 85 + 43 * (rgb.b - rgb.r) / (rgbMax - rgbMin);
    else
        hsv.h = 171 + 43 * (rgb.r - rgb.g) / (rgbMax - rgbMin);

    return hsv;
}
#endif

/**
 * @brief application's entry point
 * @param argc number of command line arguments
 * @param argv command line argument array; the first value is always the program's name
 * @return 0 if everything succeeded, -1 otherwise
 */
int main(int argc, char** argv)
{
    int ret = EXIT_SUCCESS;
    struct color colors[7];
    int num_regions = 3;
    struct stat_entry stat_prev, stat_curr;

    (void) argc;
    (void) argv;

    hid_device* dev = open_keyboard();
    if (!dev)
    {
        fprintf(stderr, "Fail opening MSI LED keyboard.\n");
        exit(EXIT_FAILURE);
    }

    enum brightness br = high;
    enum mode md = normal;
    set_mode(dev, md);

    read_proc_stat(&stat_prev);
    sleep(1);

    unsigned long use;
    unsigned long tot;
    bool deamon = true;
    while (deamon && !ret) {
        read_proc_stat(&stat_curr);

        stat_entry_calc_delta(&stat_curr, &stat_prev, &use, &tot);

        float ratio = ((float)use) / ((float)tot); /** [0..1] interval */
        //float cpu_percent = ratio * 100.0f;
        //printf(" cpu: %3.2f %%\n", cpu_percent);

        HsvColor load_in_hsv = {
            .h = 20, /** Color hue */
            .s = (unsigned char)roundf(sqrtf(ratio) * 255.f),
            .v = (unsigned char)255,
        };

        RgbColor load_in_rgb = HsvToRgb(load_in_hsv);
        colors[0].red =   (unsigned char)(load_in_rgb.r);
        colors[0].green = (unsigned char)(load_in_rgb.g);
        colors[0].blue =  (unsigned char)(load_in_rgb.b);
        colors[2] = colors[1] = colors[0];

        for (int i=0; i<num_regions && ret == 0; ++i)
            if (set_color(dev, colors[i], i+1, br) <= 0)
                ret = -1;

        stat_prev = stat_curr;
        sleep(1);
    }

    hid_close(dev);

    return ret;
}
