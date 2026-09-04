/**
 * @file graph_remove_node.c
 * @brief Functions that remove a node (package) with all its dependencies
 *
 * Copyright (C) 2025 Pedro Aguilar <paguilar@paguilar.org>
 * Released under the terms of the GNU GPL v2.0.
 *
 */

#include "graph_common.h"
#include "graph_remove_nodes.h"

/**
 * @brief The thread that builds a node. It uses a pipe to execute 'make <package>' and send all
 * its output to the logs file pbuilder_logs/<package>.logs. If there's an error, the flag build_error
 * in the main struct will be set causing the calling function, pb_graph_exec(), to halt the overall build process.
 * @param data The node to be built
 * @return NULL
 */
static void pb_node_remove_th(gpointer data, gpointer user_data)
{
    PBMain      pg = (PBMain)user_data;
    PBNode      node = (PBNode)data;
    GString     *cmd,
                *logs;
    gulong      elapsed_usecs = 0;
    gint        ret,
                have_logs = 0,
                pkg_build_failed = 0,
                total_nodes_done = 0;
    gchar       path[BUFF_8K];
    FILE        *fp = NULL,
                *fd = NULL;

    if (!pg || !node)
        return;

    pb_debug(1, DBG_EXEC, "Thread at position %d is processing '%s'\n", node->pool_pos, node->name->str);

    node->timer = g_timer_new();

    /* Write output to ${CONFIG_DIR}/pbuilder_logs/<package>.log */
    logs = g_string_new(NULL);
    g_string_printf(logs, "%s/pbuilder_logs/%s.log", pg->env->config_dir, node->name->str);
    if ((fd = fopen(logs->str, "a")) != NULL)
        have_logs = 1;
    else
        pb_log(LOG_ERR, "%s(): fopen(): %s: %s", __func__, logs->str, strerror(errno));

    /* Remove package by calling make <package>-uninstall */
    cmd = g_string_new(NULL);
    if (strlen(pg->env->br2_external) > 0)
        g_string_printf(cmd, "BR2_EXTERNAL=%s ", pg->env->br2_external);

    g_string_append_printf(cmd, "make %s-uninstall 2>&1", node->name->str);

    fp = popen(cmd->str, "r");
    if (fp == NULL) {
        pb_log(LOG_ERR, "%s(): Pipe creation failed while building '%s': %s", __func__, node->name->str, strerror(errno));
		/* TODO exit thread*/
		pkg_build_failed = 1;
	}
    else {
        while (fgets(path, sizeof(path), fp) != NULL) {
            if (have_logs)
                fwrite(path, sizeof(char), strlen(path), fd);
            if (!strncmp(path, "\E[7m>>>", 7))
                printf("%s", path);
        }

        ret = WEXITSTATUS(pclose(fp));
		if (ret) {
            pb_log(LOG_ERR, "Error while building '%s'!\nSee pbuilder_logs/%s.log\n", node->name->str, node->name->str);
            pkg_build_failed = 1;
		}
    }

    if (have_logs)
        fclose(fd);

#if 0
    if ((node->priority == 1) || (access(pg->br2_ext_file->str, F_OK) != 0)) {
        if ((fd = fopen(pg->br2_ext_file->str, "w")) == NULL)
            pb_log(LOG_ERR, "%(): fopen(): %s", __func__, strerror(errno));
        else
            fclose(fd);
    }
#endif

    g_string_free(logs, TRUE);

    g_timer_stop(node->timer);
    node->elapsed_secs = g_timer_elapsed(node->timer, &elapsed_usecs);

    if (pkg_build_failed) {
        node->pg->build_error = TRUE;
        node->build_failed = TRUE;
    }

    g_timer_destroy(node->timer);

    g_string_free(cmd, TRUE);

    node->status = PB_STATUS_DONE;

    /* If the package was successfully built, print elapsed time and total percentage */
    if (!pkg_build_failed) {
        g_mutex_lock(&pg->nodes_mutex);

        for (GList *list = pg->graph; list; list = list->next) {
            PBNode pkg = (PBNode)list->data;
            if (pkg->status == PB_STATUS_DONE)
                total_nodes_done++;
        }

        pb_log(PB_INFO, "(%.2f%%) Package '%s' built in %.3f secs\n",
            (float)total_nodes_done / (float)g_list_length(pg->graph) * 100, node->name->str, node->elapsed_secs);

        g_mutex_unlock(&pg->nodes_mutex);
    }

    return;
}

