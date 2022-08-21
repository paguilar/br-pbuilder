/**
 * @file graph_create.c
 * @brief
 *
 * Author: Pedro Aguilar
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

#if 0
static gint search_dep_in_br_pkg_list(gconstpointer data, gconstpointer user_data)
{
    const GString *pkg_name = data;
    const gchar   *node_name = user_data;

    /* TODO Remove 'host-' from dep name */
    if (!strncmp(node_name, "host-", 5)) {
        if (!strcmp(pkg_name->str, node_name + 5)) {
            printf("\tMatch %s!!!\n", pkg_name->str);
            return 0;
        }
    }
    else {
        if (!strcmp(pkg_name->str, node_name)) {
            printf("\tMatch %s!!!\n", pkg_name->str);
            return 0;
        }
    }

    return 1;
}
#endif

void pg_node_free(gpointer data)
{
    GPNode node = data;

    if (node->name)
        g_string_free(node->name, TRUE);

    if (node->parents_str)
        g_strfreev(node->parents_str);

    if (node->childs)
        g_list_free(node->childs);

    if (node->parents)
        g_list_free(node->parents);

    g_rec_mutex_clear(&node->mutex);

    g_free(node);
}

static void pg_graph_free(GPMain pg)
{
    if (!pg)
        return;

    if (pg->graph) {
        g_list_free_full(pg->graph, pg_node_free);
    }

    if (pg->th_pool)
        g_free(pg->th_pool);
        /*g_thread_pool_free(pg->th_pool, TRUE, TRUE);*/

    g_free(pg);
}

void pg_graph_print_node_name(gpointer data, gpointer user_data)
{
    GPNode  node = data;
    printf("%s ", node->name->str);
}

void pg_graph_print(gpointer data, gpointer user_data)
{
    GPNode node = data;

    if (!node)
        return;

    printf("Package: %s%s%s\n", C_GREEN, node->name->str, C_NORMAL);
    printf("\tPriority: %d\n", node->priority);
    printf("\tParents: ");
    g_list_foreach(node->parents, pg_graph_print_node_name, NULL);
    printf("\n\tChilds: ");
    g_list_foreach(node->childs, pg_graph_print_node_name, NULL);
    printf("\n");
}

static gint pg_graph_order_by_priority(gconstpointer a, gconstpointer b)
{
    GPNode node_a = (GPNode)a;
    GPNode node_b = (GPNode)b;

    if (node_a->priority < node_b->priority)
        return -1;
    else if (node_a->priority > node_b->priority)
        return 1;
    else
        return 0;
}

static GPResult pg_child_set_status_ready(GPNode node)
{
    if (node->status != GP_STATUS_PENDING)
        return GP_OK;

    node->status = GP_STATUS_READY;

    pg_debug(2, DBG_CREATE, "Node '%s' has build priority:    %d\n", node->name->str, node->priority);

    return GP_OK;
}

static void pg_child_set_prio(gpointer data, gpointer user_data)
{
    GPNode  parent = data;
    GPNode  child = user_data;

    if (parent->priority >= child->priority) {
        child->priority = parent->priority + 1;
    }
}

static void pg_are_parents_ready(gpointer data, gpointer user_data)
{
    GPNode      parent = data;
    gboolean    *parent_already_built = user_data;

    if (parent->status < GP_STATUS_READY)
        *parent_already_built = FALSE;
}

static void pg_child_calc_prio(gpointer data, gpointer user_data)
{
    GPNode      child = data;
    gboolean    all_parents_built = TRUE;

    /*printf("\t%s(): >>>\n", __func__);*/
    g_list_foreach(child->parents, pg_are_parents_ready, &all_parents_built);

    if (all_parents_built) {
        g_list_foreach(child->parents, pg_child_set_prio, child);
        pg_child_set_status_ready(child);
    }
}

#if 1
static void pg_grandson_calc_prio(gpointer data, gpointer user_data)
{
    GPNode      child = data;

    pg_node_calc_prio(child);
}

