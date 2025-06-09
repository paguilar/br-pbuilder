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
    if (strlen(pg->env->br2_external) > 0)
        g_string_printf(cmd, "BR2_EXTERNAL=%s ", pg->env->br2_external);

    g_string_append_printf(cmd, "make %s 2>&1", target);

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
 * @brief Check if a package was already built. If yes, set state to done and return 1.
 * @param node The node to be checked
 * @return 1 if the package was already built, 0 otherwise
 */
gboolean pb_node_already_built(PBNode node){
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

/**
 * @brief The thread that builds a node. It uses a pipe to execute 'make <package>' and send all
 * its output to the logs file pbuilder_logs/<package>.logs. If there's an error, the flag build_error
 * in the main struct will be set causing the calling function, pb_graph_exec(), to halt the overall build process.
 * @param data The node to be built
 * @return NULL
 */
void pb_node_build_th(gpointer data, gpointer user_data)
{
    PBMain      pg = (PBMain)user_data;
    PBNode      node = (PBNode)data;
    GString     *cmd,
                *logs;
    gulong      elapsed_usecs = 0;
	gint        ret,
    			have_logs = 0,
				pkg_build_failed = 0;
    gchar       path[BUFF_8K];
    FILE        *fp = NULL,
                *fd = NULL;

    if (!pg || !node)
        return;

    pb_debug(1, DBG_EXEC, "Thread at position %d is building '%s'\n", node->pool_pos, node->name->str);

    node->timer = g_timer_new();

    /* Write output to ${CONFIG_DIR}/pbuilder_logs/<package>.log */
    logs = g_string_new(NULL);
    g_string_printf(logs, "%s/pbuilder_logs/%s.log", pg->env->config_dir, node->name->str);
    if ((fd = fopen(logs->str, "a")) != NULL)
        have_logs = 1;
    else
        pb_log(LOG_ERR, "%s(): fopen(): %s: %s", __func__, logs->str, strerror(errno));

    /* Build package by calling make <package> */
    cmd = g_string_new(NULL);
    if (strlen(pg->env->br2_external) > 0)
        g_string_printf(cmd, "BR2_EXTERNAL=%s ", pg->env->br2_external);

    g_string_append_printf(cmd, "make %s 2>&1", node->name->str);

    fp = popen(cmd->str, "r");
    if (fp == NULL) {
        pb_log(LOG_ERR, "%s(): Pipe creation failed while building '%s': %s", __func__, node->name->str, strerror(errno));
        pb_print_err("Pipe creation failed while building '%s': %s\n", node->name->str, strerror(errno));
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
            pb_log(LOG_ERR, "%s(): Error while building '%s'", __func__, node->name->str);
            pb_print_err("Error while building '%s'!\nSee pbuilder_logs/%s.log\n", node->name->str, node->name->str);
            pkg_build_failed = 1;
		}
    }

    if (have_logs)
        fclose(fd);

    if ((node->priority == 1) || (access(pg->br2_ext_file->str, F_OK) != 0)) {
        pb_print_ok("BR2 external lock file doesn't exist, Creating it...\n");
        if ((fd = fopen(pg->br2_ext_file->str, "w")) == NULL)
            pb_log(LOG_ERR, "%(): fopen(): %s", __func__, strerror(errno));
        else
            fclose(fd);
    }

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
 * @brief For each priority move across the graph and build the nodes that belong to the current priority.
 * Assign a CPU core to a single node that has to be built and when it finishes go to the next node and
 * so on until there are no more nodes with the that priority.
 * @param pg Main struct
 * @return PB_OK if successful, PB_FAIL otherwise
 */
PBResult pb_graph_exec(PBMain pg)
{
    GList       *list;
    PBNode      node;
    gulong      elapsed_usecs = 0;
    GString     *logs;

    if (!pg)
        return PB_FAIL;

    /* Create path where the output will be writen: ${CONFIG_DIR}/pbuilder_logs/<package>.log */
    logs = g_string_new(NULL);
    g_string_printf(logs, "%s/pbuilder_logs", pg->env->config_dir);

    mkdir(logs->str, S_IRWXU);
        /*if (mkdir(logs->str, S_IRWXU) != 0)*/
        /*pb_log(PB_WARN, "%s(): mkdir(): %s: %s", __func__, logs->str, strerror(errno));*/
    g_string_free(logs, TRUE);

    pg->br2_ext_file = g_string_new(NULL);
    g_string_printf(pg->br2_ext_file, "%s/%s", pg->env->config_dir, BR2_EXT_EXEC_ONCE_FILE);
    pb_print_ok("Removing BR2 external lock file %s\n", pg->br2_ext_file->str);
    remove(pg->br2_ext_file->str);

    pb_print_ok("========== Building %u packages using br-pbuilder\n", g_list_length(pg->graph));

    pg->timer = g_timer_new();

    while (TRUE) {
        if (g_thread_pool_get_num_threads(pg->th_pool) > pg->cpu_num){
            pb_print_err("Number of threads is greater than the number of CPUs. Halting build!\n");
            pg->build_error = TRUE;
            break;
        }

        guint num_threads_available = (guint)(pg->cpu_num) - g_thread_pool_get_num_threads(pg->th_pool);

        for (list = pg->graph; list != NULL; list = list->next) {
            gboolean dependencies_built = TRUE;
            node = list->data;

            if (num_threads_available == 0) {
                break;
            }

            if (node->status != PB_STATUS_READY){
                continue;
            }

            if (pb_node_already_built(node)){
                pb_print_warn("Package '%s' was already built. Skipping!\n", node->name->str);
                continue;
            }

            for (GList *parent_list = node->parents; parent_list != NULL; parent_list = parent_list->next) {
                PBNode parent_node = parent_list->data;
                if (parent_node->status != PB_STATUS_DONE) {
                    dependencies_built = FALSE;
                    break;
                }
            }

            if (dependencies_built) {
                printf("Processing '%s'\n", node->name->str);
                if (g_thread_pool_push(pg->th_pool, (gpointer)node, NULL) != TRUE) {
                    pb_log(LOG_ERR, "%s(): Failed to create thread for package '%s'", __func__, node->name->str);
                    pg->build_error = TRUE;
                    break;
                }
                node->status = PB_STATUS_BUILDING;
                num_threads_available--;
            }
        }

        if (pg->build_error) {
            pb_print_err("Halting build due to previous errors!\n");
            break;
        }

        if (!g_thread_pool_get_num_threads(pg->th_pool))
            break;
        
        sleep(1);
    }

    pb_th_wait_for_all_threads(pg);

    remove(pg->br2_ext_file->str);
    pb_print_ok("Removing BR2 external lock file %s\n", pg->br2_ext_file->str);

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

