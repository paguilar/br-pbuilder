
# br-pbuilder (Buildroot top-level parallel builder)


## Overview

This program is intended for being called inside Buildroot for executing top-level parallel builds.

The task is accomplished using a graph organized in levels. Each node of the graph represents a
package to be built and each level contains all the nodes (packages) whose dependencies are nodes of
higher level.

The graph's root is a package that does not have any dependency on any package and this is the first
level. The second level contains packages whose only dependency is the graph's root; the third level
contains packages whose dependencies are the packages in the second and first levels; and so on.

This example shows a graph organization:
Node A is the graph's root, it doesn't have any dependency; therefore is level 1 and is considered
the highest level.
Nodes B and C depend only of node A; therefore are level 2.
Node D depends on nodes A and B; therefore is level 3, but also nodes E and F are in the same level
since their only dependency is node C that is level 2.
Node G depends on node D that is level 3 and node C that is level 2, since node D is the lowest
level, node G must be level 4.
Node H depends on nodes E and F that are level 3 and node C that is level 2, since nodes E and F
are the lowest level, node H must be level 4.


                     .----.
                     |    |
                     |  A |                              Level 1
                     |    |
                     '----'
                        |
          .----+--------+---------.
          |    |                  |
       .----.  |               .----.
       |    |  |               |    |
       |  B |  |               |  C |                    Level 2
       |    |  |               |    |
       '----'  |               '----'
          |    |                  |
          +----'  .------+--------+------+-----.
          |       |      |               |     |
       .----.     |   .----.          .----.   |
       |    |     |   |    |          |    |   |
       |  D |     |   |  E |          |  F |   |         Level 3
       |    |     |   |    |          |    |   |
       '----'     |   '----'          '----'   |
          |       |      |               |     |
          +-------'      '---------------+-----'
          |                              |
       .----.                         .----.
       |    |                         |    |
       |  G |                         |  H |             Level 4
       |    |                         |    |
       '----'                         '----'

This table summarizes the packages names, their levels and their dependencies in the above example:

| Package | Level | Dependencies |
|---------|-------|--------------|
|  A      |  1    |     None     |
|  B      |  2    |     A        |
|  C      |  2    |     A        |
|  D      |  3    |     A, B     |
|  E      |  3    |     C        |
|  F      |  3    |     C        |
|  G      |  4    |     C, D     |
|  H      |  4    |     C, E, F  |

Each package may have one or more dependencies and all of them must be in higher levels.


## Performance

Buildroot uses *make* -jN (or the equivalent invocation for other build systems) for building in
parallel each single package, but each one of them is built serially one after another. 

A top-level parallel build builds several packages at the same time significantly reducing the
overall building time taking care of the dependencies between the packages.

Notice that building a package includes several steps such as its configuration, compilation and
installation, but normally only the compilation step is done in parallel, all the other steps take
place in a serial fashion. Therefore a top-level parallel build takes special advantage when
executing those steps at the same time for different packages.

br-pbuilder has been tested in top-level parallel builds of default configurations (the *defconfigs
in /configs) as well as custom configurations with br2-external trees.

As the number of packages increases, as it tends to happen with custom configurations, the building
time shows a significant time decrease with respect to the default *make* and brmake.

br-pbuilder doesn't use a per-package directories mechanism using only the global *target* and
*host* directories keeping the storage footprint at minimum.


## Requirements

br-pbuilder is a C program, a Python script and standard *make* files for building the C program.
All its requirements are also mandatory for Buildroot, except the following two packages that are
optional for Buildroot:

- glib2
- python (version 3)


## How to use it

This program should be invoked as a target from Buildroot's Makefile, therefore the main Makefile
and some scripts need to be patched.

1. Download the sources:

```
git clone https://github.com/paguilar/br-pbuilder.git
```

2. Patch Buildroot:

```
$ cd br-pbuilder
$ ./scripts/install.sh -b <buildroot_path>
```

where *<buildroot_path>* is the path to Buildroot's top directory.
This script creates symlinks inside the Buildroot sources and patches its main Makefile and some
scripts like br2-external.

4. Configure Buildroot:

The Buildroot configuration remains exactly the same, nothing has been touched here. Use any of the
configuration methods such as 'make <defconfig>' or just something like 'make menuconfig'.

5. Execute the parallel builder:

From Buildroot's build path invoke the parallel builder:

```
$ make pbuilder
```

That's it. At this point the output of the main building steps of each package are displayed
organized by priority along with the total building time of each package and at the end the total
building time of the whole configuration. The complete output of each package and its errors, if
any, can be found in pbuilder_logs/<package>.log inside Buildroot's build path.

The *pbuilder* target executes first the python script that checks that the binary and the
dependencies file exist. If the binary is missing, it builds it. If the dependencies file is
missing, it creates it using the Makefile's *show-info* target.

In order to increase the verbosity during the br-pbuilder execution, add the -l N cmdline arg in
the Makefile target where it's invoked. N is the debug level that can be [1-3].


## Current status

As of today, this is just a simple proof of concept that seems to work with several built-in
defconfigs and projects with a br2-external tree.

It has been tested against Buildroot's stable release 2022.11 and recent commits of the master
branch of the official repo.

This program is not part of the offical Buildroot project. As a matter of fact, Buildroot already
offers this functionality as an experimental feature, but the manual warns that it may not work in
some scenarios and that has been the case before writing this program.

It still needs lots of testing and it may not work in some cases or could break some Buildroot's
rules/guidelines.

Any help and bug fixes are welcome!

