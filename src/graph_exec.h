/**
 * @file graph_exec.h
 * @brief 
 *
 * Copyright (C) 2022 Pedro Aguilar <paguilar@paguilar.org>
 * Released under the terms of the GNU GPL v2.0.
 *
 */

#ifndef _GRAPH_EXEC_H_
#define _GRAPH_EXEC_H_

#include "graph_create.h"
#include "utils.h"

PBResult    pb_graph_exec(PBMain);
void        pb_th_wait_for_all_threads(PBMain);
PBResult    pb_finalize_single_target(PBMain, const gchar *);

#endif  /* _GRAPH_EXEC_H_ */