GPResult pg_node_calc_prio(GPNode parent)
{
    if (!parent)
        return GP_FAIL;

    pg_debug(3, DBG_CREATE, "%s(): Processing '%s'\n", __func__, parent->name->str);

    g_list_foreach(parent->childs, pg_child_calc_prio, parent);

    g_list_foreach(parent->childs, pg_grandson_calc_prio, parent);

    return GP_OK;
}
#else
static void pg_node_calc_prio(gpointer data, gpointer user_data)
{
    GPNode      parent = data;

    printf("%s(): ----------> %s\n", __func__, parent->name->str);
    g_list_foreach(parent->childs, pg_child_calc_prio, parent);

}
#endif

static GPResult pg_graph_calc_nodes_priority(GList *graph)
{
    GList       *list;
    GPNode      node;
    gshort      prio = -1;
    gboolean    same_prio = FALSE;

    if (!graph)
        return GP_FAIL;

#if 1
    if (pg_node_calc_prio(graph->data) != GP_OK) {
        pg_log(GP_ERR, "Failed to build packages in the graph");
        return GP_FAIL;
    }
#else
    g_list_foreach(graph, pg_node_calc_prio, NULL);
#endif

#if 1
    /* Get uclibc priority */
    for (list = graph; list; list = list->next) {
        node = list->data;
        if (!strcmp(node->name->str, "uclibc")) {
            prio = node->priority;
            printf("uclibc priority is %d\n", node->priority);
            break;
        }
    }

    if (prio < 0)
        return GP_OK;

    /* Is there another package with the same priority? */
    for (list = graph; list; list = list->next) {
        node = list->data;
        if (node->priority == prio && strcmp(node->name->str, "uclibc")) {
            pg_debug(1, DBG_CREATE, "Package '%s' has the same priority %d\n", node->name->str, node->priority);
            same_prio = TRUE;
            break;
        }
    }

    if (same_prio == FALSE)
        return GP_OK;

    for (list = graph; list; list = list->next) {
        node = list->data;
        if (node->priority >= prio) {
            if (strcmp(node->name->str, "uclibc")) {
                node->priority++;
                pg_debug(2, DBG_CREATE, "Recalculating '%s' priority to %d\n", node->name->str, node->priority);
            }
        }
    }
#endif


    return GP_OK;
}

gint pg_node_find_by_name(gconstpointer a, gconstpointer b)
{
    const struct pgbuild_node_st *  node = a;
    const gchar   *str = b;

    pg_debug(3, DBG_CREATE, "\t%s - %s: ", str, node->name->str);

    if (!g_strcmp0(node->name->str, str)) {
        pg_debug(3, DBG_CREATE, "Found!!!!!\n");
        return 0;
    }
    else
        pg_debug(3, DBG_CREATE, "NOT found\n");

    return 1;
}

static void pg_node_link_single_parent_to_childs(gpointer data, gpointer user_data)
{
    GPNode      node = data,
                child_node;
    GList       *child_element;
    GPNodeName  name_in_graph = user_data;

    pg_debug(2, DBG_CREATE, "Linking parent %s to child %s\n", node->name->str, name_in_graph->name->str);

    child_element = g_list_find_custom(node->childs, name_in_graph->name->str, (GCompareFunc)pg_node_find_by_name);
    if (child_element)
        return;

    child_element = g_list_find_custom(name_in_graph->graph, name_in_graph->name->str, (GCompareFunc)pg_node_find_by_name);
    if (!child_element)
        return;

    child_node = (GPNode)child_element->data;
    node->childs = g_list_append(node->childs, child_node);
    return;
}

/**
 * @brief For each node in main graph
 */
void pg_node_link_parents_to_childs(gpointer data, gpointer user_data)
{
    GPNode      node = data;
    GPNodeName  name_in_graph;

    name_in_graph = g_new0(struct pgbuild_node_name_in_graph_st, 1);
    name_in_graph->graph = (GList *)user_data;
    name_in_graph->name = node->name;

    g_list_foreach(node->parents, pg_node_link_single_parent_to_childs, name_in_graph);

    g_free(name_in_graph);
}