/**
 * @brief All the packages that depend on the given packages to be removed must also be removed,
 * otherwise we may end-up with a broken system
 * @param pbg Main struct
 * @return PB_OK if successful, PB_FAIL otherwise
 */
static PBResult pb_mark_pkgs_to_be_removed(PBMain pg, gchar *str_ptr)
{
    GList   *list;
    PBNode  node;

    if (!pg || !str_ptr)
        return PB_FAIL;

    list = pg->graph;

    if ((node = pb_node_find_by_name(list, str_ptr)) == NULL) {
        pb_log(LOG_ERR, "%s(): Failed to find node '%s'", __func__, str_ptr);
        return PB_FAIL;
    }

    if (node->status == PB_STATUS_TO_BE_REMOVED) {
        pb_debug(3, DBG_REMOVE, "Package '%s' already set to be removed\n", str_ptr);
        return PB_OK;
    }

    if (!node->children) {
        pb_log(PB_WARN, "Package '%s' set to be removed\n", str_ptr);
        pb_debug(1, DBG_REMOVE, "\tThis package is a leaf node\n");
        node->status = PB_STATUS_TO_BE_REMOVED;
        return PB_OK;
    }

    for (GList *child_list = node->children; child_list != NULL; child_list = child_list->next) {
        PBNode child_node = child_list->data;

        pb_mark_pkgs_to_be_removed(pg, child_node->name->str);
    }

    pb_log(PB_WARN, "Package '%s' set to be removed\n", str_ptr);
    node->status = PB_STATUS_TO_BE_REMOVED;
    return PB_OK;
}

/**
 * @brief Create pool of threads. Each thread builds one package at a time.
 * The size of the pool is the "cpu" command line argument or the max number
 * of available cores
 * @param pbg Main struct
 * @return PB_OK if successful, PB_FAIL otherwise
 */
static PBResult pb_th_init_pool(PBMain pbg)
{
    GError  *th_err;

    if (!pbg)
        return PB_FAIL;

    /* Create a non-exclusive pool */
    pbg->th_pool = g_thread_pool_new(pb_node_remove_th, pbg, pbg->cpu_num, FALSE, NULL);
    if (!pbg->th_pool)
        return PB_FAIL;

    if(g_thread_pool_set_max_threads(pbg->th_pool, pbg->cpu_num, &th_err) != TRUE) {
        pb_log(LOG_ERR, "%s(): Failed to set max number of threads for the pool", __func__);
        g_thread_pool_free(pbg->th_pool, TRUE, FALSE);
        return PB_FAIL;
    }

    return PB_OK;
}

/**
 * @brief
 * @param pg Main struct
 * @return PB_OK if successful, PB_FAIL otherwise
 */
