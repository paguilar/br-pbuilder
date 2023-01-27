/**
 * @file graph_create.c
 * @brief Functions used for creating the graph and give the build priority
 * to each node
 *
 * Copyright (C) 2022 Pedro Aguilar <paguilar@paguilar.org>
 * Released under the terms of the GNU GPL v2.0.
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

void pb_node_free(gpointer data)
{
    PBNode node = data;

    if (node->name)
        g_string_free(node->name, TRUE);

    if (node->version)
        g_string_free(node->version, TRUE);

    if (node->parents_str)
        g_strfreev(node->parents_str);

    if (node->childs)
        g_list_free(node->childs);

    if (node->parents)
        g_list_free(node->parents);

    g_rec_mutex_clear(&node->mutex);

    g_free(node);
}

void pb_graph_print_node_name(gpointer data, gpointer user_data)
{
    PBNode  node = data;
    printf("%s ", node->name->str);
}

void pb_graph_print(gpointer data, gpointer user_data)
{
    PBNode node = data;

    if (!node)
        return;

    printf("Package: %s%s%s\n", C_GREEN, node->name->str, C_NORMAL);
    if (node->version->len > 0)
        printf("\tVersion: %s\n", node->version->str);
    else
        printf("\tVersion: -\n");
    printf("\tPriority: %d\n", node->priority);
    printf("\tParents: ");
    g_list_foreach(node->parents, pb_graph_print_node_name, NULL);
    printf("\n\tChilds: ");
    g_list_foreach(node->childs, pb_graph_print_node_name, NULL);
    printf("\n");
}

static gint pb_graph_order_by_priority(gconstpointer a, gconstpointer b)
{
    PBNode node_a = (PBNode)a;
    PBNode node_b = (PBNode)b;

    if (node_a->priority < node_b->priority)
        return -1;
    else if (node_a->priority > node_b->priority)
        return 1;
    else
        return 0;
}

static PBResult pb_child_set_status_ready(PBNode node)
{
    if (node->status != PB_STATUS_PENDING)
        return PB_OK;

    node->status = PB_STATUS_READY;

    pb_debug(2, DBG_CREATE, "Node '%s' has build priority:    %d\n", node->name->str, node->priority);

    return PB_OK;
}

static void pb_child_set_prio(gpointer data, gpointer user_data)
{
    PBNode  parent = data;
    PBNode  child = user_data;

    if (parent->priority >= child->priority) {
        child->priority = parent->priority + 1;
    }
}

static void pb_are_parents_ready(gpointer data, gpointer user_data)
{
    PBNode      parent = data;
    gboolean    *parent_already_built = user_data;

    if (parent->status < PB_STATUS_READY)
        *parent_already_built = FALSE;
}

static void pb_child_calc_prio(gpointer data, gpointer user_data)
{
    PBNode      child = data;
    gboolean    all_parents_built = TRUE;

    /*printf("\t%s(): >>>\n", __func__);*/
    g_list_foreach(child->parents, pb_are_parents_ready, &all_parents_built);

    if (all_parents_built) {
        g_list_foreach(child->parents, pb_child_set_prio, child);
        pb_child_set_status_ready(child);
    }
}

static void pb_grandson_calc_prio(gpointer data, gpointer user_data)
{
    PBNode      child = data;

    pb_node_calc_prio(child);
}

/**
 * @brief Use the childs and parents lists of each node to calculate its priority
 */
PBResult pb_node_calc_prio(PBNode parent)
{
    if (!parent)
        return PB_FAIL;

    pb_debug(3, DBG_CREATE, "%s(): Processing '%s'\n", __func__, parent->name->str);

    g_list_foreach(parent->childs, pb_child_calc_prio, parent);

    g_list_foreach(parent->childs, pb_grandson_calc_prio, parent);

    return PB_OK;
}