void pg_node_link_childs_to_parents(gpointer data, gpointer user_data)
{
    GList       *graph = user_data,
                *parent_element;
    GPNode      node = data,
                parent_node;
    gchar       **p;

    if (!node->parents_str)
        return;

    pg_debug(2, DBG_CREATE, "Linking parents of %s\n", node->name->str);

    for (p = node->parents_str; *p != NULL; p++) {
        parent_element = g_list_find_custom(graph, *p, (GCompareFunc)pg_node_find_by_name);
        if (!parent_element)
            continue;

        parent_node = (GPNode)parent_element->data;

        if (parent_node) {
            /*if (!g_ptr_array_find(node->parents, parent_node, NULL)) {*/
            if (!g_list_find(node->parents, parent_node)) {
                pg_debug(2, DBG_CREATE, "\tAdding %s as parent of %s\n", parent_node->name->str, node->name->str);
                node->parents = g_list_append(node->parents, parent_node);
            }
        }
    }

    /* Set the root parent 'ALL' to all orphan nodes */
    if (!node->parents) {
        parent_element = g_list_find_custom(graph, "ALL", (GCompareFunc)pg_node_find_by_name);
        parent_node = (GPNode)parent_element->data;
        node->parents = g_list_append(node->parents, parent_node);
    }
}

static GList * pg_node_create(GPMain pg, GList *graph, gchar *node_name, gchar **parents_str)
{
    GPNode      node;

    if (!pg || !node_name)
        return graph;

    /* Check if node already exists */
    if (g_list_find_custom(graph, node_name, (GCompareFunc)pg_node_find_by_name)) {
        return graph;
    }

    node = g_new0(struct pgbuild_node_st, 1);

    node->name = g_string_new(NULL);
    g_string_printf(node->name, "%s", node_name);

    if (!g_strcmp0(node->name->str, "ALL"))
        node->status = GP_STATUS_DONE;
    else 
        node->status = GP_STATUS_PENDING;

    node->parents_str = parents_str;
    node->parents = NULL;
    node->childs = NULL;
    node->pg = pg;
    g_rec_mutex_init(&node->mutex);

    graph = g_list_append(graph, node);

    pg_debug(2, DBG_CREATE, "\tNode created: %s\n", node->name->str);

    return graph;
}

static GPResult pg_graph_create(GPMain pg)
{
    char            line[BUFF_4K];
    FILE            *fd;
    GList           *graph = NULL;

    pg_debug(2, DBG_CREATE, "-----\nCreate each single node\n-----\n");

    /* Create graph's root node */
    if ((graph = pg_node_create(pg, graph, "ALL", NULL)) == NULL) {
        printf("%s(): Failed to create root node", __func__);
        pg_log(GP_ERR, "%s(): Failed to create root node", __func__);
        return GP_FAIL;
    }

    if ((fd = fopen(deps_file, "r")) == NULL) {
        pg_log(GP_ERR, "%s(): open(): %s: %s", __func__, deps_file, strerror(errno));
        return GP_FAIL;
    }

    memset(line, 0, BUFF_4K);

    while (fgets(line, BUFF_4K, fd)) {
        gchar   **node_name,
                **parents_str,
                **p;
        gchar   *parent_list;
        /*GList   *pkg_found;*/

        if (!strncmp(line, "#", 1))
            continue;

        node_name = g_strsplit(line, ":", 2);
        /* FIXME: if (node_name == NULL)*/

        g_strchomp(*node_name);

#if 0
        /* FIXME Discard dependencies that are no real packages */
        pkg_found = g_list_find_custom(pg->br_pkg_list, *node_name, search_dep_in_br_pkg_list);
        if (!pkg_found) {
            printf("----------------> Skipping %s...\n", *node_name);
            pg_debug(2, DBG_CREATE, "Skipping %s...\n", *node_name);
            g_strfreev(node_name);
            continue;
        }
#else
        /*if (!strncmp(*node_name, "rootfs-", 7)) {*/
        /*pg_debug(2, DBG_CREATE, "Skipping %s...\n", *node_name);*/
        /*g_strfreev(node_name);*/
        /*continue;*/
        /*}*/
#endif

        pg_debug(2, DBG_CREATE, "Processing %s -> ", *node_name);

        parent_list = *(node_name + 1);
        if (parent_list) {
            g_strchug(parent_list);
            g_strchomp(parent_list);
        }

        parents_str = g_strsplit(parent_list, " ", 0);
        for (p = parents_str; *p != NULL; p++) {
            pg_debug(2, DBG_CREATE, "%s ", *p);
        }
        pg_debug(2, DBG_CREATE, "\n");

        /* Create new node and its parents nodes */
        if ((graph = pg_node_create(pg, graph, *node_name, parents_str)) == NULL) {
            printf("%s(): Failed to create node '%s' or one of its parents", __func__, *node_name);
            pg_log(GP_ERR, "%s(): Failed to create node '%s' or one of its parents", __func__, *node_name);
            g_strfreev(parents_str);
            g_strfreev(node_name);
            return GP_FAIL;
        }

        g_strfreev(node_name);
    }

    fclose(fd);

    /* Link childs to parents */
    pg_debug(2, DBG_CREATE, "\n-----\nLink childs to parents\n-----\n");
    g_list_foreach(graph, pg_node_link_childs_to_parents, graph);

    /* Link parents to childs */
    pg_debug(2, DBG_CREATE, "\n-----\nLink parents to childs\n-----\n");
    g_list_foreach(graph, pg_node_link_parents_to_childs, graph);

    pg->graph = graph;

    return GP_OK;
}

