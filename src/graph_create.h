/**
 * @file graph_create.h
 * @brief 
 *
 * Copyright (C) 2022 Pedro Aguilar <paguilar@paguilar.org>
 * Released under the terms of the GNU GPL v2.0.
 *
 */

#ifndef _GRAPH_CREATE_H_
#define _GRAPH_CREATE_H_

#include <glib.h>

#include "utils.h"

typedef enum
{
    GP_STATUS_PENDING,
    GP_STATUS_READY,
    GP_STATUS_BUILDING,
    GP_STATUS_DONE
} GPStatus;

typedef struct pbuilder_main_st *               GPMain;
typedef struct pbuilder_node_st *               GPNode;
typedef struct pbuilder_node_name_in_graph_st * PBNodeName;

/**
 * Aux struct used for passing parameters while linking
 * parents to childs nodes during the graph creation
 */
struct pbuilder_node_name_in_graph_st {
    GList           *graph;
    GString         *name;
};

/**
 * A node of the graph that represents a package to be built
 */
struct pbuilder_node_st
{
    GString         *name;              /**< Package name */
    GPStatus        status;             /**< Node status */
    gushort         priority;           /**< Indicates when this node has to be built */
    GRecMutex       mutex;              /**< TODO Use it!!! */
    GCond           *cond;
    gchar           **parents_str;      /**< List of strings that contain the package parents */
    GList           *parents;           /**< List that points to this node's parents */
    GList           *childs;            /**< List that points to this node's children */
    gushort         pool_pos;           /**< Thread position in the pool that is building this node */
    GPMain          pg;                 /**< Pointer to the main struct */
    GTimer          *timer;             /**< Timer needed to measure the node's building time */
    gdouble         elapsed_secs;       /**< Time required to build this node */
};

/**
 * Main struct
 */
struct pbuilder_main_st
{
    GList           *graph;             /**< Graph used to build */
    GList           *br_pkg_list;       /**< List of buildroot package names */
    gushort         cpu_num;            /**< Number of CPUs that determine the number of threads used to build */
    gint64          *th_pool;           /**< Pool of threads of size cpu_num */
    GTimer          *timer;             /**< Timer needed to measure the graph's building time */
    gdouble         elapsed_secs;       /**< Time required to build the whole graph */
};

GPResult    pg_node_calc_prio(GPNode);
gint        pg_node_find_by_name(gconstpointer, gconstpointer);
GPResult    pb_graph_create(GPMain *);
void        pb_graph_free(GPMain);

#endif  /* _GRAPH_CREATE_H_ */