static PBResult pb_graph_calc_nodes_priority(GList *graph)
{
    GList       *list;
    PBNode      node;
    gshort      prio = -1;
    gboolean    same_prio = FALSE;

    if (!graph)
        return PB_FAIL;

#if 1
    if (pb_node_calc_prio(graph->data) != PB_OK) {
        pb_log(PB_ERR, "Failed to build packages in the graph");
        return PB_FAIL;
    }
#else
    g_list_foreach(graph, pb_node_calc_prio, NULL);
#endif

#if 1
    /* TODO: Fix uclibc deps so this can be removed? */
    for (list = graph; list; list = list->next) {
        node = list->data;
        if (!strcmp(node->name->str, "uclibc")) {
            prio = node->priority;
            printf("uclibc priority is %d\n", node->priority);
            break;
        }
    }

    if (prio < 0)
        return PB_OK;

    /* Is there another package with the same priority? */
    for (list = graph; list; list = list->next) {
        node = list->data;
        if (node->priority == prio && strcmp(node->name->str, "uclibc")) {
            pb_debug(1, DBG_CREATE, "Package '%s' has the same priority %d\n",
                node->name->str, node->priority);
            same_prio = TRUE;
            break;
        }
    }

    if (same_prio == FALSE)
        return PB_OK;

    for (list = graph; list; list = list->next) {
        node = list->data;
        if (node->priority >= prio) {
            if (strcmp(node->name->str, "uclibc")) {
                node->priority++;
                pb_debug(2, DBG_CREATE, "Recalculating '%s' priority to %d\n",
                    node->name->str, node->priority);
            }
        }
    }
#endif

    return PB_OK;
}

/**
 * @brief Search the name of a node. It's called for each node in the graph
 * @param a A node in the graph
 * @param b The name of the node to search for
 * @return 0 if a node is found, 1 otherwise
 */
gint pb_node_find_by_name(gconstpointer a, gconstpointer b)
{
    const struct pbuilder_node_st *  node = a;
    const gchar   *str = b;

    pb_debug(3, DBG_CREATE, "\t%s - %s: ", str, node->name->str);

    if (!g_strcmp0(node->name->str, str)) {
        pb_debug(3, DBG_CREATE, "Found!!!!!\n");
        return 0;
    }
    else
        pb_debug(3, DBG_CREATE, "NOT found\n");

    return 1;
}

static void pb_node_link_single_parent_to_childs(gpointer data, gpointer user_data)
{
    PBNode      node = data,
                child_node;
    GList       *child_element;
    PBNodeName  name_in_graph = user_data;

    pb_debug(2, DBG_CREATE, "Linking parent %s to child %s\n", node->name->str, name_in_graph->name->str);

    child_element = g_list_find_custom(node->childs,
        name_in_graph->name->str,
        (GCompareFunc)pb_node_find_by_name);
    if (child_element)
        return;

    child_element = g_list_find_custom(name_in_graph->graph,
        name_in_graph->name->str,
        (GCompareFunc)pb_node_find_by_name);
    if (!child_element)
        return;

    child_node = (PBNode)child_element->data;
    node->childs = g_list_append(node->childs, child_node);
    return;
}

/**
 * @brief For each node in main graph and for each parent of those nodes,
 * add to the list of childs of each parent the node in the main graph.
 * The above two functions do the searching and linking.
 * @param data One node in the main graph
 * @param user_data The main graph
 */
void static pb_node_link_parents_to_childs(gpointer data, gpointer user_data)
{
    PBNode      node = data;
    PBNodeName  name_in_graph;

    name_in_graph = g_new0(struct pbuilder_node_name_in_graph_st, 1);
    name_in_graph->graph = (GList *)user_data;
    name_in_graph->name = node->name;

    g_list_foreach(node->parents, pb_node_link_single_parent_to_childs, name_in_graph);

    g_free(name_in_graph);
}

/**
 * @brief Add to the list of parents of the current node the node of a parent
 * once the parent's name has been found in the array of strings
 * (this array contains the names of the parent packages)
 * @param data The node to which its parents must be linked
 * @param user_data The main graph
 */
