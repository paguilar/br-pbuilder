/**
 * @file graph_common.c
 * @brief
 *
 * Copyright (C) 2022 Pedro Aguilar <paguilar@paguilar.org>
 * Released under the terms of the GNU GPL v2.0.
 *
 */

#include "graph_common.h"
#include "utils.h"

PBNode pb_node_find_by_name(GList *list, gchar *str)
{
    if (!list || !str)
        return NULL;

    for (; list != NULL; list = list->next) {
        PBNode node = list->data;

        if (!g_strcmp0(node->name->str, str)) {
            pb_debug(3, DBG_ALL, "Found!!!!!\n");
            return node;
        }
    }

    return NULL;
}

/**
 * @brief Search the name of a node. It's called for each node in the graph
 * @param a A node in the graph
 * @param b The name of the node to search for
 * @return 0 if a node is found, 1 otherwise
 */
gint pb_node_name_exists(gconstpointer a, gconstpointer b)
{
    const struct pbuilder_node_st *  node = a;
    const gchar   *str = b;

    pb_debug(3, DBG_ALL, "\t%s - %s: ", str, node->name->str);

    if (!g_strcmp0(node->name->str, str)) {
        pb_debug(3, DBG_ALL, "Found!!!!!\n");
        return 0;
    }
    else
        pb_debug(3, DBG_ALL, "NOT found\n");

    return 1;
}

#if 0
/**
 * @brief Execute the last targets that are not packages, but steps normally used
 * for creating the filesystem images, FIT images and the like.
 * These operations must be serialized.
 * @param pg Main struct
 * @param target String with the BR target name. Eg. target-finalize
 * @return PB_OK if successful, PB_FAIL otherwise
 */
PBResult pb_finalize_single_target(PBMain pg, const gchar *target)
{
    gchar   path[BUFF_8K];
    FILE    *fp = NULL,
            *flog = NULL;
    gint    ret,
            have_logs = 0,
			target_build_failed = 0;
    GString *cmd,
            *logs;
    GTimer  *timer;
    gdouble elapsed_time;
    gulong  elapsed_usecs = 0;

    if (!pg || !target)
        return PB_FAIL;

    cmd = g_string_new(NULL);
    if (strlen(pg->env->br2_external) > 0)
        g_string_printf(cmd, "BR2_EXTERNAL=%s ", pg->env->br2_external);

    g_string_append_printf(cmd, "make %s 2>&1", target);

    timer = g_timer_new();

    logs = g_string_new(NULL);
    g_string_printf(logs, "%s/pbuilder_logs/%s.log", pg->env->config_dir, target);

    if ((flog = fopen(logs->str, "a")) != NULL)
        have_logs = 1;
    else
        pb_log(PB_ERR, "%s(): fopen(): %s: %s", __func__, logs->str, strerror(errno));

    fp = popen(cmd->str, "r");
    if (fp == NULL) {
        pb_log(PB_ERR, "Error while building '%s': %s\n", target, strerror(errno));
        target_build_failed = 1;
    }
    else {
        while (fgets(path, sizeof(path), fp) != NULL) {
            if (have_logs)
                fwrite(path, sizeof(char), strlen(path), flog);
            if (!strncmp(path, "\E[7m>>>", 7))
                printf("%s", path);
        }

        ret = WEXITSTATUS(pclose(fp));
        if (ret) {
            pb_log(PB_ERR, "Error while building '%s'!\nSee pbuilder_logs/%s.log\n", target, target);
            target_build_failed = 1;
        }
    }

    if (have_logs)
        fclose(flog);

    g_string_free(logs, TRUE);

    g_timer_stop(timer);
    elapsed_time = g_timer_elapsed(timer, &elapsed_usecs);

    if (!target_build_failed)
        pb_log(PB_INFO, "'%s' executed in %.3f secs\n", target, elapsed_time);

    g_timer_destroy(timer);

    g_string_free(cmd, TRUE);

    if (target_build_failed)
        return PB_FAIL;

    return PB_OK;
}
#endif

void pb_th_wait_for_all_threads(PBMain pg)
{
    while (1) {
        if (!g_thread_pool_get_num_threads(pg->th_pool))
            break;

        sleep(1);
    }
}

/**
 * @brief Check if a package was already built. If yes, set state to done and return 1.
 * @param node The node to be checked
 * @return 1 if the package was already built, 0 otherwise
 */
gboolean pb_node_already_built(PBNode node) {
    struct stat sb;
    GString *pkg_path = g_string_new(NULL);
    g_string_printf(pkg_path, "%s/%s", node->pg->env->build_dir, node->name->str);

    if (node->version->len > 0)
        g_string_append_printf(pkg_path, "-%s", node->version->str);

    if ((stat(pkg_path->str, &sb) == 0) && S_ISDIR(sb.st_mode)) {
        g_string_append(pkg_path, "/.stamp_installed");
        if (stat(pkg_path->str, &sb) == 0) {
            g_string_free(pkg_path, TRUE);
            node->elapsed_secs = 0;
            node->status = PB_STATUS_DONE;
            return TRUE;
        }
    }
    g_string_free(pkg_path, TRUE);
    return FALSE;
}

