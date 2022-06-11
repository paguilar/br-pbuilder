/**
 * @file utils.h
 * @brief Misc functions used all around the project.
 * 
 * Author: 
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

#define GPBUILD_NAME    "GPBuild"
#define GPBUILD_DESC    "Utilty that builds Buildroot packages following a graph-like pattern: make gpbuild"

#define BUFF_1024       1024
#define BUFF_4096       4096

#define DBG_ALL         "all"
#define DBG_CREATE      "creation"
#define DBG_EXEC        "execution"
#define DBG_NONE        "none"

/**
 * Return types
 */
typedef enum
{
    GP_OK = 0,          /**< No error, everything was OK */
    GP_FAIL,           /**< Error, something went wrong */
} GPResult;


typedef enum
{
    GP_INFO,
    GP_WARN,
    GP_ERR
} GPLogType;

void        reset_buff(gchar *, guint);
void        pg_log(GPLogType, gchar *, ...);
void        pg_debug(guint, gchar *, gchar *, ...);

#endif /* _UTILS_H_ */