static void pb_node_link_childs_to_parents(gpointer data, gpointer user_data)
{
    GList       *graph = user_data,
                *parent_element;
    PBNode      node = data,
                parent_node;
    gchar       **p;

    if (!node->parents_str)
        return;

    pb_debug(2, DBG_CREATE, "Linking parents of %s\n", node->name->str);

    for (p = node->parents_str; *p != NULL; p++) {
        parent_element = g_list_find_custom(graph, *p, (GCompareFunc)pb_node_find_by_name);
        if (!parent_element)
            continue;

        parent_node = (PBNode)parent_element->data;
        if (parent_node) {
            /*if (!g_ptr_array_find(node->parents, parent_node, NULL)) {*/
            if (!g_list_find(node->parents, parent_node)) {
                pb_debug(2, DBG_CREATE, "\tAdding %s as parent of %s\n",
                    parent_node->name->str, node->name->str);
                node->parents = g_list_append(node->parents, parent_node);
            }
        }
    }

    /* Set the root parent 'ALL' to all orphan nodes */
    if (!node->parents) {
        parent_element = g_list_find_custom(graph, "ALL", (GCompareFunc)pb_node_find_by_name);
        parent_node = (PBNode)parent_element->data;
        node->parents = g_list_append(node->parents, parent_node);
    }
}

/**
 * @brief Create a single node and attach it to the graph. Each node represent a package.
 * Some info is calculated later and it's parents and childs are linked later.
 * @param pbg Main struct
 * @param graph The graph
 * @param node_info The node name
 * @param parents_str An array with the names of the node parents
 * @return The node
 */
static GList * pb_node_create(PBMain pbg, GList *graph, gchar **node_info)
{
    PBNode      node;
    gchar       *node_name,
                *node_ver,
                **p,
                **parents_str = NULL,
                *parent_list;

    if (!pbg || !node_info)
        return graph;

    node_name = *node_info;
    node_ver = *(node_info + 1);
    parent_list = *(node_info + 2);

    g_strchomp(node_name);

    /* Check if node already exists */
    if (g_list_find_custom(graph, node_name, (GCompareFunc)pb_node_find_by_name)) {
        return graph;
    }

    pb_debug(2, DBG_CREATE, "Parsing %s -> ", node_name);

    /* Set node name */
    node = g_new0(struct pbuilder_node_st, 1);

    node->name = g_string_new(NULL);
    g_string_printf(node->name, "%s", node_name);

    if (!g_strcmp0(node->name->str, "ALL"))
        node->status = PB_STATUS_DONE;
    else 
        node->status = PB_STATUS_PENDING;

    /* Set node version */
    if (node_ver && strlen(node_ver) > 0) {
        g_strstrip(node_ver);
        node->version = g_string_new(NULL);
        g_string_printf(node->version, "%s", node_ver);
    }
    else {
        node->version = g_string_new(NULL);
    }

    /* Set node parent list */
    if (parent_list && strlen(parent_list) > 0) {
        g_strchug(parent_list);
        g_strchomp(parent_list);

        parents_str = g_strsplit(parent_list, " ", 0);
        for (p = parents_str; *p != NULL; p++)
            pb_debug(2, DBG_CREATE, "%s ", *p);
        pb_debug(2, DBG_CREATE, "\n");
    }

    node->parents_str = parents_str;
    node->parents = NULL;
    node->childs = NULL;
    node->pg = pbg;
    g_rec_mutex_init(&node->mutex);

    graph = g_list_append(graph, node);

    pb_debug(2, DBG_CREATE, "\tNode created: %s\n", node->name->str);

    return graph;
}

/**
 * @brief For each package in the dependencies file, create a node of the graph
 * that represents a package and link it to its parents and childs.
 * @param pbg Main struct
 * @return PB_OK if successful, PB_FAIL otherwise
 */
