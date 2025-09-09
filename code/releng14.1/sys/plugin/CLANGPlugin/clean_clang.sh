#!/bin/sh
# clean_clang.sh

DIR=$(pwd)

cd $DIR

cd $DIR/build
make clean

cd $DIR
rm -f metadataTestProgram