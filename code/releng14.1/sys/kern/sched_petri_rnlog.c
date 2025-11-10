/*
 * sched_petri_rnlog.c — CSV logging + matrix dumps + sysctl dump
 * for resource_net (FreeBSD kernel)
 */

#include <sys/sched_petri_rnlog.h>

#ifdef _KERNEL

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/proc.h>
#include <sys/pcpu.h>
#include <sys/syslog.h>          /* LOG_INFO */
#include <sys/sbuf.h>            /* sbuf_*   */
#include <sys/sched_petri.h>     /* PLACE(), TRANSITION(), CPU_BASE_* */

/* ===================== Estado de captura y filtros ===================== */
static int      rn_capture = 0;            /* 0=off, 1=on (acceso atómico) */
static unsigned rn_cpumask = 0xFFFFFFFFu;  /* bit N habilita CPU N (32 bits) */
static int      rn_pid_filter = -1;        /* -1 = cualquiera, otro = PID exacto */
static unsigned rn_session = 0;            /* optional session id (acceso atómico) */

#if RNLOG_WITH_SEQ
static unsigned rn_seq_cpu[MAXCPU];        /* contador por-CPU (acceso atómico) */
#endif

/* Declarar el nodo existente kern.sched, NO volver a crearlo */
SYSCTL_DECL(_kern_sched);

/* sysctl: kern.sched.rn.* */
SYSCTL_NODE(_kern_sched, OID_AUTO, rn, CTLFLAG_RW | CTLFLAG_MPSAFE, 0,
    "resource_net CSV logging control");

SYSCTL_INT(_kern_sched_rn, OID_AUTO, capture, CTLFLAG_RWTUN,
    (int *)&rn_capture, 0, "0=off, 1=on");
SYSCTL_UINT(_kern_sched_rn, OID_AUTO, cpumask, CTLFLAG_RWTUN,
    &rn_cpumask, 0, "CPU bitmask (bit N enables CPU N)");
SYSCTL_INT(_kern_sched_rn, OID_AUTO, pid, CTLFLAG_RWTUN,
    &rn_pid_filter, 0, "PID filter (-1 any PID, else exact PID)");
SYSCTL_UINT(_kern_sched_rn, OID_AUTO, session, CTLFLAG_RWTUN,
    &rn_session, 0, "Optional session id (not printed unless compiled)");

/* ===================== API de control ===================== */

void
rnlog_enable(int on)
{
    __atomic_store_n(&rn_capture, on ? 1 : 0, __ATOMIC_RELAXED);
}

void
rnlog_set_cpumask(unsigned mask)
{
    rn_cpumask = mask;
}

void
rnlog_set_pid(int pid)
{
    rn_pid_filter = pid;
}

void
rnlog_set_session(unsigned sess)
{
    __atomic_store_n(&rn_session, sess, __ATOMIC_RELAXED);
}

/* Filtro rápido para saber si loguear este evento para el thread dado */
int
rnlog_should_log(struct thread *td)
{
    if (__atomic_load_n(&rn_capture, __ATOMIC_RELAXED) == 0)
        return 0;

    unsigned cpu = (unsigned)PCPU_GET(cpuid);
    if (cpu >= MAXCPU)
        return 0; /* paranoia: id de CPU inválido */

    if (((rn_cpumask >> cpu) & 1u) == 0)
        return 0;

    if (rn_pid_filter >= 0) {
        int pid = (td && td->td_proc) ? td->td_proc->p_pid : -1;
        if (pid != rn_pid_filter)
            return 0;
    }
    return 1;
}

/*
 * Emite una línea CSV. Formato base:
 *   CPU,TID,PROC,FROM,TRANS_ID[,SEQ][,SESSION]
 */