static PBResult pb_graph_create_from_deps_file(PBMain pbg)
{
    char            line[BUFF_4K];
    FILE            *fd;
    GList           *graph = NULL;
    gchar           *root_node[] = {"ALL", "", ""};

    pb_debug(2, DBG_CREATE, "-----\nCreate each single node\n-----\n");

    /* Create graph's root node */
    if ((graph = pb_node_create(pbg, graph, root_node)) == NULL) {
        pb_print_err("Failed to create root node");
        pb_log(PB_ERR, "%s(): Failed to create root node", __func__);
        return PB_FAIL;
    }

    if ((fd = fopen(deps_file, "r")) == NULL) {
        pb_log(PB_ERR, "%s(): open(): %s: %s", __func__, deps_file, strerror(errno));
        return PB_FAIL;
    }

    memset(line, 0, BUFF_4K);

    while (fgets(line, BUFF_4K, fd)) {
        gchar   **node_info;

        if (!strncmp(line, "#", 1))
            continue;

        /* g_strsplit() doesn't return NULL */
        node_info = g_strsplit(line, ":", 3);

        /* Create new node and its parents nodes */
        if ((graph = pb_node_create(pbg, graph, node_info)) == NULL) {
            pb_print_err("Failed to create node '%s' or one of its parents", *node_info);
            pb_log(PB_ERR, "%s(): Failed to create node '%s' or one of its parents", __func__, *node_info);
            g_strfreev(node_info);
            return PB_FAIL;
        }

        g_strfreev(node_info);
    }

    fclose(fd);

    /* Link childs to parents */
    pb_debug(2, DBG_CREATE, "\n-----\nLink childs to parents\n-----\n");
    g_list_foreach(graph, pb_node_link_childs_to_parents, graph);

    /* Link parents to childs */
    pb_debug(2, DBG_CREATE, "\n-----\nLink parents to childs\n-----\n");
    g_list_foreach(graph, pb_node_link_parents_to_childs, graph);

    pbg->graph = graph;

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
    if (!pbg)
        return PB_FAIL;

    pbg->th_pool = g_new0(gint64, pbg->cpu_num);

    return PB_OK;
}

/**
 * @brief Free the main graph and free the threads pool
 * @param pbg Main struct
 */
void pb_graph_free(PBMain pbg)
{
    if (!pbg)
        return;

    if (pbg->graph) {
        g_list_free_full(pbg->graph, pb_node_free);
    }

    if (pbg->th_pool)
        g_free(pbg->th_pool);
        /*g_thread_pool_free(pbg->th_pool, TRUE, TRUE);*/

    g_free(pbg);
}

/**
 * @brief Create the threads pool, create the graph from the dependencies file
 * and assign a priority to each node
 * @param pbg Main struct
 * @return PB_OK if successful, PB_FAIL otherwise
 */
PBResult pb_graph_create(PBMain pg)
{
    if (!pg)
        return PB_FAIL;

    if (pb_th_init_pool(pg) != PB_OK) {
        pb_log(PB_ERR, "Failed to init thread pool");
        pb_graph_free(pg);
        return PB_FAIL;
    }

    if (pb_graph_create_from_deps_file(pg) != PB_OK) {
        pb_log(PB_ERR, "Failed to create graph");
        pb_graph_free(pg);
        return PB_FAIL;
    }

    if (pb_graph_calc_nodes_priority(pg->graph) != PB_OK) {
        pb_log(PB_ERR, "Failed to build graph");
        pb_graph_free(pg);
        return PB_FAIL;
    }

    pg->graph = g_list_sort(pg->graph, pb_graph_order_by_priority);

    if (debug_level >= 1) {
        pb_debug(1, DBG_ALL, "----- Graph organization -----\n");
        g_list_foreach(pg->graph, pb_graph_print, NULL);
        pb_debug(1, DBG_ALL, "-----\n\n");
    }

    return PB_OK;
}