PBResult pb_graph_remove_nodes(PBMain pg)
{
    GList       *list;
    PBNode      node;
    gulong      elapsed_usecs = 0;
    GString     *logs;
    gchar       **str_ptr,
                **pkg_name_list;
    gboolean    pkg_found = FALSE;

    if (!pg)
        return PB_FAIL;

    if (pb_th_init_pool(pg) != PB_OK) {
        pb_log(PB_ERR, "Failed to init thread pool");
        return PB_FAIL;
    }

    /* Create path where the output will be writen: ${CONFIG_DIR}/pbuilder_logs/<package>.log */
    logs = g_string_new(NULL);
    g_string_printf(logs, "%s/pbuilder_logs", pg->env->config_dir);

    mkdir(logs->str, S_IRWXU);
    g_string_free(logs, TRUE);

    pg->br2_ext_file = g_string_new(NULL);
    g_string_printf(pg->br2_ext_file, "%s/%s", pg->env->config_dir, BR2_EXT_EXEC_ONCE_FILE);
    remove(pg->br2_ext_file->str);

    pkg_name_list = g_strsplit(pg->env->remove_pkgs, ",", PBUILDER_REMOVE_PKGS_MAX_NUM);

    /* Verify that the packages to be removed have been previously installed */
    for (str_ptr = pkg_name_list; *str_ptr != NULL; str_ptr++) {
        pkg_found = FALSE;
        for (list = pg->graph; list != NULL; list = list->next) {
            node = (PBNode)list->data;
            if (!strncmp(*str_ptr, node->name->str, node->name->len)) {
                pkg_found = TRUE;
                break;
            }
        }

        if (pkg_found == FALSE) {
            pb_log(PB_ERR, "Cannot remove package '%s' since it hasn't been installed. Halting!\n", *str_ptr);
            g_strfreev(pkg_name_list);
            return PB_FAIL;
        }

        pb_debug(1, DBG_REMOVE, "Package '%s' found!\n", *str_ptr);
    }

    /* Set the status of the packages as to-be-removed */
    str_ptr = pkg_name_list;
    for (str_ptr = pkg_name_list; *str_ptr != NULL; str_ptr++) {
        if (pb_mark_pkgs_to_be_removed(pg, *str_ptr) != PB_OK) {
            pb_log(PB_ERR, "Failed to set package '%s' as 'to be removed'", *str_ptr);
            g_strfreev(pkg_name_list);
            return PB_FAIL;
        }
    }

    /* Now that we're sure that all the packages set to be removed were previously installed, remove them */

    pg->timer = g_timer_new();

#if 1 
    while (TRUE) {
        if (g_thread_pool_get_num_threads(pg->th_pool) > pg->cpu_num){
            pb_log(PB_ERR, "Number of threads is greater than the number of CPUs. Halting build!\n");
            pg->build_error = TRUE;
            break;
        }

        guint num_threads_available = (guint)(pg->cpu_num) - g_thread_pool_get_num_threads(pg->th_pool);

        for (list = pg->graph; list != NULL; list = list->next) {
            gboolean children_removed = TRUE;
            node = list->data;

            if (num_threads_available == 0) {
                break;
            }

            if (node->status != PB_STATUS_TO_BE_REMOVED) {
                continue;
            }

            if (pb_node_already_built(node)) {
                pb_log(PB_WARN, "Package '%s' was already built. Skipping!\n", node->name->str);
                continue;
            }

            for (GList *children_list = node->children; children_list != NULL; children_list = children_list->next) {
                PBNode children_node = children_list->data;
                if (children_node->status != PB_STATUS_DONE) {
                    children_removed = FALSE;
                    break;
                }
            }

            if (children_removed) {
                printf("Processing '%s'\n", node->name->str);
                if (g_thread_pool_push(pg->th_pool, (gpointer)node, NULL) != TRUE) {
                    pb_log(LOG_ERR, "%s(): Failed to create thread for package '%s'", __func__, node->name->str);
                    pg->build_error = TRUE;
                    break;
                }
                node->status = PB_STATUS_PROCESSING;
                num_threads_available--;
            }
        }

        if (pg->build_error) {
            pb_log(PB_ERR, "Halting build due to previous errors!\n");
            break;
        }

        if (!g_thread_pool_get_num_threads(pg->th_pool))
            break;
        
        sleep(1);
    }
#endif

    g_strfreev(pkg_name_list);

    pb_th_wait_for_all_threads(pg);

    remove(pg->br2_ext_file->str);

    g_timer_stop(pg->timer);
    pg->elapsed_secs = g_timer_elapsed(pg->timer, &elapsed_usecs);

    pb_log(PB_INFO, "===== Total elapsed time: %.3f\n", pg->elapsed_secs);
    g_timer_destroy(pg->timer);
    pg->timer = NULL;

    if (pg->build_error) {
        pb_log(PB_ERR, "Failed to remove packages!!!\n");
        pb_log(PB_ERR, "See pbuilder_logs/<pkg>.log for further info.\n");
        pb_log(PB_ERR, "The following packages gave an error:\n");
        for (list = pg->graph; list != NULL; list = list->next) {
            node = list->data;
            if (node->build_failed)
                pb_log(PB_ERR, "%s\n", node->name->str);
        }
        return PB_FAIL;
    }

    return PB_OK;
}

