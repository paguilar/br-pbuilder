#!/bin/bash
#
# Adds symlinks inside the Buildroot sources and patches its main Makefile
# so when invoking "make pbuilder" it knows from where to get the builder
# binary and script.
# The remove option deletes the symlinks and unpatches the Makefile.
#
# Copyright (C) 2022 Pedro Aguilar <paguilar@paguilar.org>
# Released under the terms of the GNU GPL v2.0.
#

usage()
{
    printf "
$0 -b <buildroot_path> [-r] [-h]

Installs Buildroot's parallel builder

-b <buildroot_path>  Path to Buildroot top directory
-r                   Remove the builder and clean
-h                   This help
"
}

install()
{
    echo "Creating symlinks..."
    cd $BR_PATH/utils
    ln -s $PBUILDER_PATH pbuilder 

    cd $BR_PATH/support/scripts
    ln -s $PBUILDER_PATH/scripts/pbuilder.py .

    echo "Patching Buildroot Makefile..."
    cd $BR_PATH
    patch -p1 < $PBUILDER_PATH/patches/br2_makefile_add_pbuilder.patch
}

remove()
{
    echo "Removing symlinks..."
    rm -f $BR_PATH/utils/pbuilder
    rm -f $BR_PATH/support/scripts/pbuilder.py

    echo "Restoring Buildroot Makefile..."
    cd $BR_PATH
    patch -R -p1 < $PBUILDER_PATH/patches/br2_makefile_add_pbuilder.patch
}

REMOVE=0
BR_PATH=""

while getopts "hb:r" OPTION; do
    case "$OPTION" in
        b)
            BR_PATH="${OPTARG}"
            ;;
        r)
            REMOVE=1
            ;;
        h)
            usage && exit 0
            ;;
        *)
            usage && exit 1
            ;;
    esac
done

[ "$BR_PATH" == "" ] && usage && exit 1

if [ ! -d "$BR_PATH" ]; then
    echo "Buildroot path does not exist or is not a directory"
    exit 1
fi

PBUILDER_PATH=$(pwd)
if [ ! -d "$PBUILDER_PATH/src" -o ! -d "$PBUILDER_PATH/scripts" ]; then
    echo "Failed to find source or scripts paths."
    echo "Execute this script from the project top dir."
    exit 1
fi

if [ $REMOVE -eq 0 ]; then
    install
else
    remove
fi

exit 0
