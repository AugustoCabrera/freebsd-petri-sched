#!/bin/sh
# rn_logger.sh - Helper para capturar CSV de resource_net en FreeBSD
# Uso:
#   sudo ./rn_logger.sh run          # captura completa interactiva
#   sudo ./rn_logger.sh setup        # configura filtros
#   sudo ./rn_logger.sh clear        # trunca el log
#   sudo ./rn_logger.sh start        # enciende captura
#   sudo ./rn_logger.sh stop         # apaga captura
#   sudo ./rn_logger.sh toggle       # alterna captura ON/OFF
#   sudo ./rn_logger.sh view         # muestra últimas 20 líneas
#   sudo ./rn_logger.sh status       # muestra estado actual
#   sudo ./rn_logger.sh watch        # monitorea drops en vivo (requiere SEQ)
#   sudo ./rn_logger.sh report       # reporte offline de drops por CPU
#   sudo ./rn_logger.sh report-span  # reporte detallado (offline)
#   sudo ./rn_logger.sh reset-seq    # intenta resetear contadores SEQ
#
# Nota: asegurar que syslog-ng escriba SOLO CSV de resource_net en:
#       /var/log/resource_net.log
#
# Este script NO modifica syslog-ng, solo opera sysctl y el archivo destino.

set -eu

LOGFILE="/var/log/resource_net.log"

need_root() {
  if [ "$(id -u)" -ne 0 ]; then
    echo "Este script requiere root. Usá: sudo $0 ..." >&2
    exit 1
  fi
}

sysctl_set() {
  sysctl "$1=$2" >/dev/null
}

setup_filters() {
  sysctl_set kern.sched.rn.cpumask 0xFFFFFFFF
  sysctl_set kern.sched.rn.pid -1
  sysctl kern.sched.rn.session=0 >/dev/null 2>&1 || true
  echo "[INFO] Filtros configurados: cpumask=0xFFFFFFFF pid=-1 session=0"
}

clear_window() {
  : > "$LOGFILE"
  echo "[INFO] Ventana limpia: $LOGFILE truncado."
}

start_capture() {
  sysctl_set kern.sched.rn.capture 1
  echo "[OK] Captura activada: logeando estados en $LOGFILE"
}

stop_capture() {
  sysctl_set kern.sched.rn.capture 0
  echo "[OK] Captura desactivada."
}

toggle_capture() {
  CUR=$(sysctl -n kern.sched.rn.capture 2>/dev/null || echo 0)
  if [ "$CUR" = "0" ]; then
    start_capture
  else
    stop_capture
  fi
}

view_tail() {
  if [ -f "$LOGFILE" ]; then
    echo "--- Últimas 20 líneas de $LOGFILE ---"
    tail -n 20 "$LOGFILE"
  else
    echo "[ERR] No existe $LOGFILE. syslog-ng esta configurado para escribir ahi?" >&2
    exit 1
  fi
}

status() {
  CAP=$(sysctl -n kern.sched.rn.capture 2>/dev/null || echo 0)
  CPU=$(sysctl -n kern.sched.rn.cpumask 2>/dev/null || echo "?")
  PIDF=$(sysctl -n kern.sched.rn.pid 2>/dev/null || echo "?")
  SES=$(sysctl -n kern.sched.rn.session 2>/dev/null || echo "n/a")
  echo "Estado actual:"
  echo "  capture = $CAP (1=ON, 0=OFF)"
  echo "  cpumask = $CPU"
  echo "  pid     = $PIDF"
  echo "  session = $SES"
  if [ -f "$LOGFILE" ]; then
    echo "  logfile = $LOGFILE ($(wc -l < "$LOGFILE" 2>/dev/null || echo 0) líneas)"
  else
    echo "  logfile = $LOGFILE (no existe)"
  fi
}

# Flujo de pruebas
run_flow() {
  setup_filters
  clear_window
  start_capture
  echo "[*] Ejecuta tu workload/benchmark ahora."
  echo "    Cuando quieras detener la captura y ver el log, presioná ENTER..."
  # shellcheck disable=SC2034
  read _ || true
  stop_capture
  view_tail
  echo "----------------------------------------"
  report_drops
  echo "----------------------------------------"
  report_span
}

