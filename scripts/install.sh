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

# Get Buildroot version from the variable BR2_VERSION
# in the main Makefile of the given path
get_br2_ver()
{
    br2_ver_str=$(cat $BR_PATH/Makefile | grep -e "^export BR2_VERSION :=" | awk '{print $4}')
    major_ver=$(echo $br2_ver_str | awk -F'.' '{print $1}')
    git_ver=$(echo $br2_ver_str | grep "git")

    # Get the version also when Buildroot is downloaded from a git repo
    if [ -z $git_ver ]; then
        minor_ver=$(echo $br2_ver_str | awk -F'.' '{print $2}')
    else
        minor_ver=$(echo $br2_ver_str | awk -F'.' '{print $2}' | awk -F'-' '{print $1}')
    fi

    br2_ver=$major_ver"."$minor_ver
}

install()
{
    get_br2_ver
    echo "Detected Buildroot version (yyyy.mm): $br2_ver"
    if [ ! -d $PBUILDER_PATH/patches/$br2_ver ]; then
        echo "Failed to found patches for Buildroot version '$br2_ver'. Trying latest patch set..."
        br2_ver="latest"
    fi

    echo "Creating symlinks..."
    cd $BR_PATH/utils
    ln -s $PBUILDER_PATH pbuilder
    cd - 1>/dev/null

    cd $BR_PATH/support/scripts
    ln -s $PBUILDER_PATH/scripts/pbuilder.py .
    cd - 1>/dev/null

    cd $BR_PATH
    echo "Applying patches..."
    for i in `ls $PBUILDER_PATH/patches/$br2_ver/*.patch`; do
        #echo "patch -p1 < $i"
        patch -p1 < $i
    done
    cd - 1>/dev/null
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
