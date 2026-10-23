#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define LOG_NAME "skycircuit.log"
#define HDD_LOG  "/dev_hdd0/tmp/" LOG_NAME

static FILE *g_hdd;
static FILE *g_usb;
static int g_ready;

static int try_open_usb(void)
{
    /* Prefer common HEN/CFW USB mount points */
    static const char *roots[] = {
        "/dev_usb000/",
        "/dev_usb001/",
        "/dev_usb002/",
        "/dev_usb003/",
        "/dev_usb004/",
        "/dev_usb005/",
        "/dev_usb006/",
        "/dev_usb007/",
        NULL
    };
    char path[96];
    int i;
    for (i = 0; roots[i]; i++) {
        struct stat st;
        if (stat(roots[i], &st) != 0)
            continue;
        snprintf(path, sizeof(path), "%s%s", roots[i], LOG_NAME);
        g_usb = fopen(path, "a");
        if (g_usb) {
            setvbuf(g_usb, NULL, _IONBF, 0);
            return 1;
        }
    }
    return 0;
}

void log_init(void)
{
    if (g_ready)
        return;
    g_hdd = fopen(HDD_LOG, "a");
    if (g_hdd)
        setvbuf(g_hdd, NULL, _IONBF, 0);
    try_open_usb();
    g_ready = 1;
    log_msg("=== Sky Circuit log start ===");
}

void log_msg(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    int n;

    if (!g_ready)
        log_init();

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if (n >= (int)sizeof(buf))
        n = (int)sizeof(buf) - 1;
    buf[n] = '\0';

    if (g_hdd) {
        fputs(buf, g_hdd);
        fputc('\n', g_hdd);
        fflush(g_hdd);
    }
    if (g_usb) {
        fputs(buf, g_usb);
        fputc('\n', g_usb);
        fflush(g_usb);
    }
}

void log_flush(void)
{
    if (g_hdd)
        fflush(g_hdd);
    if (g_usb)
        fflush(g_usb);
}
