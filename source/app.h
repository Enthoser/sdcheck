#pragma once

#include <switch.h>
#include <switch/runtime/pad.h>
#include <switch/services/hid.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <stdint.h>
#include <stdarg.h>
#include <sys/types.h>
#include <time.h>
#include <ctype.h>

#ifndef SDCHECK_VERSION
#define SDCHECK_VERSION "unknown"
#endif

/* libnx naming compatibility */
#ifndef HidNpadStyleSet_NpadFullKey
#define HidNpadStyleSet_NpadFullKey  HidNpadStyleTag_NpadFullKey
#endif
#ifndef HidNpadStyleSet_NpadHandheld
#define HidNpadStyleSet_NpadHandheld HidNpadStyleTag_NpadHandheld
#endif
#ifndef HidNpadStyleSet_NpadJoyDual
#define HidNpadStyleSet_NpadJoyDual  HidNpadStyleTag_NpadJoyDual
#endif
#ifndef HidNpadStyleSet_NpadJoyLeft
#define HidNpadStyleSet_NpadJoyLeft  HidNpadStyleTag_NpadJoyLeft
#endif
#ifndef HidNpadStyleSet_NpadJoyRight
#define HidNpadStyleSet_NpadJoyRight HidNpadStyleTag_NpadJoyRight
#endif

/* Constants */
#define PATH_MAX_LOCAL  2048
#define ERR_RING_MAX    16
#define LOG_RING_MAX    96
#define UI_H            28
#define UI_W            80
#define UI_INNER        (UI_W-2)

#define LARGEST_MAX     10
#define FAIL_MAX        5

typedef struct {
    uint64_t size;
    char path[256];
} LargestEntry;

/* Sample regions */
#define SAMPLE_REGION   (64u * 1024u)

/* ANSI Console colors */
#define C_RESET  "\x1b[0m"
#define C_BOLD   "\x1b[1m"
#define C_DIM    "\x1b[2m"
#define C_GRAY   "\x1b[2m\x1b[37m" /* dim white/gray */
#define C_RED    "\x1b[31m"
#define C_GREEN  "\x1b[32m"
#define C_YELLOW "\x1b[33m"
#define C_CYAN   "\x1b[36m"
#define C_WHITE  "\x1b[97m"

#define UI_HEADER_H 5
#define UI_CONTENT_Y (UI_HEADER_H + 1)
#define UI_CONTENT_H (UI_H - UI_HEADER_H)
