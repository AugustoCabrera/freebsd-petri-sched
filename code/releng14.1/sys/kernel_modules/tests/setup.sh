#!/usr/local/bin/bash

compile_metadata_programs () {
    
    if ! test -d "../../kernel_modules/tests/"; then
        echo -e "[ERROR] no existe '../../kernel_modules/tests/'.\n"
        exit 1
    fi
    sh "test.sh" "$1" "$2" "schedData/$2"
    
    mv "$2" "../../kernel_modules/tests/"
}

echo -e "[INFO] Se van a compilar los modulos de kernel del proyecto.\n"

cd ../ || exit 

echo -e "[INFO] Limpiando el entorno.\n"
make clean

echo -e "\n[INFO] Compilando los modulos.\n"
make

echo -e "\n[SUCCESS] Modulos correctamente compilados.\n"

echo -e "[INFO] Compilando plugin para insertar metadata.\n"

if test -d "../CLANGPlugin"; then
    cd "../CLANGPlugin" || exit
    sh clean_clang.sh
    sh setup.sh

    cd test/ || exit

    echo "[INFO] Se compilaran diversos programas con metadata insertada:"
    echo "       - no realizan trabajo, solo tienen info para scheduler:"
    echo "           - lowperf requiere poco cpu y monopolizar"
    echo "           - std requiere poco cpu y monopolizar"
    echo "           - hiperf requiere poco cpu y monopolizar"
    echo "           - critical requiere poco cpu y monopolizar"
    echo -e "       - primes: solicita un cpu e informa alto uso de cpu\n"

    compile_metadata_programs test.c lowperf
    compile_metadata_programs test.c std
    compile_metadata_programs test.c hiperf
    compile_metadata_programs test.c critical
    compile_metadata_programs primes.c primes
fi