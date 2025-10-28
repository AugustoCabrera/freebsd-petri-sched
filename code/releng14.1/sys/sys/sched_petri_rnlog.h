/*
 * sched_petri_rnlog.h — CSV logging helpers for resource_net (FreeBSD kernel)
 */

#ifndef _SCHED_PETRI_RNLOG_H_
#define _SCHED_PETRI_RNLOG_H_

#ifdef _KERNEL

#include <sys/types.h>
struct thread;

/* Compile-time knobs */
#ifndef RNLOG_WITH_SEQ
#define RNLOG_WITH_SEQ 0
#endif

#ifndef RNLOG_WITH_SESSION
#define RNLOG_WITH_SESSION 0
#endif

/* external from your scheduler code */
extern const char *rn_name_from_index(int tindex, char *buf, size_t size);

/* API */
int  rnlog_should_log(struct thread *td);
void rn_log_transition(struct thread *td, int tindex, const char *func, const char *note);
void rnlog_enable(int on);          /* 0=off, 1=on */
void rnlog_set_cpumask(unsigned mask);
void rnlog_set_pid(int pid);
void rnlog_set_session(unsigned sess);

#endif /* _KERNEL */
#endif /* _SCHED_PETRI_RNLOG_H_ */
