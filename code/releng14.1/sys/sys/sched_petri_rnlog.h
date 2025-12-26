/*
 * sched_petri_rnlog.h — CSV logging helpers for resource_net (FreeBSD kernel)
 */

#ifndef _SYS_SCHED_PETRI_RNLOG_H_
#define _SYS_SCHED_PETRI_RNLOG_H_

#ifdef _KERNEL

#include <sys/types.h>
struct thread;             /* forward */

/* ===== Compile-time knobs (overridable via -D o options) ===== */
#ifndef RNLOG_WITH_SEQ
#define RNLOG_WITH_SEQ 1           /* default: habilitado para medir drops */
#endif

#ifndef RNLOG_WITH_SESSION
#define RNLOG_WITH_SESSION 0       /* default: deshabilitado */
#endif

/* (Opcional) helper para mapear índice → nombre de transición */
const char *rn_name_from_index(int tindex, char *buf, size_t size);

/* ===== API de control / logging ===== */
void rnlog_enable(int on);                 /* 0=off, 1=on */
void rnlog_set_cpumask(unsigned mask);
void rnlog_set_pid(int pid);
void rnlog_set_session(unsigned sess);

int  rnlog_should_log(struct thread *td);
void rn_log_transition(struct thread *td, int tindex,
                       const char *func, const char *note);

/* ===== Dumps de matrices (implementados en sched_petri_rnlog.c) ===== */
void rn_dump_resource_matrices_full(void);          /* dump global: incidence + inhibition */
void rn_dump_resource_matrices_cpu_block(int cpu);  /* dump solo bloque del CPU */

#endif /* _KERNEL */
#endif /* _SYS_SCHED_PETRI_RNLOG_H_ */
