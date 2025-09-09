#!/bin/sh

# Script to compile the GCC plugin

fbsd_num=$(freebsd-version | cut -d "-" -f1)
fbsd_version="freebsd$fbsd_num"

gcc_version=$(gcc --version | awk 'NR==1 {print $NF}')
gcc_mayor=$(echo "$gcc_version" | cut -d "." -f1)

dir_gcc_plugin_header="/usr/local/lib/gcc$gcc_mayor/gcc/x86_64-portbld-$fbsd_version/$gcc_version/plugin/include"

make_cmd="make DIR_GCC_PLUGIN_HEADER='$dir_gcc_plugin_header'";

echo "$make_cmd"
eval "$make_cmd"