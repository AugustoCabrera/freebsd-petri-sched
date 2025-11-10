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
#   sudo sh rn_logger.sh watch      # monitorea drops en vivo (requiere SEQ)
#   sudo sh rn_logger.sh report     # reporte offline de drops por CPU (requiere SEQ)
#   sudo sh rn_logger.sh reset-seq  # intenta resetear contadores SEQ (si el sysctl existe)
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

# ---------- Dropwatch (en vivo) y reporte (offline) ----------

# Detección de columna SEQ:
# Con RNLOG_WITH_SEQ=1: SEQ es el campo 6.
# Con ambos (SEQ+SESSION): SEQ sigue siendo el 6.
# Si solo SESSION (sin SEQ), no hay forma de medir drops: detectamos y avisamos.
_seq_field_awk='
function get_seq_idx(nf, f6, f7) {
  # Heurística: si hay >=6 campos y el campo 6 es numérico, asumimos SEQ=$6.
  # Si hay >=7 y $6 no es numérico pero $7 sí, usamos $7 (fallback muy raro).
  if (nf >= 6 && f6 ~ /^[0-9]+$/) return 6;
  if (nf >= 7 && f7 ~ /^[0-9]+$/) return 7;
  return -1;
}
'

watch_drops() {
  if [ ! -f "$LOGFILE" ]; then
    echo "No existe $LOGFILE. Corré captura primero." >&2
    exit 1
  fi
  echo "👀 Dropwatch en vivo (Ctrl-C para salir). Archivo: $LOGFILE"
  echo "Tip: en otra terminal, corré tu benchmark."

  # Evitar que 'set -e' corte el pipeline al salir
  set +e
  # Forzar C locale para awk numérico simple
  LC_ALL=C \
  tail -F "$LOGFILE" 2>/dev/null | awk -F, "
$_seq_field_awk
BEGIN {
  OFS=\",\"; printf(\"CPU LOST WRAPS\\n\") > \"/dev/stderr\";
}
{
  nf=NF; f6=\$6; f7=\$7;
  seqidx=get_seq_idx(nf, f6, f7);
  if (seqidx<0) next;

  cpu=\$1+0;
  seq=\$seqidx+0;

  if (cpu in last) {
    if (seq>last[cpu]+1) { lost[cpu]+=seq-last[cpu]-1; changed=1 }
    else if (seq<last[cpu]) { wraps[cpu]++; changed=1 }
  }
  last[cpu]=seq;

  # refresco periòdico
  if (NR%500==0 || changed) {
    # imprimimos un resumen compacto en stderr para no romper pipes
    line=\"\r\"; for (c in last) { line=sprintf(\"%sCPU%u lost=%u wraps=%u  \", line, c, lost[c]+0, wraps[c]+0) }
    printf(\"%s\", line) > \"/dev/stderr\"; fflush(\"/dev/stderr\");
    changed=0
  }
}
END {
  printf(\"\\n\\nResumen final (vivo):\\n\") > \"/dev/stderr\";
  printf(\"CPU,LOST,WRAPS\\n\") > \"/dev/stderr\";
  for (c in last) printf(\"%u,%u,%u\\n\", c, lost[c]+0, wraps[c]+0) > \"/dev/stderr\";
}
"
  rc=$?
  set -e
  exit $rc
}

report_drops() {
  if [ ! -f "$LOGFILE" ]; then
    echo "No existe $LOGFILE. Corré captura primero." >&2
    exit 1
  fi
  echo "📄 Reporte de drops (offline) sobre $LOGFILE"
  LC_ALL=C awk -F, "
$_seq_field_awk
BEGIN { OFS=\",\"; }
{
  nf=NF; f6=\$6; f7=\$7;
  seqidx=get_seq_idx(nf, f6, f7);
  if (seqidx<0) { noseq=1; next; }

  cpu=\$1+0;
  seq=\$seqidx+0;

  if (cpu in last) {
    if (seq>last[cpu]+1) lost[cpu]+=seq-last[cpu]-1;
    else if (seq<last[cpu]) wraps[cpu]++;
  }
  last[cpu]=seq
}
END {
  if (noseq) {
    print \"WARN: No se detectó columna SEQ en alguna línea. ¿Compilaste con RNLOG_WITH_SEQ=1?\" > \"/dev/stderr\";
  }
  print \"CPU,LOST,WRAPS\";
  total_lost=0; total_wraps=0;
  for (c in last) { print c, lost[c]+0, wraps[c]+0; total_lost+=lost[c]+0; total_wraps+=wraps[c]+0; }
  print \"TOTAL\", total_lost, total_wraps;
}
" "$LOGFILE"
}

reset_seq() {
  # Solo funciona si existe el sysctl que propusimos (reset de SEQ per-CPU)
  if sysctl -aN 2>/dev/null | grep -qx 'kern.sched.rn.reset_seq'; then
    sysctl kern.sched.rn.reset_seq=1 >/dev/null
    echo "🔁 SEQ per-CPU reseteado (kern.sched.rn.reset_seq=1)."
  else
    echo "No existe kern.sched.rn.reset_seq en este kernel (opcional)."
  fi
}

main() {
  need_root
  CMD="${1:-run}"
  case "$CMD" in
    run)        run_flow ;;
    setup)      setup_filters ;;
    clear)      clear_window ;;
    start)      start_capture ;;
    stop)       stop_capture ;;
    toggle)     toggle_capture ;;
    view)       view_tail ;;
    status)     status ;;
    watch)      watch_drops ;;
    report)     report_drops ;;
    reset-seq)  reset_seq ;;
    *) echo "Uso: $0 {run|setup|clear|start|stop|toggle|view|status|watch|report|reset-seq}" >&2; exit 2 ;;
  esac
}

main "$@"
