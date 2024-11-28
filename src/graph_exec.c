/**
 * @file graph_exec.c
 * @brief Functions that actually build the packages following the previously set priority
 * in parallel according to the number of specified cores/threads.
 *
 * Copyright (C) 2022 Pedro Aguilar <paguilar@paguilar.org>
 * Released under the terms of the GNU GPL v2.0.
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <glib-2.0/glib.h>

#include "graph_create.h"
#include "graph_exec.h"

/**
 * @brief Calculate the number of packages that belong to the same priority
 * @param pg Main struct
 * @param cur_prio The current priority
 * @return The number of packages in the given priority
 */
static guint pb_get_pkg_num_by_prio(PBMain pg, guint cur_prio)
{
    GList   *list;
    gint    pkg_num_by_prio = 0;

    if (!pg)
        return pkg_num_by_prio;

    if (!pg->graph)
        return pkg_num_by_prio;

    list = pg->graph;
    while (list) {
        PBNode  node = (PBNode)list->data;
        if (node->priority == cur_prio)
            pkg_num_by_prio++;
        list = list->next;
    }

    return pkg_num_by_prio;
}

/**
 * @brief Execute a single target. This func is used only for the final targets
 * that are serialized.
 * @param pg Main struct
 * @param target String with the BR target name. Eg. target-finalize
 * @return PB_OK if successful, PB_FAIL otherwise
 */
static PBResult pb_finalize_single_target(PBMain pg, const gchar *target)
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
    g_string_printf(cmd, "BR2_EXTERNAL=%s make %s 2>&1", pg->env->br2_external, target);

    timer = g_timer_new();

    logs = g_string_new(NULL);
    g_string_printf(logs, "%s/pbuilder_logs/%s.log", pg->env->config_dir, target);

    if ((flog = fopen(logs->str, "a")) != NULL)
        have_logs = 1;
    else
        pb_log(LOG_ERR, "%s(): fopen(): %s: %s", __func__, logs->str, strerror(errno));

    fp = popen(cmd->str, "r");
    if (fp == NULL) {
        pb_log(LOG_ERR, "%s(): Error while building '%s': %s", __func__, target, strerror(errno));
        pb_print_err("Error while building '%s': %s\n", target, strerror(errno));
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
            pb_log(LOG_ERR, "%s(): Error while building '%s'", __func__, target);
            pb_print_err("Error while building '%s'!\nSee pbuilder_logs/%s.log\n", target, target);
            target_build_failed = 1;
        }
    }

    if (have_logs)
        fclose(flog);

    g_string_free(logs, TRUE);

    g_timer_stop(timer);
    elapsed_time = g_timer_elapsed(timer, &elapsed_usecs);

    if (!target_build_failed)
        pb_print_ok("Package '%s' built in %f secs\n", target, elapsed_time);

    g_timer_destroy(timer);

    g_string_free(cmd, TRUE);

    if (target_build_failed)
        return PB_FAIL;

    return PB_OK;
}

/**
 * @brief Execute the last targets that are not packages, but steps that normally used
 * for creating the filesystem images, FIT images and the like.
 * These operations are serialized, not in parallel
 * @param pg Main struct
 * @return PB_OK if successful, PB_FAIL otherwise
 */
static PBResult pb_finalize_targets(PBMain pg)
{
    if (!pg)
        return PB_FAIL;

    if (pb_finalize_single_target(pg, "host-finalize") != PB_OK) {
        pb_log(LOG_ERR, "%s(): Failed to execute 'host-finalize' target", __func__);
        return PB_FAIL;
    }

    if (pb_finalize_single_target(pg, "staging-finalize") != PB_OK) {
        pb_log(LOG_ERR, "%s(): Failed to execute 'staging-finalize' target", __func__);
        return PB_FAIL;
    }

    if (pb_finalize_single_target(pg, "target-finalize") != PB_OK) {
        pb_log(LOG_ERR, "%s(): Failed to execute 'target-finalize' target", __func__);
        return PB_FAIL;
    }

    if (pb_finalize_single_target(pg, "target-post-image") != PB_OK) {
        pb_log(LOG_ERR, "%s(): Failed to execute 'target-post-image' target", __func__);
        return PB_FAIL;
    }

    return PB_OK;
}


