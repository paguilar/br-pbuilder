/**
 * @file main.c
 * @brief Main file.
 *
 * Copyright (C) 2022 Pedro Aguilar <paguilar@paguilar.org>
 * Released under the terms of the GNU GPL v2.0.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>

#include <glib-2.0/glib.h>
#include <glibconfig.h>

#include "graph_create.h"
#include "graph_exec.h"

gint    debug_level;
gchar   *debug_module;
gchar   *deps_file;
gint    cpu_num;

static GOptionEntry opt_entries[] =
{
    { "debug_level", 'l', 0, G_OPTION_ARG_INT, &debug_level,
        "Set debug level. Values: [1-3]. Default: 0 (disabled)", NULL },
    { "debug_module", 'm', 0, G_OPTION_ARG_STRING, &debug_module,
        "Set module to debug. Values: all, create, execute, none. Default: none", NULL },
    { "filename", 'f', 0, G_OPTION_ARG_FILENAME, &deps_file,
        "Dependencies file generated by pbuilder.py", NULL },
    { "cpu", 'c', 0, G_OPTION_ARG_INT, &cpu_num,
        "Max number of CPUs used to build. Default: 0 (Auto-detect)", NULL },
    { NULL }
};

int main(int argc, char *argv[]) 
{
    GOptionContext  *opt_context;
    GError          *error = NULL;
    GPMain          pbg;

    opt_context = g_option_context_new (PBUILDER_DESC);
    g_option_context_add_main_entries (opt_context, opt_entries, NULL);
    if (!g_option_context_parse (opt_context, &argc, &argv, &error)) {
        pb_log(GP_ERR, "Error while parsing options: %s. Aborting!", error->message);
        return EXIT_FAILURE;
    }

    if (!debug_module) {
        debug_module = g_new0(gchar, 4);
        g_snprintf(debug_module, 4, "all");
    }

    if (!deps_file) {
        pb_log(GP_ERR, "No dependencies filename given. Aborting!");
        g_option_context_free(opt_context);
        return EXIT_FAILURE;
    }

    if (pb_graph_create(&pbg) != GP_OK) {
        pb_log(GP_ERR, "Failed to create graph");
        g_option_context_free(opt_context);
        return EXIT_FAILURE;
    }

    if (pb_graph_exec(pbg) != GP_OK) {
        pb_log(GP_ERR, "Failed to execute graph");
        pb_graph_free(pbg);
        g_option_context_free(opt_context);
        return EXIT_FAILURE;
    }

    pb_graph_free(pbg);

    g_option_context_free(opt_context);

    return EXIT_SUCCESS;
}
