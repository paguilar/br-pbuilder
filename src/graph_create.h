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

#include "utils.h"

typedef enum
{
    PB_STATUS_PENDING,
    PB_STATUS_READY,
    PB_STATUS_BUILDING,
    PB_STATUS_DONE
} PBStatus;

typedef struct pbuilder_main_st *               PBMain;
typedef struct pbuilder_node_st *               PBNode;
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
    GString         *version;           /**< Package version */
    PBStatus        status;             /**< Node status */
    gushort         priority;           /**< Indicates when this node has to be built */
    GRecMutex       mutex;              /**< TODO Use it!!! */
    GCond           *cond;
    gchar           **parents_str;      /**< List of strings that contain the package parents */
    GList           *parents;           /**< List that points to this node's parents */
    GList           *childs;            /**< List that points to this node's children */
    gushort         pool_pos;           /**< Thread position in the pool that is building this node */
    PBMain          pg;                 /**< Pointer to the main struct */
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
    gboolean        build_error;        /**< An error occurred while building */
};

PBResult    pb_node_calc_prio(PBNode);
gint        pb_node_find_by_name(gconstpointer, gconstpointer);
PBResult    pb_graph_create(PBMain *);
void        pb_graph_free(PBMain);

#endif  /* _GRAPH_CREATE_H_ */