static void pb_th_wait_for_all_threads(PBMain pg)
{
    while (1) {
        if (!g_thread_pool_get_num_threads(pg->th_pool))
            break;

        sleep(1);
    }
}

/**
 * @brief The thread that builds a node. It uses a pipe to execute 'make <package>' and send all
 * its output to the logs file pbuilder_logs/<package>.logs. If there's an error, the flag build_error
 * in the main struct is set and when all the threads in that priority level finish, the calling func
 * pb_graph_exec() will halt the overall building process.
 * @param data The node to be built
 * @return NULL
 */
void pb_node_build_th(gpointer data, gpointer user_data)
{
    PBMain      pg = (PBMain)user_data;
    PBNode      node = (PBNode)data;
    GString     *pkg_path,
                *cmd,
                *logs;
    gulong      elapsed_usecs = 0;
	gint        ret,
    			have_logs = 0,
				pkg_build_failed = 0;
    gchar       path[BUFF_8K];
    FILE        *fp = NULL,
                *flog = NULL;
    struct stat sb;

    if (!pg || !node)
        return;

    /* Check if package was already built. If yes, skip it and set it as done */
    pkg_path = g_string_new(NULL);
    g_string_printf(pkg_path, "%s/%s", pg->env->build_dir, node->name->str);

    if (node->version->len > 0)
        g_string_append_printf(pkg_path, "-%s", node->version->str);

    /*if (sb.st_mode & S_IFDIR) {*/
    if ((stat(pkg_path->str, &sb) == 0) && S_ISDIR(sb.st_mode)) {
        g_string_append(pkg_path, "/.stamp_installed");
        if (stat(pkg_path->str, &sb) == 0) {
            g_string_free(pkg_path, TRUE);
            pb_print_warn("Package '%s' was already built. Skipping!\n", node->name->str);
            node->elapsed_secs = 0;
            node->status = PB_STATUS_DONE;
            return;
        }
    }

    g_string_free(pkg_path, TRUE);

    pb_debug(1, DBG_EXEC, "Thread at position %d is building '%s'\n", node->pool_pos, node->name->str);

    node->timer = g_timer_new();

    /* Write output to ${CONFIG_DIR}/pbuilder_logs/<package>.log */
    logs = g_string_new(NULL);
    g_string_printf(logs, "%s/pbuilder_logs/%s.log", pg->env->config_dir, node->name->str);
    if ((flog = fopen(logs->str, "a")) != NULL)
        have_logs = 1;
    else
        pb_log(LOG_ERR, "%s(): fopen(): %s: %s", __func__, logs->str, strerror(errno));

    /* Build package by calling make <package> */
    cmd = g_string_new(NULL);
    g_string_printf(cmd, "BR2_EXTERNAL=%s make %s 2>&1", pg->env->br2_external, node->name->str);
    /*g_string_printf(cmd, "%s/brmake %s", pg->env->config_dir, node->name->str);*/

    fp = popen(cmd->str, "r");
    if (fp == NULL) {
        pb_log(LOG_ERR, "%s(): Error while building '%s': %s", __func__, node->name->str, strerror(errno));
        pb_print_err("Error while building '%s': %s\n", node->name->str, strerror(errno));
		/* TODO exit thread*/
		pkg_build_failed = 1;
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
        	pb_log(LOG_ERR, "%s(): Error while building '%s'", __func__, node->name->str);
            pb_print_err("Error while building '%s'!\nSee pbuilder_logs/%s.log\n", node->name->str, node->name->str);
			pkg_build_failed = 1;
		}
    }

    if (have_logs)
        fclose(flog);

    g_string_free(logs, TRUE);

    g_timer_stop(node->timer);
    node->elapsed_secs = g_timer_elapsed(node->timer, &elapsed_usecs);

    if (pkg_build_failed) {
        node->pg->build_error = TRUE;
        node->build_failed = TRUE;
    }
    else
        pb_print_ok("Package '%s' built in %f secs\n", node->name->str, node->elapsed_secs);

    g_timer_destroy(node->timer);

    g_string_free(cmd, TRUE);

    node->status = PB_STATUS_DONE;

    return;
}

