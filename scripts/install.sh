#!/bin/bash
#
# Adds symlinks inside the Buildroot sources and patches some makefiles
# so when invoking "make pbuilder" it knows from where to get the builder
# binary and script.
# The remove option deletes the symlinks and unpatches the Makefile.
#
# Copyright (C) 2022-2023 Pedro Aguilar <paguilar@paguilar.org>
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
    echo "creating symlinks..."
    cd $BR_PATH/utils
    ln -s $PBUILDER_PATH pbuilder

    cd $BR_PATH/support/scripts
    ln -s $PBUILDER_PATH/scripts/pbuilder.py .

    echo "patching Buildroot Makefile..."
    cd $BR_PATH
    for i in `ls $PBUILDER_PATH/patches/*.patch`; do
        patch -p1 < $i
    done
}

remove()
{
    echo "Removing symlinks..."
    rm -f $BR_PATH/utils/pbuilder
    rm -f $BR_PATH/support/scripts/pbuilder.py

    echo "Restoring makefiles..."
    cd $BR_PATH
    for i in `ls $PBUILDER_PATH/patches/*.patch`; do
        patch -R -p1 < $i
    done
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
if [ -r "$PBUILDER_PATH/install.sh" ]; then
    PBUILDER_PATH="$PBUILDER_PATH/.."
    cd $PBUILDER_PATH
fi

if [ ! -d "$PBUILDER_PATH/patches" -o ! -d "$PBUILDER_PATH/scripts" ]; then
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
