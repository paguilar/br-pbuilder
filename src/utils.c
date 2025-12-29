/**
 * @file utils.c
 * @brief Misc funcs for logging, debugging, etc.
 * 
 * Copyright (C) 2022 Pedro Aguilar <paguilar@paguilar.org>
 * Released under the terms of the GNU GPL v2.0.
 */

#include "utils.h"

/**
 * @brief Get the number of available processor using glib2
 */
gushort cpu_get_num()
{
    return g_get_num_processors();
}

/**
 * @brief Return the elapsed time formatted in a friendly hours, minutes and seconds string
 * @elapsed_secs The time in seconds to be formatted
 * @return A GString with the formatted time
 */
GString * elapsed_time_nice_output(gdouble elapsed_secs)
{
    guint       hrs,
                mins,
                secs;
    GString *   elapsed_time_str;

    elapsed_time_str = g_string_new(NULL);

    if ((elapsed_secs / 3600) > UINT_MAX)
        return elapsed_time_str;

    hrs = (elapsed_secs / 3600);
    mins = (((unsigned int)elapsed_secs % 3600) / 60);
    secs = (unsigned int)((unsigned int)(elapsed_secs) % 3600) % 60;

    g_string_append_printf(elapsed_time_str, "%uh ", hrs);
    g_string_append_printf(elapsed_time_str, "%um ", mins);
    g_string_append_printf(elapsed_time_str, "%us", secs);

    return elapsed_time_str;
}

/**
 * @brief Print a log according to its level. Each level has its own color
 * @param fmt The format string
 */
void pb_log(PBLogType log_type, gchar *fmt, ...)
{
    va_list ap; 

    if (fmt) {
        va_start(ap, fmt);
        switch (log_type) {
            case PB_INFO:
                printf("%s", C_GREEN);
                vprintf(fmt, ap);
                printf("%s", C_NORMAL);
                break;
            case PB_WARN:
                printf("%s", C_YELLOW);
                vprintf(fmt, ap);
                printf("%s", C_NORMAL);
                break;
            case PB_ERR:
            default:
                printf("%s", C_RED);
                vprintf(fmt, ap);
                printf("%s", C_NORMAL);
                break;
        }
        va_end(ap);
    }
    else
        printf("%s: ERR: Unknown error!\n", PBUILDER_NAME);
}

/**
 * @brief Print a string only when debug level is equal or greater than the
 * level given in the cmdline and the module is equal to the module given in the cmdline.
 * @param level The debug level
 * @param module The module string
 * @param fmt The format string
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

