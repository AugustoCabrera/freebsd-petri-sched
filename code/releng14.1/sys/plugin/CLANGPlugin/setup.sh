#!/bin/sh

echo ""
echo "[INFO] Compilando plugin de CLang para insertar metadata en ELF..."
echo ""

DIR=$(pwd)

# Forzamos a usar el ClangConfig.cmake de LLVM14 correcto
CLANG_TUTOR_DIR="$DIR"
Clang_DIR="/usr/local/llvm14/lib/cmake/clang"

export CLANG_TUTOR_DIR
export Clang_DIR

# Borramos compilaciones anteriores
rm -rf "$DIR/build"
mkdir "$DIR/build"
cd "$DIR/build" || exit

# Configuramos cmake usando clang14 y clang++14
cmake -DCMAKE_C_COMPILER=clang14 -DCMAKE_CXX_COMPILER=clang++14 \
      -DCT_Clang_INSTALL_DIR="$Clang_DIR" "$CLANG_TUTOR_DIR"

if [ $? -ne 0 ]; then
    echo "[ERROR] Fallo la configuración con cmake."
    exit 1
fi

# Compilamos
make

if [ $? -ne 0 ]; then
    echo "[ERROR] Fallo la compilación con make."
    exit 1
fi

echo ""
echo "[SUCCESS] Plugin de CLang compilado correctamente. 🔥"
echo ""
