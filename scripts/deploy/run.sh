#!/bin/bash

basedir="$(dirname $(realpath "${BASH_SOURCE[0]}"))"
export node_group=nodes
export node_setup_group=nodes_setup
export node_file="$basedir/nodes.ini"
export group_vars="$basedir/group_vars"
export build_tasks="$basedir/app/build.yml"
export reset_tasks="$basedir/app/reset.yml"
export run_tasks="$basedir/app/run.yml"
export setup_tasks="$basedir/app/_setup.yml"
export app_module="$basedir/app/"
export run_path="$(realpath "$PWD")"

minirun/run.sh "$@"
