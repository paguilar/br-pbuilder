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


# Not used. The binary is executed by the Makefile
def pbuilder_exec(pbuilder_bin, pbuilder_deps_file, log_level):
    if pbuilder_bin is None or pbuilder_deps_file is None:
        logging.debug("NULL parameters received!")
        return 1

    try:
        if log_level is None:
            subprocess.check_output([pbuilder_bin, "-f", pbuilder_deps_file])
        else:
            #logging.debug("Executing %s -f %s -l %d", pbuilder_bin, pbuilder_deps_file, log_level)
            #subprocess.check_output([pbuilder_bin, '-f', pbuilder_deps_file, '-l', str(log_level)])
            cmd = pbuilder_bin + " -f " + pbuilder_deps_file + " -l " + str(log_level)
            logging.debug("Executing: %s", cmd)

            p = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            while True:
                output = p.stdout.readline()
                if output == '' and process.poll() is not None:
                    break;
                if output:
                    print(output.strip().decode())
            rc = process.poll()
            return rc

    except subprocess.CalledProcessError as err:
        logging.debug("Return code: %d", err.returncode)
        logging.debug("Output: %s", err.output)
        return err.returncode

    return 0


def pbuilder_compile(pbuilder_path):
    logging.debug("Building in %s", pbuilder_path)
    print("current dir: ", os.getcwd())

    os.chdir(pbuilder_path)
    try:
        logging.debug("Executing ./autogen.sh...")
        subprocess.check_output(["./autogen.sh"])

        logging.debug("Executing ./configure...")
        subprocess.check_output(["./configure"])

        logging.debug("Building pbuilder...")
        subprocess.check_output(["make"])
    except subprocess.CalledProcessError as err:
        logging.debug("Return code: %d", err.returncode)
        logging.debug("Output: %s", err.output)
        return err.returncode

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

    #with open(".pbuilder.deps", 'w') as f:
    with open(pbuilder_deps_file, 'w') as f:
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
    parser.add_argument("br2_build_dir", help="BUILD_DIR: The path where packages are extracted and built", type=str)
    parser.add_argument("pbuilder_path", help="Parallel graph builder sources path", type=str)
    args = parser.parse_args()
    log_level = args.loglevel
    br2_config_file = args.br2_config
    br2_build_dir = args.br2_build_dir
    pbuilder_path = args.pbuilder_path
    pbuilder_bin = pbuilder_path + "/src/pbuilder"
    pbuilder_deps_file = ".pbuilder.deps"

    set_log_level(log_level)

    if (log_level >= 1):
        print("BR2_CONFIG: ", br2_config_file)
        print("BUILD_DIR: ", br2_build_dir)
        print("pbuilder binary: ", pbuilder_bin)

    if os.path.isfile(br2_config_file) is False:
        logging.error("Failed to find BR config file: %s", br2_config_file)
        sys.exit(1)

    if os.path.isfile(pbuilder_bin) is False or os.access(pbuilder_bin, os.X_OK) is False:
        curr_path = os.getcwd()
        if pbuilder_compile(pbuilder_path) != 0:
            logging.error("Failed to build pbuilder. Exiting!")
            sys.exit(1)
        os.chdir(curr_path)

    if os.path.isfile(pbuilder_deps_file) is False:
        logging.debug("Dependencies file not found...")
        if generate_deps(pbuilder_deps_file) != 0:
            logging.error("Failed to calculate dependencies. Exiting!")
            sys.exit(1)
    else:
        logging.debug("'.config' last modified on       %s", time.ctime(os.path.getmtime(br2_config_file)))
        logging.debug("'.pbuilder.deps last modified on %s", time.ctime(os.path.getmtime(pbuilder_deps_file)))

        if os.path.getmtime(pbuilder_deps_file) <= os.path.getmtime(br2_config_file):
            logging.debug("Dependencies file is too old. Generating it again...")

        logging.debug("No need to update the dependencies file")


if __name__ == "__main__":
    sys.exit(main())
