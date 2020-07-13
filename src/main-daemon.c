/**
 * @file main-deamon.c
 *
 * @brief The main MSI Keyboard Light Manager daemon source file.
 */

#include <assert.h>
#include <getopt.h>
#include <libgen.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "msiklm.h"

#define NUM_REGIONS 3

static bool daemon_running = true;

static unsigned char hue = 20;

static bool dry_run = false;

/**
 * @brief SIGTERM signal handler
 */
static void sigterm_handler(int n)
{
    (void) n;
    daemon_running = false;
}

/**
 * @brief System statistics.
 *
 * Represents cpu time spend in different mode.
 *
 * @note: man 5 proc, and search for "/proc/stat" definitions.
 * @note: Compute cpu usage sample:
 *  https://supportcenter.checkpoint.com/supportcenter/portal?eventSubmit_doGoviewsolutiondetails=&solutionid=sk65143
 */
struct stat_entry {
    unsigned long user;         /**< Time spent in user mode. */
    unsigned long nice;         /**< Time spent in user mode with low priority (nice). */
    unsigned long sys;          /**< Time spent in system mode. */
    unsigned long idle;         /**< Time spent in the idle task. */
    unsigned long iowait;       /**< Time waiting for I/O to complete. */
    unsigned long irq;          /**< Time servicing interrupts. */
    unsigned long softirq;      /**< Time servicing softirqs. */
    unsigned long steal;        /**< Stolen time, which is the time spent in other operating systems when running in a virtualized environment. */
    unsigned long guest;        /**< Time spent running a virtual CPU for guest operating systems under the control of the Linux kernel. */
    unsigned long guest_nice;   /**< Time spent running a niced guest (virtual CPU for guest operating systems under the control of the Linux kernel. */
};

/**
 * @brief Color definition using Red Green Blue components.
 */
typedef struct rgb_color
{
    unsigned char r;
    unsigned char g;
    unsigned char b;
} rgb_color_t;

/**
 * @brief Color definition using Hue Saturation Value components.
 */
typedef struct hsv_color
{
    unsigned char h;
    unsigned char s;
    unsigned char v;
} hsv_color_t;

/**
 * @brief Read system statistics from procfs counters.
 *
 * @param[out]  out  Pointer on stat_entry object to update.
 */
static void read_proc_stat(struct stat_entry *out)
{
    int ret = EXIT_SUCCESS;
    static FILE *proc_stat_hdl = NULL;

    assert(out);

    /* static FILE *proc_stat_hdl = fopen("/proc/stat", "r"); */
    if (!proc_stat_hdl)
        proc_stat_hdl = fopen("/proc/stat", "r");
    assert(proc_stat_hdl);
    fflush(proc_stat_hdl);

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

    rewind(proc_stat_hdl);
    /* fclose(proc_stat_hdl); */
}

/**
 * @brief Compute statistics differential between two measurements.
 *
 * @param[in]   curr   Pointer on latest stat_entry object.
 * @param[in]   prev   Pointer on earliest stat_entry object.
 * @param[out]  usage  Computed time spend between earliest and latest
 *                     measurements in user, nice and system mode.
 * @param[out]  total  Computed time spend between earliest and latest
 *                     measurements in all mode.
 */
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

    /* printf("us: %lu ni: %lu sy: %lu id: %lu ", d_us, d_ni, d_sy, d_id); */

    *usage = d_us + d_ni + d_sy;
    *total = d_us + d_ni + d_sy + d_id + d_io
           + d_hi + d_si + d_st + d_gu + d_gn;
}

/**
 * @brief Convert color from HSV to RGB colorspace.
 *
 * @param[in]   hsv   Color represented in HSV format.
 *
 * @return  Color represented in RGV format.
 */