void
rn_log_transition(struct thread *td, int tindex, const char *func, const char *note)
{
    (void)note; /* reservado para uso futuro */
    if (!td || !rnlog_should_log(td))
        return;

    unsigned cpu = (unsigned)PCPU_GET(cpuid);
    if (cpu >= MAXCPU)
        return;

    unsigned tid = (unsigned)td->td_tid;

    /* p_comm es un array (char[]), no puntero: chequear primer char */
    const char *pname = (td->td_proc && td->td_proc->p_comm[0] != '\0')
                        ? td->td_proc->p_comm : "unknown";

#if RNLOG_WITH_SEQ
    unsigned seq = __atomic_add_fetch(&rn_seq_cpu[cpu], 1, __ATOMIC_RELAXED);
#endif
#if RNLOG_WITH_SESSION
    unsigned sess = __atomic_load_n(&rn_session, __ATOMIC_RELAXED);
#endif

#if RNLOG_WITH_SEQ && RNLOG_WITH_SESSION
    log(LOG_INFO, "%u,%u,%s,%s,%d,%u,%u\n",
        cpu, tid, pname, (func ? func : "unknown"), tindex, seq, sess);
#elif RNLOG_WITH_SEQ && !RNLOG_WITH_SESSION
    log(LOG_INFO, "%u,%u,%s,%s,%d,%u\n",
        cpu, tid, pname, (func ? func : "unknown"), tindex, seq);
#elif !RNLOG_WITH_SEQ && RNLOG_WITH_SESSION
    log(LOG_INFO, "%u,%u,%s,%s,%d,%u\n",
        cpu, tid, pname, (func ? func : "unknown"), tindex, sess);
#else
    log(LOG_INFO, "%u,%u,%s,%s,%d\n",
        cpu, tid, pname, (func ? func : "unknown"), tindex);
#endif
}

/* ===================== DUMPS DE MATRICES (a syslog) ===================== */

/* Globals definidas en otro TU */
extern int CPU_NUMBER_PLACES;
extern int CPU_NUMBER_TRANSITIONS;
extern struct petri_cpu_resource_net *resource_net;

/* helpers internos */
static int
rn_abs_i(int x) { return x < 0 ? -x : x; }

/* ancho de celda común, calculado sobre AMBAS matrices para alinear */
static int
rn_cellw_global(int rows, int cols, char **A, char **B)
{
    int maxv = 0;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int v1 = rn_abs_i(A[r][c]);
            int v2 = rn_abs_i(B[r][c]);
            if (v1 > maxv) maxv = v1;
            if (v2 > maxv) maxv = v2;
        }
    }
    int d = 1;
    while (maxv >= 10) { maxv /= 10; d++; }
    if (d < 1) d = 1;
    d += 2;                  /* signo + espacio */
    return d < 4 ? 4 : d;    /* mínimo ancho decente */
}

static void
rn_log_header_cols(int cols, int cellw)
{
    log(LOG_INFO, "%*s", 6, "");
    for (int c = 0; c < cols; c++)
        log(LOG_INFO, "%*d", cellw, c);
    log(LOG_INFO, "\n");
}

static void
rn_log_matrix_full(const char *title, int rows, int cols, int cellw, char **M)
{
    log(LOG_INFO, "\n=== %s (size %dx%d) ===\n", title, rows, cols);
    rn_log_header_cols(cols, cellw);
    for (int r = 0; r < rows; r++) {
        log(LOG_INFO, "%-5d|", r);
        for (int c = 0; c < cols; c++)
            log(LOG_INFO, "%*d", cellw, M[r][c]);
        log(LOG_INFO, "\n");
    }
}

/* dump global completo (incidence + inhibition) */
void
rn_dump_resource_matrices_full(void)
{
    const int rows = CPU_NUMBER_PLACES;
    const int cols = CPU_NUMBER_TRANSITIONS;
    const int cellw = rn_cellw_global(rows, cols,
                                      resource_net->incidence_matrix,
                                      resource_net->inhibition_matrix);

    rn_log_matrix_full("Incidence Matrix",  rows, cols, cellw, resource_net->incidence_matrix);
    rn_log_matrix_full("Inhibition Matrix", rows, cols, cellw, resource_net->inhibition_matrix);
}

/* ---- Dump por-CPU (opcional) ---- */

