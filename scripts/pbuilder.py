#!/usr/bin/env python3

import sys
import json
import logging
import os
import subprocess
import argparse
import time

def set_log_level(log_level):
    if log_level is None:
        return

    if log_level != 0:
        logging.basicConfig(level=logging.DEBUG, format='%(asctime)s - %(levelname)s - %(message)s') 
        print("Setting log level to debugging")


def pbuilder_exec(pbuilder_bin):
    logging.debug("Executing %s", pbuilder_bin)

    return 0


def compile_builder(pbuilder_path):
    logging.debug("Building in %s", pbuilder_path)

    os.chdir(pbuilder_path)
    try:
        logging.debug("Executing autogen.sh")
        subprocess.check_output(["./autogen.sh"])
        subprocess.check_output(["./configure"])
        subprocess.check_output(["make"])
        subprocess.check_output(["ls", "hola"])
    except subprocess.CalledProcessError as err:
        logging.debug("Return code: %d", err.returncode)
        logging.debug("Output: %s", err.output)
        return err.output

    return 0


def generate_deps(pbuilder_deps_file):
    logging.debug("Generating dependency tree...")

    deps = {}

    cmd = ["make", "-s", "--no-print-directory", "show-info"]
    #with open(os.devnull, 'wb') as devnull:
        #p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=devnull,
                             #universal_newlines=True)
        #pkg_list = json.loads(p.communicate()[0])
    pkg_list = json.loads(subprocess.check_output(cmd))

    with open(".pbuilder.deps", 'w') as f:
        for pkg in pkg_list:
            pkg_name = pkg_list[pkg].get("name")
            if not pkg_name:
                logging.debug("Skipping: %s", pkg);
                continue
            deps[pkg] = pkg_list[pkg].get("dependencies", [])
            print(pkg, ":", end=" ", file=f)
            for p in deps[pkg]:
                print(p, end=" ", file=f)
            print("", file=f)

    return 0


def main():
    # Get BR2_CONFIG and compare date with .pbuilder.deps
    parser = argparse.ArgumentParser("pbuilder.py")
    parser.add_argument("-l", "--loglevel", help="Enable/Disable log level. Default: 0 (disabled)", type=int)
    parser.add_argument("br2_config", help="BR2_CONFIG: The path to the Buildroot .config file", type=str)
    parser.add_argument("br2_topdir", help="TOPDIR: The path to Buildroot's top dir", type=str)
    parser.add_argument("pbuilder_path", help="Parallel graph builder sources path", type=str)
    args = parser.parse_args()
    log_level = args.loglevel
    br2_config_path = args.br2_config
    br2_config_file = br2_config_path
    br2_topdir = args.br2_topdir
    pbuilder_path = args.pbuilder_path
    pbuilder_bin = pbuilder_path + "/src/pbuilder"
    pbuilder_deps_file = br2_topdir + "/.pbuilder.deps"

    set_log_level(log_level)

    if os.path.isfile(br2_config_file) is False:
        logging.error("Failed to find BR config file: %s", br2_config_file)
        sys.exit(1)

    if os.path.isfile(pbuilder_bin) is False or os.access(pbuilder_bin, os.X_OK) is False:
        res = compile_builder(pbuilder_path)
        if res != 0:
            logging.error("Failed to build pbuilder. Exiting!")
            sys.exit(1)

    if os.path.isfile(pbuilder_deps_file) is False:
        logging.debug("Dependencies file not found...")
        res = generate_deps(pbuilder_deps_file)
        if res != 0:
            logging.error("Failed to calculate dependencies. Exiting!")
            sys.exit(1)
    else:
        logging.debug("'.config' last modified on       %s", time.ctime(os.path.getmtime(br2_config_file)))
        logging.debug("'.pbuilder.deps last modified on %s", time.ctime(os.path.getmtime(pbuilder_deps_file)))

        if os.path.getmtime(pbuilder_deps_file) <= os.path.getmtime(br2_config_file):
            logging.debug("Dependencies file is too old. Generating it again...")

        logging.debug("No need to update the dependencies file")

    res = pbuilder_exec(pbuilder_bin)
    if res != 0:
        logging.error("Failed to execute pbuilder. Exiting!")
        sys.exit(1)


if __name__ == "__main__":
    sys.exit(main())
