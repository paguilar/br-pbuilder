
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <glib-2.0/glib.h>

#include "graph_create.h"
#include "graph_exec.h"

GPResult pg_finalize_target(GPMain pg, const gchar *target)
{
    gint    ret;
    GString *cmd;
    GTimer  *timer;
    gdouble elapsed_time;
    gulong  elapsed_usecs = 0;

    if (!pg || !target)
        return GP_FAIL;

    cmd = g_string_new(NULL);
    g_string_printf(cmd, "make %s 1>/dev/null 2>/dev/null", target);

    timer = g_timer_new();

    ret = system(cmd->str);
    if (ret != 0)
        pg_log(LOG_ERR, "%s(): Error while building '%s'", __func__, target);

    g_timer_stop(timer);
    elapsed_time = g_timer_elapsed(timer, &elapsed_usecs);

    printf("Package '%s' built in %f secs\n", target, elapsed_time);
    g_timer_destroy(timer);

    g_string_free(cmd, TRUE);

    return GP_OK;
}

static GPResult finalize_targets(GPMain pg)
{
    if (!pg)
        return GP_FAIL;

    if (pg_finalize_target(pg, "host-finalize") != GP_OK) {
        pg_log(LOG_ERR, "%s(): Failed to execute 'host-finalize' target", __func__);
        return GP_FAIL;
    }

    if (pg_finalize_target(pg, "staging-finalize") != GP_OK) {
        pg_log(LOG_ERR, "%s(): Failed to execute 'staging-finalize' target", __func__);
        return GP_FAIL;
    }

    if (pg_finalize_target(pg, "target-finalize") != GP_OK) {
        pg_log(LOG_ERR, "%s(): Failed to execute 'target-finalize' target", __func__);
        return GP_FAIL;
    }

    if (pg_finalize_target(pg, "target-post-image") != GP_OK) {
        pg_log(LOG_ERR, "%s(): Failed to execute 'target-post-image' target", __func__);
        return GP_FAIL;
    }

    return GP_OK;
}


void pg_th_wait_for_all_threads(GPMain pg)
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

gshort pg_th_get_avail_thread(GPMain pg)
{
    gushort i;

    if (!pg)
        return -1;

    for (i = 0; i < pg->cpu_num; i++) {
        if (*(pg->th_pool + i) == 0) {
            pg_log(2, DBG_EXEC, "Position free: %d", i);
            return i;
        }
    }

    /*printf("No available position found\n");*/
    return -1;
}


gshort pg_th_wait_for_avail_thread(GPMain pg)
{
    gshort avail_pos = -1;

    if (!pg)
        return avail_pos;

    while ((avail_pos = pg_th_get_avail_thread(pg)) < 0) {
        /*printf("    %s(): Waiting...\n", __func__);*/
        usleep(250000);
    }

    return avail_pos;
}


GPResult pg_th_remove_from_pool(GPMain pg, pthread_t *tid)
{
    gushort i;

    if (!pg || !tid)
        return GP_FAIL;

    for (i = 0; i < pg->cpu_num; i++) {
        /*printf("Comparing %lu - %lu\n", *(pg->th_pool + i), *tid);*/
        if (*(pg->th_pool + i) == *tid) {
            pg_debug(2, DBG_EXEC, "%sFreeing thread at position %d\n", C_GREEN, i);
            *(pg->th_pool + i) = 0;
            return GP_OK;
        }
    }

    pg_log(GP_ERR, "Trying to remove unregister thread %lu\n", *tid);

    return GP_FAIL;
}

GPResult pg_th_add_to_pool(GPMain pg, pthread_t *tid, gushort avail_pos)
{
    if (!pg || !tid)
        return GP_FAIL;

    *(pg->th_pool + avail_pos) = *tid;
    pg_debug(2, DBG_EXEC, "Thread %lu assigned to position %d\n", *tid, avail_pos);

    return GP_OK;
}