static rgb_color_t hsv2rgb(hsv_color_t hsv)
{
    rgb_color_t rgb;
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

/**
 * @brief Prints help information.
 */
static void usage(char *argv[])
{
    printf("%s - MSI Keyboard Light Manager daemon", basename(argv[0]));
    puts("");
    printf("Usage:\%s [-h] [-c h,s,v] [-n]", basename(argv[0]));
    puts("");
    puts("Description:");
    puts("");
    puts("\tThis deamon periodically monitors the load average of the current system");
    puts("\tand adapts the keyboard color accordingly. Color saturation variates from");
    puts("\tno saturation (white color) when system is idle, to full saturation when cpu");
    puts("\tload is 100 %.");
    puts("");
    puts("Options:");
    puts("\t-h, --help\t\tDisplay this help message.");
    puts("\t-c <hue>");
    puts("\t--color=<hue>\t\tDefines full load hue color. Value must be in [0..255].");
    printf("\t\t\t\tDefault hue value is %d (i.e. orange color).\n", hue);
    puts("\t-n, --dry-run\t\tSet keyboard color without starting the deamon.");
}

/**
 * @brief Parse command line arguments.
 */
static void parse_args(int argc, char *argv[])
{
    int c, col;

    while (1) {
        int option_index = 0;
        static struct option long_options[] =
        {
          {"help", 0, 0, 'h'},
          {"color", 1, 0, 'c'},
          {"dry-run", 0, 0, 'n'},
          {0, 0, 0, 0}
        };

        c = getopt_long(argc, argv, "hc:n",
                        long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {

        default:
        case 0: /* long option only */
            break;

        case '?': /* invalid option */
        case 'h':
            usage(argv); exit(EXIT_SUCCESS);
            break;

        case 'c':
            col = atoi(optarg);
            if (c > 255)
                c = 255;
            else if (c < 0)
                c = 0;
            hue = (unsigned char)col;
            break;

        case 'n':
            dry_run = true;
            break;
        }
    }
}

/**
 * @brief Update led from 0 to 100% and then 0% saturation, during approximately one second.
 */
static int blink_test()
{
    int ret = EXIT_SUCCESS;
    struct color colors;
    int num_regions = NUM_REGIONS;

    hid_device* dev = open_keyboard();
    if (!dev) {
        fprintf(stderr, "open_keyboard() failed\n");
        exit(EXIT_FAILURE);
    }

    enum brightness br = rgb;
    enum mode md = normal;
    set_mode(dev, md);

    for (unsigned char sat = 0, dir = 0; dir < 8; ++dir) {
        sat = (dir & 1) ? 255 : 0;

        hsv_color_t load_in_hsv = {
            .h = hue, /** Color hue */
            .s = (unsigned char)(dir ? 255 - sat : sat),
            .v = (unsigned char)255,
        };

        rgb_color_t load_in_rgb = hsv2rgb(load_in_hsv);
        colors.red   = (unsigned char)(load_in_rgb.r);
        colors.green = (unsigned char)(load_in_rgb.g);
        colors.blue  = (unsigned char)(load_in_rgb.b);

        /* fprintf(stderr, "Set r:%d, g:%d, b:%d color.\n",
                colors.red, colors.green, colors.blue); */

        for (int i = 0; i < num_regions && ret == 0; ++i)
            if (set_color(dev, colors, i + 1, br) <= 0)
                ret = -1;

        struct timespec wait_time = {
            .tv_nsec = 500 * 1000 * 1000, /* 500 ms */
        };
        nanosleep(&wait_time, NULL);
    }

    hid_close(dev);

    return ret;
}

/**
 * @brief Application's entry point.
 *
 * @param[in]  argc  Number of command line arguments.
 * @param[in]  argv  Command line argument array; the first value is always the program's name.
 *
 * @return 0 if everything succeeded, -1 otherwise.
 */
int main(int argc, char** argv)
{
    int ret = EXIT_SUCCESS;
    int loglevel = LOG_USER;
    struct color colors;
    int num_regions = NUM_REGIONS;
    struct stat_entry stat_prev, stat_curr;

    parse_args(argc, argv);

    if (!keyboard_found())
    {
        fprintf(stderr, "Fail opening MSI LED keyboard.\n");
        exit(EXIT_FAILURE);
    }

    if (dry_run) {
        ret = blink_test();
        exit(ret);
    }

    /* Our process ID and Session ID */
    pid_t pid, sid;

    /* Fork off the parent process */
    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "fork() failed.\n");
        exit(EXIT_FAILURE);
    }
    /* If we got a good PID, then
       we can exit the parent process. */
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    /* Change the file mode mask */
    umask(0);

    /* Open any logs here */
    syslog(loglevel | LOG_INFO, "%s daemon started.", basename(argv[0]));

    /* Create a new SID for the child process */
    sid = setsid();
    if (sid < 0) {
        /* Log the failure */
        fprintf(stderr, "setsid() failed.\n");
        exit(EXIT_FAILURE);
    }

    /* Change the current working directory */
    if ((chdir("/")) < 0)
        exit(EXIT_FAILURE);

    /* Close out the standard file descriptors */
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    /* Register SIGTERM handler */
    signal(SIGTERM, sigterm_handler);

    /* Catch initial CPU usage */
    read_proc_stat(&stat_prev);
    sleep(1);

    hid_device* dev = open_keyboard();
    if (!dev) {
        syslog(loglevel | LOG_ERR, " open_keyboard() failed\n");
        ret = 1;
    }

    enum brightness br = rgb;
    enum mode md = normal;
    set_mode(dev, md);

    unsigned long use;
    unsigned long tot;
    unsigned char timeout = 10; /* seconds */
    while (daemon_running && !ret) {
        read_proc_stat(&stat_curr);

        stat_entry_calc_delta(&stat_curr, &stat_prev, &use, &tot);

        float ratio = ((float)use) / ((float)tot); /** [0..1] interval */
        /*
        float cpu_percent = ratio * 100.0f;
        printf(" cpu: %3.2f %%\n", cpu_percent);
        syslog(loglevel | LOG_DEBUG, " cpu: %3.2f %%\n", cpu_percent);
        */

        hsv_color_t load_in_hsv = {
            .h = hue, /** Color hue */
            .s = (unsigned char)roundf(sqrtf(ratio) * 255.f),
            .v = (unsigned char)255,
        };

        rgb_color_t load_in_rgb = hsv2rgb(load_in_hsv);
        colors.red   = (unsigned char)(load_in_rgb.r);
        colors.green = (unsigned char)(load_in_rgb.g);
        colors.blue  = (unsigned char)(load_in_rgb.b);

        for (int i = 0; i < num_regions && ret == 0; ++i)
            if (set_color(dev, colors, i + 1, br) <= 0)
                ret = -1;

        if (ret) {
            syslog(loglevel | LOG_ERR, "%s call to set_color() failed.", basename(argv[0]));

            hid_close(dev);
            dev = open_keyboard();
            while (!dev && timeout) {
                syslog(loglevel | LOG_ERR, "%s retry(%d) opening keyboard device.", basename(argv[0]), timeout);
                sleep(1);
                dev = open_keyboard();
                timeout--;
            }
            if (!dev) {
                syslog(loglevel | LOG_ERR, "%s too much retry... will quit.", basename(argv[0]));
            } else {
                ret = 0;
            }
        }
        stat_prev = stat_curr;
        sleep(1);
    }

    syslog(loglevel | LOG_INFO, "%s daemon exiting.", basename(argv[0]));
    hid_close(dev);

    return ret;
}
