#!/bin/sh
# rn_logger.sh — Helper para capturar CSV de resource_net en FreeBSD
# Uso:
#   sudo sh rn_logger.sh run        # hace pasos 1-3, espera Enter, hace 4-5
#   sudo sh rn_logger.sh setup      # solo configura filtros (paso 1)
#   sudo sh rn_logger.sh clear      # trunca ventana (paso 2)
#   sudo sh rn_logger.sh start      # enciende captura (paso 3)
#   sudo sh rn_logger.sh stop       # apaga captura (paso 4)
#   sudo sh rn_logger.sh toggle     # alterna captura ON/OFF (eco en español)
#   sudo sh rn_logger.sh view       # muestra últimas 20 líneas (paso 5)
#   sudo sh rn_logger.sh status     # muestra estado actual
#
# Nota 0) Asegúrate de tener syslog-ng escribiendo SOLO CSV de resource_net en:
#         /var/log/resource_net.log   (ver guía de configuración)
#
# Este script NO modifica syslog-ng, solo opera los sysctl y el archivo destino.

set -eu

LOGFILE="/var/log/resource_net.log"

need_root() {
  if [ "$(id -u)" -ne 0 ]; then
    echo "Este script requiere root. Usá: sudo $0 ..." >&2
    exit 1
  fi
}

sysctl_set() {
  # $1 clave, $2 valor
  sysctl "$1=$2" >/dev/null
}

setup_filters() {
  # Paso 1) Configurar filtros razonables por defecto
  sysctl_set kern.sched.rn.cpumask 0xFFFFFFFF     # todos los cores (32 bits)
  sysctl_set kern.sched.rn.pid -1                 # cualquier PID
  # session es opcional; si no está soportado en build, ignorar error
  sysctl kern.sched.rn.session=0 >/dev/null 2>&1 || true
  echo "Filtros configurados: cpumask=0xFFFFFFFF pid=-1 session=0"
}

clear_window() {
  # Paso 2) Ventana limpia
  : > "$LOGFILE"
  echo "Ventana limpia: $LOGFILE truncado."
}

start_capture() {
  # Paso 3) Encender captura
  sysctl_set kern.sched.rn.capture 1
  echo "✅ Captura activada: logeando estados de la rdp de los recursos → $LOGFILE"
}

stop_capture() {
  # Paso 4) Apagar captura
  sysctl_set kern.sched.rn.capture 0
  echo "🛑 Captura desactivada."
}

toggle_capture() {
  # Alterna el estado actual y muestra mensaje en español
  CUR=$(sysctl -n kern.sched.rn.capture 2>/dev/null || echo 0)
  if [ "$CUR" = "0" ]; then
    start_capture
  else
    stop_capture
  fi
}

view_tail() {
  # Paso 5) Ver últimas 20 líneas
  if [ -f "$LOGFILE" ]; then
    echo "── Últimas 20 líneas de $LOGFILE ──"
    tail -n 20 "$LOGFILE"
  else
    echo "No existe $LOGFILE. ¿syslog-ng está configurado para escribir CSV ahí?" >&2
    exit 1
  fi
}

status() {
  CAP=$(sysctl -n kern.sched.rn.capture 2>/dev/null || echo 0)
  CPU=$(sysctl -n kern.sched.rn.cpumask 2>/dev/null || echo "?")
  PIDF=$(sysctl -n kern.sched.rn.pid 2>/dev/null || echo "?")
  SES=$(sysctl -n kern.sched.rn.session 2>/dev/null || echo "n/a")
  echo "Estado:"
  echo "  capture = $CAP (1=ON,0=OFF)"
  echo "  cpumask = $CPU"
  echo "  pid     = $PIDF"
  echo "  session = $SES"
  if [ -f "$LOGFILE" ]; then
    echo "  logfile = $LOGFILE ($(wc -l < "$LOGFILE" 2>/dev/null || echo 0) líneas)"
  else
    echo "  logfile = $LOGFILE (no existe)"
  fi
}

run_flow() {
  setup_filters
  clear_window
  start_capture
  echo "▶️  Ejecutá tu workload/benchmark ahora."
  echo "   Cuando quieras detener la captura y ver el log, presioná ENTER..."
  # shellcheck disable=SC2034
  read _ || true
  stop_capture
  view_tail
}

main() {
  need_root
  CMD="${1:-run}"
  case "$CMD" in
    run)     run_flow ;;
    setup)   setup_filters ;;
    clear)   clear_window ;;
    start)   start_capture ;;
    stop)    stop_capture ;;
    toggle)  toggle_capture ;;
    view)    view_tail ;;
    status)  status ;;
    *) echo "Uso: $0 {run|setup|clear|start|stop|toggle|view|status}" >&2; exit 2 ;;
  esac
}

main "$@"