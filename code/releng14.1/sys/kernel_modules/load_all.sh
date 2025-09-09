#!/usr/local/bin/bash

BASE_DIR="/usr/src/sys/kernel_modules"
OBJ_DIR="/usr/obj/usr/src/amd64.amd64/sys/kernel_modules"

echo -e "[INFO] Descargando módulos existentes antes de cargar nuevos.\n"

# Descargar módulos en orden inverso
echo -e "[INFO] Descargando módulo pin_threads.\n"
kldunload "$OBJ_DIR/pin_threads/pin_threads.ko"

echo -e "[INFO] Descargando módulo toggle_active_cpu.\n"
kldunload "$OBJ_DIR/toggle_active_cpu/toggle_active_cpu.ko"

echo -e "[INFO] Descargando módulo thread_stats.\n"
kldunload "$OBJ_DIR/thread_stats/thread_stats.ko"

echo -e "[INFO] Descargando módulo cpu_stats.\n"
kldunload "$OBJ_DIR/cpu_stats/cpu_stats.ko"

echo -e "[INFO] Compilando y cargando módulo cpu_stats.\n"
cd "$BASE_DIR/cpu_stats" && make && kldload "$OBJ_DIR/cpu_stats/cpu_stats.ko" && cd "$BASE_DIR"

echo -e "[INFO] Compilando y cargando módulo thread_stats.\n"
cd "$BASE_DIR/thread_stats" && make && kldload "$OBJ_DIR/thread_stats/thread_stats.ko" && cd "$BASE_DIR"

echo -e "[INFO] Compilando y cargando módulo toggle_active_cpu.\n"
cd "$BASE_DIR/toggle_active_cpu" && make && kldload "$OBJ_DIR/toggle_active_cpu/toggle_active_cpu.ko" && cd "$BASE_DIR"

echo -e "[INFO] Compilando y cargando módulo pin_threads.\n"
cd "$BASE_DIR/pin_threads" && make && kldload "$OBJ_DIR/pin_threads/pin_threads.ko" && cd "$BASE_DIR"


kldload /boot/modules/cpu_stats.ko
kldload /boot/modules/thread_stats.ko
kldload /usr/obj/usr/src/amd64.amd64/sys/kernel_modules/pin_threads/pin_threads.ko
kldload /boot/modules/toggle_active_cpu.ko


echo -e "\n[SUCCESS] ¡Todos los módulos cargados correctamente! 🔥\n"

# Mostrar las entradas que ahora están debajo de kern.sched_stats
echo -e "[INFO] Estado actual de kern.sched_stats:\n"
sysctl kern.sched_stats
