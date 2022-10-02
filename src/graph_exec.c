/**
 * @file graph_exec.c
 * @brief
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
            printf("%sError while building '%s'!\nSee pbuilder_logs/%s.log%s\n", C_RED, target, target, C_NORMAL);
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
            pb_debug(2, DBG_EXEC, "Position free: %d", i);
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

static PBResult pb_th_add_to_pool(PBMain pg, pthread_t *tid, gushort avail_pos)
{
    if (!pg || !tid)
        return PB_FAIL;

    *(pg->th_pool + avail_pos) = *tid;
    pb_debug(2, DBG_EXEC, "Thread %lu assigned to position %d\n", *tid, avail_pos);

    return PB_OK;
}

static gpointer pb_node_build_th(gpointer data)
{
    pthread_t   tid;
    PBNode      node = data;
    GString     *cmd,
                *logs;
    gulong      elapsed_usecs = 0;
	gint        ret,
    			have_logs = 0,
				pkg_build_failed = 0;
    gchar       *configdir;
    gchar       path[BUFF_8K];
    FILE        *fp = NULL,
                *flog = NULL;
    struct stat sb;

    if (!node)
        return NULL;

    configdir = getenv("CONFIG_DIR");
    if (!configdir) {
        pb_log(LOG_ERR, "%s(): Failed to get environament variable CONFIG_DIR", __func__);
        return NULL;
    }

    if (chdir(configdir)) {
        pb_log(LOG_ERR, "%s(): Failed to chdir to %s", __func__, configdir);
        return NULL;
    }

    tid = pthread_self();
    /* Add thread to pool */
    pb_th_add_to_pool(node->pg, &tid, node->pool_pos);

    /* Exec system() with make? */
    printf("Thread at position %d is building '%s'\n", node->pool_pos, node->name->str);

    cmd = g_string_new(NULL);
    /*g_string_printf(cmd, "make %s 1>/dev/null 2>/dev/null", node->name->str);*/
    g_string_printf(cmd, "make %s 2>&1", node->name->str);
    /*g_string_printf(cmd, "%s/brmake %s", configdir, node->name->str);*/

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
    		printf("%sError while building '%s'!\nSee pbuilder_logs/%s.log%s\n", C_RED, node->name->str, node->name->str, C_NORMAL);
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

void pb_node_get_max_priority(gpointer data, gpointer user_data)
{
    PBNode node = data;
    guint *max_prio = user_data;

    if (node->priority > *max_prio)
        *max_prio = node->priority;
}

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
                    printf("All threads are busy, waiting...\n");
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

