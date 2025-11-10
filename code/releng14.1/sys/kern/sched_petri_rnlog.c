/*
 * sched_petri_rnlog.c — CSV logging implementation for resource_net (FreeBSD kernel)
 */

#include <sys/sched_petri_rnlog.h>

#ifdef _KERNEL

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/proc.h>
#include <sys/pcpu.h>
#include <sys/syslog.h>   /* LOG_INFO */

/* Estado de captura y filtros */
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

/* Setters programáticos (usan acceso atómico donde corresponde) */
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

#endif /* _KERNEL */
