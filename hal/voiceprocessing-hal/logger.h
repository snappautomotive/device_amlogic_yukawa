/*
 * Copyright 2017 Knowles Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _LOGGER_H_
#define _LOGGER_H_

#include <stdio.h>

#define DEBUG_LVL       (0x1F)

#define DEBUG_VERBOSE   (1<<0)
#define DEBUG_INFO      (1<<1)
#define DEBUG_DEBUG     (1<<2)
#define DEBUG_WARNING   (1<<3)
#define DEBUG_ERROR     (1<<4)

#define PRINT(x, y, ...) if (DEBUG_LVL & x) {printf(y "\n", ##__VA_ARGS__);}

#define PRINTV(x...) PRINT(DEBUG_VERBOSE, x)
#define PRINTI(x...) PRINT(DEBUG_INFO, x)
#define PRINTE(x...) PRINT(DEBUG_ERROR, x)
#define PRINTW(x...) PRINT(DEBUG_WARNING, x)
#define PRINTD(x...) PRINT(DEBUG_DEBUG, x)

#define ALOGE(...) PRINTE(__VA_ARGS__)
#define ALOGW(...) PRINTW(__VA_ARGS__)
#define ALOGI(...) PRINTI(__VA_ARGS__)
#define ALOGV(...) PRINTV(__VA_ARGS__)
#define ALOGD(...) PRINTD(__VA_ARGS__)

#endif // #ifndef _LOGGER_H_