static void
rn_log_matrix_block(const char *title, int r0, int nr, int c0, int nc, char **M)
{
    /* ancho de celda en base al bloque */
    int maxv = 0;
    for (int r = 0; r < nr; r++)
        for (int c = 0; c < nc; c++) {
            int v = rn_abs_i(M[r0 + r][c0 + c]);
            if (v > maxv) maxv = v;
        }
    int d = 1; while (maxv >= 10) { maxv /= 10; d++; }
    const int cellw = (d + 2 < 4) ? 4 : d + 2;

    log(LOG_INFO, "=== %s (rows %d..%d, cols %d..%d) ===\n",
        title, r0, r0 + nr - 1, c0, c0 + nc - 1);

    /* encabezado de columnas relativo */
    log(LOG_INFO, "%*s", 6, "");
    for (int c = 0; c < nc; c++)
        log(LOG_INFO, "%*d", cellw, c);
    log(LOG_INFO, "\n");

    for (int r = 0; r < nr; r++) {
        log(LOG_INFO, "%-5d|", r0 + r);
        for (int c = 0; c < nc; c++)
            log(LOG_INFO, "%*d", cellw, M[r0 + r][c0 + c]);
        log(LOG_INFO, "\n");
    }
}

void
rn_dump_resource_matrices_cpu_block(int cpu_n)
{
    const int r0 = PLACE(cpu_n, 0);
    const int c0 = TRANSITION(cpu_n, 0);
    rn_log_matrix_block("Incidence Matrix [CPU block]",
                        r0, CPU_BASE_PLACES, c0, CPU_BASE_TRANSITIONS,
                        resource_net->incidence_matrix);
    rn_log_matrix_block("Inhibition Matrix [CPU block]",
                        r0, CPU_BASE_PLACES, c0, CPU_BASE_TRANSITIONS,
                        resource_net->inhibition_matrix);
}

/* ===================== Sysctl: dump “lindo” via sbuf ===================== */

/* prototipo (por si rn_cellw_global queda más abajo en otro refactor) */
static int rn_cellw_global(int rows, int cols, char **A, char **B);

/* Helpers para imprimir a sbuf en vez de log() */
static void
rn_sbuf_header_cols(struct sbuf *sb, int cols, int cellw)
{
    sbuf_printf(sb, "%*s", 6, "");
    for (int c = 0; c < cols; c++)
        sbuf_printf(sb, "%*d", cellw, c);
    sbuf_printf(sb, "\n");
}

static void
rn_sbuf_matrix_full(struct sbuf *sb, const char *title,
                    int rows, int cols, int cellw, char **M)
{
    sbuf_printf(sb, "=== %s (size %dx%d) ===\n", title, rows, cols);
    rn_sbuf_header_cols(sb, cols, cellw);
    for (int r = 0; r < rows; r++) {
        sbuf_printf(sb, "%-5d|", r);
        for (int c = 0; c < cols; c++)
            sbuf_printf(sb, "%*d", cellw, M[r][c]);
        sbuf_printf(sb, "\n");
    }
}

/* Handler del sysctl: arma y devuelve el dump como texto */
static int
sysctl_rn_dump(SYSCTL_HANDLER_ARGS)
{
    if (resource_net == NULL)
        return (ENOENT);

    const int rows = CPU_NUMBER_PLACES;
    const int cols = CPU_NUMBER_TRANSITIONS;
    const int cellw = rn_cellw_global(rows, cols,
        resource_net->incidence_matrix, resource_net->inhibition_matrix);

    struct sbuf *sb = sbuf_new_auto();
    if (sb == NULL)
        return (ENOMEM);

    sbuf_printf(sb, "########## Petri Resource Net Matrices ##########\n");
    rn_sbuf_matrix_full(sb, "Incidence Matrix",  rows, cols, cellw, resource_net->incidence_matrix);
    sbuf_printf(sb, "\n");
    rn_sbuf_matrix_full(sb, "Inhibition Matrix", rows, cols, cellw, resource_net->inhibition_matrix);
    sbuf_printf(sb, "\n");

    sbuf_finish(sb);
    int error = SYSCTL_OUT(req, sbuf_data(sb), sbuf_len(sb));
    sbuf_delete(sb);
    return (error);
}

/* nodo: kern.sched.rn.dump (devuelve dump “lindo” de las matrices) */
SYSCTL_PROC(_kern_sched, OID_AUTO, rn_dump,
    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE,
    NULL, 0, sysctl_rn_dump, "A",
    "Pretty dump of resource_net matrices");

#endif /* _KERNEL */
