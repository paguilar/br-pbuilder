/**
 * @file utils.h
 * @brief Misc functions used all around the project.
 * 
 * Copyright (C) 2022 Pedro Aguilar <paguilar@paguilar.org>
 * Released under the terms of the GNU GPL v2.0.
 *
 */

#ifndef _UTILS_H_
#define _UTILS_H_

#include <glib.h>
#include <syslog.h>

/* 
 * GCC 10 defaults to -fno-common, which means a linker error is reported if
 * extern is ommited when declaring global variables in a header file that is
 * included by several files. To fix this, use extern in header files and ensure
 * each global is defined in exactly one C file.
 */
extern gint    debug_level;        /**< Set debug level. Values: [0-3]. Default: 0 */
extern gchar   *debug_module;      /**< Set module to debug. Values: [all]. Default: all */
extern gchar   *deps_file;         /**< Filename given in the cmdline */
extern gint    cpu_num;            /**< Max number of CPU used to build */

#define PBUILDER_NAME   "pbuilder"
#define PBUILDER_DESC   "Buildroot utility that builds packages in parallel using an acyclic graph"

#define BUFF_1K         1024
#define BUFF_4K         4096
#define BUFF_8K         8192

#define DBG_ALL         "all"
#define DBG_CREATE      "creation"
#define DBG_EXEC        "execution"
#define DBG_NONE        "none"

#define C_NORMAL        "\x1B[0m"
#define C_RED           "\x1B[31m"
#define C_GREEN         "\x1B[32m"
#define C_YELLOW        "\x1B[33m"
#define C_BLUE          "\x1B[34m"
#define C_MAGENT        "\x1B[35m"
#define C_CYAN          "\x1B[36m"
#define C_WHITE         "\x1B[37m"

/**
 * Return types
 */
typedef enum
{
    GP_OK = 0,			/**< No error, everything was OK */
    GP_FAIL,            /**< Error, something went wrong */
} GPResult;


typedef enum
{
    GP_INFO,
    GP_WARN,
    GP_ERR
} GPLogType;

void        reset_buff(gchar *, guint);
void        pb_log(GPLogType, gchar *, ...);
void        pb_debug(guint, gchar *, gchar *, ...);

#endif /* _UTILS_H_ */