# Monitoreo en vivo
watch_drops() {
  if [ ! -f "$LOGFILE" ]; then
    echo "[ERR] No existe $LOGFILE. Corré captura primero." >&2
    exit 1
  fi
  echo "[INFO] Dropwatch en vivo (Ctrl-C para salir). Archivo: $LOGFILE"
  echo "Tip: en otra terminal, corré tu benchmark."

  set +e
  LC_ALL=C tail -F "$LOGFILE" 2>/dev/null | awk -F': ' '
  BEGIN {
    OFS=","; 
    printf("CPU LOST WRAPS\n") > "/dev/stderr";
  }
  {
    csv=$NF;
    n=split(csv,a,",");
    if(n<6 || n>7) next;

    c=a[1]+0; s=a[6]+0;
    # Heurística por si SEQ cae en el campo 7
    if (s !~ /^[0-9]+$/ && n>=7 && a[7] ~ /^[0-9]+$/) s=a[7]+0;

    if (c in last) {
      if (s>last[c]+1) { lost[c]+=s-last[c]-1; changed=1 }
      else if (s<last[c]) { wraps[c]++; changed=1 }
    }
    last[c]=s;

    if (NR%500==0 || changed) {
      line="\r";
      for (c in last) { line=sprintf("%sCPU%u lost=%u wraps=%u  ", line, c, lost[c]+0, wraps[c]+0) }
      printf("%s", line) > "/dev/stderr"; 
      fflush("/dev/stderr");
      changed=0
    }
  }
  END {
    printf("\n\nResumen final (vivo):\n") > "/dev/stderr";
    printf("CPU,LOST,WRAPS\n") > "/dev/stderr";
    for (c in last) printf("%u,%u,%u\n", c, lost[c]+0, wraps[c]+0) > "/dev/stderr";
  }
  '
  rc=$?
  set -e
  exit $rc
}

# Reportes offline
report_drops() {
  if [ ! -f "$LOGFILE" ]; then
    echo "[ERR] No existe $LOGFILE. Corre captura primero." >&2
    exit 1
  fi
  echo "[INFO] Reporte de drops offline sobre $LOGFILE"
  
  LC_ALL=C awk -F': ' '
  BEGIN { OFS=","; }
  {
    csv=$NF;
    n=split(csv,a,",");
    if(n<6 || n>7) { noseq=1; next; }

    c=a[1]+0; s=a[6]+0;
    if (!(s ~ /^[0-9]+$/) && n>=7 && a[7] ~ /^[0-9]+$/) s=a[7]+0;

    if (c in last) {
      if (s>last[c]+1) lost[c]+=s-last[c]-1;
      else if (s<last[c]) wraps[c]++;
    }
    last[c]=s
  }
  END {
    if (noseq) {
      print "WARN: No se detecto colum SEQ en alguna linea.Compilaste con RNLOG_WITH_SEQ=1?" > "/dev/stderr";
    }
    print "CPU,LOST,WRAPS";
    total_lost=0; total_wraps=0;
    for (c in last) {
      print c, lost[c]+0, wraps[c]+0;
      total_lost+=lost[c]+0; total_wraps+=wraps[c]+0;
    }
    print "TOTAL", total_lost, total_wraps;
  }
  ' "$LOGFILE"
}

report_span() {
  if [ ! -f "$LOGFILE" ]; then
    echo "[ERR] No existe $LOGFILE. Corré captura primero." >&2
    exit 1
  fi
  echo "[INFO] Resumen SPAN/SEEN/LOST por CPU"
  
  awk -F': ' '
  {
    n=split($NF,a,","); 
    if(n<6 || n>7) next;
    
    c=a[1]+0; s=a[6]+0;
    if (!(s ~ /^[0-9]+$/) && n>=7 && a[7] ~ /^[0-9]+$/) s=a[7]+0;

    if(!(c in first)) first[c]=s;
    last[c]=s; seen[c]++;
    
    if(c in prev){
      if(s>prev[c]+1) gap[c]+=s-prev[c]-1;
      else if(s<prev[c]) wrap[c]++;
    }
    prev[c]=s
  }
  END{
    printf("CPU,FIRST,LAST,SPAN,SEEN,LOST,WRAPS\n");
    for(c in last){
      span=last[c]-first[c]+1;
      printf("%d,%d,%d,%d,%d,%d,%d\n", c, first[c], last[c], span, (seen[c]+0), (gap[c]+0), (wrap[c]+0));
    }
  }' "$LOGFILE" | sort -t, -k1,1n
}

reset_seq() {
  if sysctl -aN 2>/dev/null | grep -qx 'kern.sched.rn.reset_seq'; then
    sysctl kern.sched.rn.reset_seq=1 >/dev/null
    echo "[OK] SEQ per-CPU reseteado (kern.sched.rn.reset_seq=1)."
  else
    echo "[INFO] No existe kern.sched.rn.reset_seq en este kernel."
  fi
}

main() {
  need_root
  CMD="${1:-run}"
  case "$CMD" in
    run)         run_flow ;;
    setup)       setup_filters ;;
    clear)       clear_window ;;
    start)       start_capture ;;
    stop)        stop_capture ;;
    toggle)      toggle_capture ;;
    view)        view_tail ;;
    status)      status ;;
    watch)       watch_drops ;;
    report)      report_drops ;;
    report-span) report_span ;;
    reset-seq)   reset_seq ;;
    *)
      echo "Uso: $0 {run|setup|clear|start|stop|toggle|view|status|watch|report|report-span|reset-seq}" >&2
      exit 2
      ;;
  esac
}

main "$@"
