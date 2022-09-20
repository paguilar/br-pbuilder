/**
 * @file utils.c
 * @brief Misc funcs for logging, debugging, etc.
 * 
 * Copyright (C) 2022 Pedro Aguilar <paguilar@paguilar.org>
 * Released under the terms of the GNU GPL v2.0.
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <glib.h>
#include <glib/gprintf.h>

#include "utils.h"

gushort cpu_get_num()
{
    return g_get_num_processors();
}

/**
 * @brief Clear a buffer with '\\0'
 * @param buff The buffer to be cleared
 * @param size The size of the buffer
 */
void reset_buff(gchar *buff, guint size)
{
    guint i;

    for (i = 0; i < size; i++)
        buff[i] = '\0';
}

/**
 * @brief
 * @fmt The format string
 */
void pb_log(GPLogType log_type, gchar *fmt, ...)
{
    va_list ap; 
    gchar buff[BUFF_1K];

    if (fmt) {
        va_start(ap, fmt);
        g_vsnprintf(buff, BUFF_1K, fmt, ap);
        va_end(ap);
        switch (log_type) {
            case GP_INFO:
                printf("%s: INFO: %s\n", PBUILDER_NAME, buff);
                break;
            case GP_WARN:
                printf("%s: WARNING: %s\n", PBUILDER_NAME, buff);
                break;
            case GP_ERR:
            default:
                printf("%s: ERR: %s\n", PBUILDER_NAME, buff);
                break;
        }
    }
    else
        printf("%s: ERR: Unknown error!\n", PBUILDER_NAME);
}

/**
 * @brief Print a string only when debug level is equal or greater than the
 * level given in the command line.
 * @level The debug level
 * @fmt The format string
 */
void pb_debug(guint level, gchar *module, gchar *fmt, ...)
{
    va_list ap;

    if (level <= debug_level) {
        if (!g_strcmp0(module, debug_module) || !g_strcmp0(debug_module, DBG_ALL)) {
            if (fmt) {
                va_start(ap, fmt);
                vfprintf(stdout, fmt, ap);
                va_end(ap);
            }
        }
    }
}   