/**
 * @brief Get the highest priority in the graph. It's called from a g_list_foreach()
 * @param data A node
 * @param user_data An unsigned int variable where the highest priority is saved
 */
static void pb_node_get_max_priority(gpointer data, gpointer user_data)
{
    PBNode node = data;
    guint *max_prio = user_data;

    if (node->priority > *max_prio)
        *max_prio = node->priority;
}

/**
 * @brief For each priority move across the graph and build the nodes that belong to the current priority.
 * Assign a CPU core to a single node that has to be built and when it finishes go to the next node and
 * so on until there are no more nodes with the that priority.
 * @param pg Main struct
 * @return PB_OK if successful, PB_FAIL otherwise
 */
PBResult pb_graph_exec(PBMain pg)
{
    GList       *list;
    guint       i,
                pkg_num_by_prio,
                prio_num = 0;
    /*gshort      pool_pos;*/
    PBNode      node;
    /*pthread_t   th;*/
    gulong      elapsed_usecs = 0;
    GString     *logs;
    GError      *th_err;

    if (!pg)
        return PB_FAIL;

    /* Create path where the output will be writen: ${CONFIG_DIR}/pbuilder_logs/<package>.log */
    logs = g_string_new(NULL);
    g_string_printf(logs, "%s/pbuilder_logs", pg->env->config_dir);

    mkdir(logs->str, S_IRWXU);
        /*if (mkdir(logs->str, S_IRWXU) != 0)*/
        /*pb_log(PB_WARN, "%s(): mkdir(): %s: %s", __func__, logs->str, strerror(errno));*/
    g_string_free(logs, TRUE);

    g_list_foreach(pg->graph, pb_node_get_max_priority, &prio_num);
    pb_print_ok("========== Building %u packages organized in %d priorities\n", g_list_length(pg->graph), prio_num);

    pg->timer = g_timer_new();

    for (i = 1; i <= prio_num; i++) {
        pkg_num_by_prio = pb_get_pkg_num_by_prio(pg, i);
        if (pkg_num_by_prio > 0)
            pb_print_ok("========== Building %d packages with priority %d...\n", pkg_num_by_prio, i);
        else
            pb_print_ok("========== Building packages with priority %d...\n", i);

        for (list = pg->graph; list != NULL; list = list->next) {
            node = list->data;
            if (node->priority == i) {
                printf("Processing '%s' (%d)\n", node->name->str, node->priority);

                if (g_thread_pool_push(pg->th_pool, (gpointer)node, &th_err) != TRUE)
                    pb_log(LOG_ERR, "%s(): Failed to create thread for package '%s'", __func__, node->name->str);
            }
        }
        printf("\n");
        pb_th_wait_for_all_threads(pg);

        if (pg->build_error) {
            pb_print_err("Halting build due to previous errors!\n");
            break;
        }
    }

    if (pg->build_error == FALSE) {
        if (pb_finalize_targets(pg) != PB_OK) {
            pb_log(LOG_ERR, "%s(): Failed to finalize targets", __func__);
            pb_print_err("Failed to finalize targets\n");
            pg->build_error = TRUE;
        }
    }

    g_timer_stop(pg->timer);
    pg->elapsed_secs = g_timer_elapsed(pg->timer, &elapsed_usecs);

    pb_print_ok("===== Total elapsed time: %f\n", pg->elapsed_secs);
    g_timer_destroy(pg->timer);
    pg->timer = NULL;

    if (pg->build_error) {
        pb_print_err("Build failed!!!\n");
        pb_print_err("See pbuilder_logs/<pkg>.log for further info.\n");
        pb_print_err("The following packages gave an error:\n");
        for (list = pg->graph; list != NULL; list = list->next) {
            node = list->data;
            if (node->build_failed)
                pb_print_err("%s\n", node->name->str);
        }
        return PB_FAIL;
    }

    return PB_OK;
}

