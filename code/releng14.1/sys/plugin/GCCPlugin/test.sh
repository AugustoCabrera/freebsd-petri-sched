#!/bin/sh

# Script for parsing and processing the arguments for Makefile of GCC plugin  

# $1 = user program source code name 
# rest of arguments = plugin arguments containing data to insert in user program's executable

# EXAMPLE
# $ sh test.sh prueba.c decl_payA.txt_binary.bin decl_payB.txt_binary.bin

arguments="";
args_count=$#;

plugin_name="InsertPayload_GCCPlugin";
flag_gcc_plugin_arg="fplugin-arg";

if [ -z "$1" ]; then
    echo "ERROR: User program source code has not been defined as the first argument."
    exit 1
fi

i=2;
while [ "$i" -le "$args_count" ]
do
    if grep -q "Sched Info:" "$2"; then #sched info payload
        flag_numfunction="2"
    else #bin payload
        flag_numfunction="1"
    fi

    arguments="$arguments-$flag_gcc_plugin_arg-$plugin_name-$flag_numfunction=$2 ";
    echo "Plugin argument - $i: $2";
    i=$((i + 1));
    shift 1
done

echo "Count of arguments: $args_count"
# echo "$arguments"
make_test_cmd="make test PLUGIN_ARGS='$arguments'";

echo "$make_test_cmd"
eval "$make_test_cmd"