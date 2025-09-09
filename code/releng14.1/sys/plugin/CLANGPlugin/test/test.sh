#!/bin/sh

# The Clang driver accepts the -fplugin option to load a plugin. Clang plugins can receive arguments 
# from the compiler driver command line via the fplugin-arg-<plugin name>-<argument> option. 
# Using this method, the plugin name cannot contain dashes itself, but the argument passed to the plugin can.

# $1 = user program source code name 
# $2 = output file name
# rest of arguments = plugin arguments containing data to insert in user program's executable

# EXAMPLE
# $ sh test.sh test.c output_file decl_payA.txt_binary.bin decl_payB.txt_binary.bin

arguments="";
args_count=$#;

compiler="clang14" #clang14 is default version in freebsd 13.2
plugin_so_name="libInsertPayload_Plugin.so"
plugin_so_dir="./../build"
plugin_name="InsertPayload_Plugin"
flag_clang_usage="-Xclang"
flag_arg_clang="-plugin-arg-"
compiler_flags="-Wno-implicit-int" # Flags needed to compile the plugin and the modified user source code
tmp_dir="/tmp/"
modified_src_code_prefix="metadata_"

include_sys_dir="/usr/src/sys"

if [ -z "$1" ]; then
    echo "ERROR: User program source code has not been defined as the first argument."
    exit 1
fi

src_code="$1"
src_code_filename=$(basename "$src_code")
output_program_name="$2"

echo "[INFO] Insertando metadata al archivo $src_code."
echo ""

echo "User program source code: $1"

if [ -z "$3" ]; then
    echo "ERROR: Missing output name or file to insert as metadata"
    exit 1
fi

i=3;
while [ "$i" -le "$args_count" ]
do
    arguments="$arguments $flag_clang_usage $flag_arg_clang$plugin_name $flag_clang_usage $3";
    echo "Plugin argument - $i: $3";
    i=$((i + 1));
    shift 1;
done

echo "Count of arguments: $args_count"
echo ""

## COMPILE AND PLUGIN USAGE COMMAND
clang_command="$clang_command $compiler $flag_clang_usage -load $flag_clang_usage $plugin_so_dir/$plugin_so_name "
clang_command="$clang_command $flag_clang_usage -plugin $flag_clang_usage $plugin_name "
clang_command="$clang_command $arguments "
clang_command="$clang_command -c $src_code -I$include_sys_dir $compiler_flags"

echo "$clang_command"
eval "$clang_command"

if [ $? != 0 ]; then
    exit 1
fi

echo ""

echo "[INFO] Compilando $src_code con la metadata insertada."
compile_command="$compiler -o $output_program_name $tmp_dir$modified_src_code_prefix$src_code_filename -I$include_sys_dir"
echo ""

eval "$compile_command"
echo ""

## REMOVE THE TMP FILE
remove_tmp_src_command="rm $tmp_dir$modified_src_code_prefix$src_code_filename"

eval "$remove_tmp_src_command"

echo "[SUCCESS] metadata insertada en $src_code."
echo ""