gpointer pg_node_build(gpointer data)
{
    pthread_t   tid;
    GPNodeBuild node_building = data;
    GPNode      node;
    GString     *cmd,
                *logs;
    gulong      elapsed_usecs = 0;
    /*gint        ret;*/
    gchar       *configdir;
    gchar       path[BUFF_8K];
    FILE        *fp = NULL,
                *flog = NULL;
    struct stat sb;
    gint        have_logs = 0;

    if (!node_building)
        return NULL;

    configdir = getenv("CONFIG_DIR");
    if (!configdir) {
        pg_log(LOG_ERR, "%s(): Failed to get environament variable CONFIG_DIR", __func__);
        return NULL;
    }

    if (chdir(configdir)) {
        pg_log(LOG_ERR, "%s(): Failed to chdir to %s", __func__, configdir);
        return NULL;
    }

    node = node_building->node;

    tid = pthread_self();
    /* Add thread to pool */
    pg_th_add_to_pool(node_building->pg, &tid, node_building->avail_pos);

    /* Exec system() with make? */
    printf("%sThread at position %d is building '%s'\n", C_GREEN, node_building->avail_pos, node->name->str);

    cmd = g_string_new(NULL);
    /*g_string_printf(cmd, "make %s 1>/dev/null 2>/dev/null", node->name->str);*/
    g_string_printf(cmd, "make %s 2>&1", node->name->str);
    /*g_string_printf(cmd, "%s/brmake %s", configdir, node->name->str);*/

    node->timer = g_timer_new();

#if 1
    logs = g_string_new("pbuilder_logs");

    if ((stat(logs->str, &sb) == 0) && S_ISDIR(sb.st_mode))
        have_logs = 1;
    else
        if (mkdir(logs->str, S_IRWXU) != 0)
            pg_log(LOG_DEBUG, "%s(): mkdir(): %s: %s", __func__, logs->str, strerror(errno));

    g_string_printf(logs, "pbuilder_logs/%s.log", node->name->str);
    if ((flog = fopen(logs->str, "a")) != NULL)
        have_logs = 1;
    else
        pg_log(LOG_ERR, "%s(): fopen(): %s: %s", __func__, logs->str, strerror(errno));

#endif

    fp = popen(cmd->str, "r");
    if (fp == NULL)
        pg_log(LOG_ERR, "%s(): Error while building '%s'", __func__, node->name->str);
    else {
        while (fgets(path, sizeof(path), fp) != NULL) {
            if (have_logs)
                fwrite(path, sizeof(char), strlen(path), flog);
            if (!strncmp(path, "\E[7m>>>", 7))
                printf("%s", path);
        }

        fclose(fp);
    }

    if (have_logs)
        fclose(flog);

    g_string_free(logs, TRUE);

    /*ret = system(cmd->str);*/
    /*if (ret != 0)*/
    /*pg_log(LOG_ERR, "%s(): Error while building '%s'", __func__, node->name->str);*/

    g_timer_stop(node->timer);
    node->elapsed_secs = g_timer_elapsed(node->timer, &elapsed_usecs);

    printf("%sPackage '%s' built in %f secs\n", C_GREEN, node->name->str, node->elapsed_secs);
    g_timer_destroy(node->timer);

    g_string_free(cmd, TRUE);

    /* Remove thread from pool */
    if (pg_th_remove_from_pool(node->pg, &tid) != GP_OK) {
        pg_log(GP_ERR, "%s(): Failed to remove thread (%lu) from pool!", __func__, tid);
    }

    g_free(node_building);

    node->status = GP_STATUS_DONE;

    /*if (ret != 0) {*/
    /*pg_log(LOG_ERR, "%s(): Exiting!", __func__);*/
    /*_exit(EXIT_FAILURE);*/
    /*}*/

    return NULL;
}

void pg_node_get_max_priority(gpointer data, gpointer user_data)
{
    GPNode node = data;
    guint *max_prio = user_data;

    if (node->priority > *max_prio)
        *max_prio = node->priority;
}

GPResult pbg_graph_exec(GPMain pg)
{
    GList       *list;
    guint       i,
                prio_num = 0;
    gshort      avail_pos;
    GPNode      node;
    pthread_t   th;
    GPNodeBuild node_building;
    gulong      elapsed_usecs = 0;

    if (!pg)
        return GP_FAIL;

    g_list_foreach(pg->graph, pg_node_get_max_priority, &prio_num);
    pg_debug(1, DBG_ALL, "Number of priorities found: %d\n", prio_num);

    pg->timer = g_timer_new();

    for (i = 1; i <= prio_num; i++) {
        printf("%s========== Processing packages with priority %d...\n", C_RED, i);
        for (list = pg->graph; list != NULL; list = list->next) {
            node = list->data;
            if (node->priority == i) {
                printf("%sProcessing '%s' (%d)\n", C_GREEN, node->name->str, node->priority);

                avail_pos = pg_th_get_avail_thread(pg);
                if (avail_pos < 0) {
                    printf("%sAll threads are busy, waiting...\n", C_GREEN);
                    avail_pos = pg_th_wait_for_avail_thread(pg);
                }

                node->status = GP_STATUS_BUILDING;

                node_building = g_new0(struct pg_node_building_st, 1);
                node_building->node = node;
                node_building->pg = pg;
                node_building->node = node;
                node_building->avail_pos = avail_pos;

                /* TODO Reserve position in pool */
                pg->th_pool[avail_pos] = -1;

                pthread_create(&th, NULL, pg_node_build, node_building);
            }
        }
        printf("\n");
        pg_th_wait_for_all_threads(pg);
    }

    if (finalize_targets(pg) != GP_OK)
        pg_log(LOG_ERR, "%s(): Failed to finalize targets", __func__);

    g_timer_stop(pg->timer);
    pg->elapsed_secs = g_timer_elapsed(pg->timer, &elapsed_usecs);

    printf("%s===== Total elapsed time: %f\n", C_RED, pg->elapsed_secs);
    g_timer_destroy(pg->timer);
    pg->timer = NULL;

    return GP_OK;
}


