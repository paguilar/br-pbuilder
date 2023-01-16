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
    g_string_printf(cmd, "make %s 1>/dev/null 2>/dev/null", target);

    timer = g_timer_new();

    logs = g_string_new("pbuilder_logs");

    g_string_printf(logs, "pbuilder_logs/%s.log", target);
    if ((flog = fopen(logs->str, "a")) != NULL)
        have_logs = 1;
    else
        pb_log(LOG_ERR, "%s(): fopen(): %s: %s", __func__, logs->str, strerror(errno));

    fp = popen(cmd->str, "r");
    if (fp == NULL) {
        pb_log(LOG_ERR, "%s(): Error while building '%s': %s", __func__, target, strerror(errno));
        printf("%sError while building '%s': %s%s\n", C_RED, target, strerror(errno), C_NORMAL);
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
            printf("%sError while building '%s'!\nSee pbuilder_logs/%s.log%s\n",
                C_RED, target, target, C_NORMAL);
            target_build_failed = 1;
        }
    }

    if (have_logs)
        fclose(flog);

    g_string_free(logs, TRUE);

    g_timer_stop(timer);
    elapsed_time = g_timer_elapsed(timer, &elapsed_usecs);

    if (!target_build_failed)
        printf("Package '%s' built in %f secs\n", target, elapsed_time);

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
    gushort i,
            th_total;

    while (1) {
        th_total = 0;

        for (i = 0; i < pg->cpu_num; i++) {
            if (*(pg->th_pool + i) == 0) {
                th_total++;
            }
        }

        if (th_total >= pg->cpu_num)
            break;

        usleep(250000);
    }
}

static gshort pb_th_get_avail_thread(PBMain pg)
{
    gushort i;

    if (!pg)
        return -1;

    for (i = 0; i < pg->cpu_num; i++) {
        if (*(pg->th_pool + i) == 0) {
            pb_debug(2, DBG_EXEC, "Position free: %d\n", i);
            return i;
        }
    }

    /*printf("No available position found\n");*/
    return -1;
}


static gshort pb_th_wait_for_avail_thread(PBMain pg)
{
    gshort avail_pos = -1;

    if (!pg)
        return avail_pos;

    while ((avail_pos = pb_th_get_avail_thread(pg)) < 0) {
        /*printf("    %s(): Waiting...\n", __func__);*/
        usleep(250000);
    }

    return avail_pos;
}

/**
 * @brief Remove the thread id from the pool of threads
 * @param pg The main struct
 * @param tid Thread id
 * @return PB_OK if successful, PB_FAIL otherwise
 */
static PBResult pb_th_remove_from_pool(PBMain pg, pthread_t *tid)
{
    gushort i;

    if (!pg || !tid)
        return PB_FAIL;

    for (i = 0; i < pg->cpu_num; i++) {
        /*printf("Comparing %lu - %lu\n", *(pg->th_pool + i), *tid);*/
        if (*(pg->th_pool + i) == *tid) {
            pb_debug(2, DBG_EXEC, "Freeing thread at position %d\n", i);
            *(pg->th_pool + i) = 0;
            return PB_OK;
        }
    }

    pb_log(PB_ERR, "Trying to remove unregister thread %lu\n", *tid);

    return PB_FAIL;
}

/**
 * @brief Add the thread id to the pool of threads in the position that was previosuly
 * reserved before creating the thread
 * @param pg The main struct
 * @param tid Thread id
 * @param avail_pos Position in the pool of threads
 * @return PB_OK if successful, PB_FAIL otherwise
 */
static PBResult pb_th_add_to_pool(PBMain pg, pthread_t *tid, gushort avail_pos)
{
    if (!pg || !tid)
        return PB_FAIL;

    *(pg->th_pool + avail_pos) = *tid;
    pb_debug(2, DBG_EXEC, "Thread %lu assigned to position %d\n", *tid, avail_pos);

    return PB_OK;
}