/*
 * TODO See support/scripts/pkg-stats
 * Scan WALK_USEFUL_SUBDIRS and discard WALK_EXCLUDES
 */
static GPResult br_pkg_list_create(GPMain pg)
{
    gchar       *topdir;
    const gchar *entry;
    GString     *pkgs_path,
                *pkg_name_linux;
    GDir        *dir;
    gushort     i = 0;
    struct stat sb;

    pg_debug(2, DBG_CREATE, "Obtaining list of availables packages\n");

    topdir = getenv("TOPDIR");
    if (!topdir) {
        pg_log(LOG_ERR, "%s(): Failed to get environament variable TOPDIR", __func__);
        return GP_FAIL;
    }

    pkgs_path= g_string_new(NULL);

    /* Get package list in $(TOPDIR)/package */
    g_string_printf(pkgs_path, "%s/package", topdir);

    if ((dir = g_dir_open(pkgs_path->str, 0, NULL)) == NULL) {
        pg_log(LOG_ERR, "%s(): Failed to open path '%s'", __func__, pkgs_path->str);
        g_string_free(pkgs_path, TRUE);
        return GP_FAIL;
    }

    while ((entry = g_dir_read_name(dir))) {
        GString *pkg_name;

        pkg_name = g_string_new(NULL);
        g_string_printf(pkg_name, "%s/%s", pkgs_path->str, entry);

        if (stat(pkg_name->str, &sb) != 0)
            continue;

        if ((sb.st_mode & S_IFMT) != S_IFDIR) {
            pg_debug(1, DBG_CREATE, "\tSkipping entry '%s'\n", entry);
            continue;
        }

        g_string_printf(pkg_name, "%s", entry);

        pg->br_pkg_list = g_list_append(pg->br_pkg_list, pkg_name);
        i++;
    }

    g_dir_close(dir);

    /* Get package list in $(TOPDIR)/boot */
    g_string_printf(pkgs_path, "%s/boot", topdir);

    if ((dir = g_dir_open(pkgs_path->str, 0, NULL)) == NULL) {
        pg_log(LOG_ERR, "%s(): Failed to open path '%s'", __func__, pkgs_path->str);
        g_string_free(pkgs_path, TRUE);
        return GP_FAIL;
    }

    while ((entry = g_dir_read_name(dir))) {
        GString *pkg_name;

        pkg_name = g_string_new(NULL);
        g_string_printf(pkg_name, "%s/%s", pkgs_path->str, entry);

        if (stat(pkg_name->str, &sb) != 0)
            continue;

        if ((sb.st_mode & S_IFMT) != S_IFDIR) {
            pg_debug(2, DBG_CREATE, "\tSkipping entry '%s'\n", entry);
            continue;
        }

        g_string_printf(pkg_name, "%s", entry);

        pg->br_pkg_list = g_list_append(pg->br_pkg_list, pkg_name);
        i++;
    }

    g_dir_close(dir);

    /* Get package list in $(TOPDIR)/toolchain */
    g_string_printf(pkgs_path, "%s/toolchain", topdir);

    if ((dir = g_dir_open(pkgs_path->str, 0, NULL)) == NULL) {
        pg_log(LOG_ERR, "%s(): Failed to open path '%s'", __func__, pkgs_path->str);
        g_string_free(pkgs_path, TRUE);
        return GP_FAIL;
    }

    while ((entry = g_dir_read_name(dir))) {
        GString *pkg_name;

        pkg_name = g_string_new(NULL);
        g_string_printf(pkg_name, "%s/%s", pkgs_path->str, entry);

        if (lstat(pkg_name->str, &sb) != 0)
            continue;

        if ((sb.st_mode & S_IFMT) != S_IFDIR) {
            pg_debug(2, DBG_CREATE, "\tSkipping entry '%s'\n", entry);
            continue;
        }

        g_string_printf(pkg_name, "%s", entry);

        pg->br_pkg_list = g_list_append(pg->br_pkg_list, pkg_name);
        i++;
    }

    g_dir_close(dir);

    g_string_free(pkgs_path, TRUE);

    pkg_name_linux = g_string_new("linux");
    pg->br_pkg_list = g_list_prepend(pg->br_pkg_list, pkg_name_linux);
    

    /* TODO Add packages in BR2_EXTERNAL */

    /*for (GList *list = pg->br_pkg_list; list; list = list->next) {*/
    /*GString *elem = list->data;*/
    /*printf("pkg_name: %s\n", elem->str);*/
    /*}*/

    pg_debug(2, DBG_CREATE, "\tNumber of packages found: %d\n", i);

    return GP_OK;
}

