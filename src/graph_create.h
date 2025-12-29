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

#include "graph_common.h"
#include "utils.h"

typedef struct pbuilder_node_name_in_graph_st * PBNodeName;

/**
 * Aux struct used for passing parameters while linking
 * parents to children nodes during the graph creation
 */
struct pbuilder_node_name_in_graph_st
{
    GList           *graph;
    GString         *name;
};


PBResult    pb_node_calc_prio(PBNode);
PBResult    pb_graph_create(PBMain);
void        pb_graph_free(PBMain);

#endif  /* _GRAPH_CREATE_H_ */