/**
 * @brief The thread that builds a node. It uses a pipe to execute 'make <package>' and send all
 * its output to the logs file pbuilder_logs/<package>.logs. If there's an error, the flag build_error
 * in the main struct is set and when all the threads in that priority level finish, the calling func
 * pb_graph_exec() will halt the overall building process.
 * @param data The node to be built
 * @return NULL
 */
static gpointer pb_node_build_th(gpointer data)
{
    pthread_t   tid;
    PBNode      node = data;
    GString     *pkg_path,
                *cmd,
                *logs;
    gulong      elapsed_usecs = 0;
	gint        ret,
    			have_logs = 0,
				pkg_build_failed = 0;
    gchar       *build_dir,
                *config_dir,
                *br2_external;
    gchar       path[BUFF_8K];
    FILE        *fp = NULL,
                *flog = NULL;
    struct stat sb;

    if (!node)
        return NULL;

    build_dir = getenv("BUILD_DIR");
    if (!build_dir) {
        pb_log(LOG_ERR, "%s(): Failed to get environment variable BUILD_DIR", __func__);
        return NULL;
    }

    config_dir = getenv("CONFIG_DIR");
    if (!config_dir) {
        pb_log(LOG_ERR, "%s(): Failed to get environment variable CONFIG_DIR", __func__);
        return NULL;
    }

    if (chdir(config_dir)) {
        pb_log(LOG_ERR, "%s(): Failed to chdir to %s", __func__, config_dir);
        return NULL;
    }

    br2_external = getenv("BR2_EXTERNAL");
    if (!br2_external) {
        pb_log(LOG_ERR, "%s(): Failed to get environment variable BR2_EXTERNAL", __func__);
        return NULL;
    }

    tid = pthread_self();
    /* Add thread to pool */
    pb_th_add_to_pool(node->pg, &tid, node->pool_pos);

    /* Check if package was already built. If yes, skip it and set it as done */
    pkg_path = g_string_new(NULL);
    g_string_printf(pkg_path, "%s/%s", build_dir, node->name->str);

    if (node->version->len > 0)
        g_string_append_printf(pkg_path, "-%s", node->version->str);

    /*if (sb.st_mode & S_IFDIR) {*/
    if ((stat(pkg_path->str, &sb) == 0) && S_ISDIR(sb.st_mode)) {
        g_string_append(pkg_path, "/.stamp_installed");
        if (stat(pkg_path->str, &sb) == 0) {
            g_string_free(pkg_path, TRUE);
            pb_debug(1, DBG_EXEC, "Package '%s' was already built. Skipping!\n", node->name->str);
            node->elapsed_secs = 0;
            if (pb_th_remove_from_pool(node->pg, &tid) != PB_OK)
                pb_log(PB_ERR, "%s(): Failed to remove thread (%lu) from pool!", __func__, tid);
            node->status = PB_STATUS_DONE;
            return NULL;
        }
    }

    g_string_free(pkg_path, TRUE);

    /* Exec system() with make? */
    pb_debug(1, DBG_EXEC, "Thread at position %d is building '%s'\n", node->pool_pos, node->name->str);

    cmd = g_string_new(NULL);
    /*g_string_printf(cmd, "make %s 1>/dev/null 2>/dev/null", node->name->str);*/
    g_string_printf(cmd, "BR2_EXTERNAL=%s make %s 2>&1", br2_external, node->name->str);
    /*g_string_printf(cmd, "%s/brmake %s", config_dir, node->name->str);*/

    node->timer = g_timer_new();

    logs = g_string_new("pbuilder_logs");

    if ((stat(logs->str, &sb) == 0) && S_ISDIR(sb.st_mode))
        have_logs = 1;
    else
        if (mkdir(logs->str, S_IRWXU) != 0)
            pb_log(LOG_DEBUG, "%s(): mkdir(): %s: %s", __func__, logs->str, strerror(errno));

    g_string_printf(logs, "pbuilder_logs/%s.log", node->name->str);
    if ((flog = fopen(logs->str, "a")) != NULL)
        have_logs = 1;
    else
        pb_log(LOG_ERR, "%s(): fopen(): %s: %s", __func__, logs->str, strerror(errno));

    fp = popen(cmd->str, "r");
    if (fp == NULL) {
        pb_log(LOG_ERR, "%s(): Error while building '%s': %s", __func__, node->name->str, strerror(errno));
    	printf("%sError while building '%s': %s%s\n", C_RED, node->name->str, strerror(errno),  C_NORMAL);
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
            printf("%sError while building '%s'!\nSee pbuilder_logs/%s.log%s\n",
                C_RED, node->name->str, node->name->str, C_NORMAL);
			pkg_build_failed = 1;
		}
    }

    if (have_logs)
        fclose(flog);

    g_string_free(logs, TRUE);

    g_timer_stop(node->timer);
    node->elapsed_secs = g_timer_elapsed(node->timer, &elapsed_usecs);

    if (pkg_build_failed)
        node->pg->build_error = TRUE;
    else
    	printf("%sPackage '%s' built in %f secs%s\n", C_GREEN, node->name->str, node->elapsed_secs, C_NORMAL);

    g_timer_destroy(node->timer);

    g_string_free(cmd, TRUE);

    /* Remove thread from pool */
    if (pb_th_remove_from_pool(node->pg, &tid) != PB_OK) {
        pb_log(PB_ERR, "%s(): Failed to remove thread (%lu) from pool!", __func__, tid);
    }

    node->status = PB_STATUS_DONE;

    return NULL;
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
                prio_num = 0;
    gshort      pool_pos;
    PBNode      node;
    pthread_t   th;
    gulong      elapsed_usecs = 0;

    if (!pg)
        return PB_FAIL;

    g_list_foreach(pg->graph, pb_node_get_max_priority, &prio_num);
    pb_debug(1, DBG_ALL, "Number of priorities found: %d\n", prio_num);

    pg->timer = g_timer_new();

    for (i = 1; i <= prio_num; i++) {
        printf("%s========== Processing packages with priority %d...%s\n", C_GREEN, i, C_NORMAL);
        for (list = pg->graph; list != NULL; list = list->next) {
            node = list->data;
            if (node->priority == i) {
                printf("Processing '%s' (%d)\n", node->name->str, node->priority);

                pool_pos = pb_th_get_avail_thread(pg);
                if (pool_pos < 0) {
                    pb_debug(1, DBG_EXEC, "All threads are busy, waiting...\n");
                    pool_pos = pb_th_wait_for_avail_thread(pg);
                }

                node->status = PB_STATUS_BUILDING;

                node->pool_pos = pool_pos;

                /* TODO Reserve position in pool */
                pg->th_pool[pool_pos] = -1;

                pthread_create(&th, NULL, pb_node_build_th, node);
            }
        }
        printf("\n");
        pb_th_wait_for_all_threads(pg);

        if (pg->build_error) {
            printf("%sHalting build due to previous errors!%s\n", C_RED, C_NORMAL);
            break;
        }
    }

    if (pg->build_error == FALSE) {
        if (pb_finalize_targets(pg) != PB_OK) {
            pb_log(LOG_ERR, "%s(): Failed to finalize targets", __func__);
            printf("%sFailed to finalize targets%s\n", C_RED, C_NORMAL);
            pg->build_error = TRUE;
        }
    }

    g_timer_stop(pg->timer);
    pg->elapsed_secs = g_timer_elapsed(pg->timer, &elapsed_usecs);

    printf("%s===== Total elapsed time: %f%s\n", C_GREEN, pg->elapsed_secs, C_NORMAL);
    g_timer_destroy(pg->timer);
    pg->timer = NULL;

    if (pg->build_error) {
        printf("%sBuild failed!!!%s\n", C_RED, C_NORMAL);
        return PB_FAIL;
    }

    return PB_OK;
}