static GPResult pg_th_init_pool(GPMain pg)
{
    if (!pg)
        return GP_FAIL;

    pg->th_pool = g_new0(gint64, pg->cpu_num);

    return GP_OK;
}


GPResult pbg_graph_create(GPMain *pbg)
{
    GPMain pg;

    pg = g_new0(struct pgbuild_main_st, 1);
    pg->graph = NULL;
    pg->timer = NULL;

    if (cpu_num < 1 || cpu_num > g_get_num_processors())
        pg->cpu_num = g_get_num_processors();
    else
        pg->cpu_num = cpu_num;

    if (pg_th_init_pool(pg) != GP_OK) {
        pg_log(GP_ERR, "Failed to init thread pool");
        pg_graph_free(pg);
        return GP_FAIL;
    }

#if 0
    if (br_pkg_list_create(pg) != GP_OK) {
        pg_log(GP_ERR, "Failed to create list of buildroot package names");
        pg_graph_free(pg);
        return GP_FAIL;
    }
#endif

    if (pg_graph_create(pg) != GP_OK) {
        pg_log(GP_ERR, "Failed to create graph");
        pg_graph_free(pg);
        return GP_FAIL;
    }

    if (pg_graph_calc_nodes_priority(pg->graph) != GP_OK) {
        pg_log(GP_ERR, "Failed to build graph");
        pg_graph_free(pg);
        return GP_FAIL;
    }

    pg->graph = g_list_sort(pg->graph, pg_graph_order_by_priority);

    if (debug_level >= 1) {
        pg_debug(1, DBG_ALL, "----- Graph organization -----\n");
        g_list_foreach(pg->graph, pg_graph_print, NULL);
        pg_debug(1, DBG_ALL, "-----\n\n");
    }

    *pbg = pg;

    return GP_OK;
}

void pbg_graph_free(GPMain pg)
{
    if (pg)
        pg_graph_free(pg);
}
