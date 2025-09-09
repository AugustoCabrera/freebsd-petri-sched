/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Petri-net based thread lifecycle (scheduler side).
 */

#include <sys/param.h>
#include <sys/sched_petri.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/systm.h>   /* KASSERT, DIAGNOSTIC */

/*
 * Sysctl entry (informational only).
 */
SYSCTL_STRING(_kern_sched, OID_AUTO, cpu_sel, CTLFLAG_RD, "PETRI", 0,
    "Scheduler pickcpu method");

/* ============================================================
 * Incidence matrix of the thread net (places × transitions)
 *
 * Places (THREADS_PLACES_SIZE):
 *   0: INACTIVE
 *   1: CAN_RUN
 *   2: RUNQ
 *   3: RUNNING
 *   4: INHIBITED
 *
 * Transitions (THREADS_TRANSITIONS_SIZE):
 *   0: TRAN_INIT
 *   1: TRAN_ON_QUEUE
 *   2: TRAN_SET_RUNNING
 *   3: TRAN_SWITCH_OUT
 *   4: TRAN_TO_WAIT_CHANNEL
 *   5: TRAN_WAKEUP
 *   6: TRAN_REMOVE
 * ============================================================
 */

/* Global data (read-only) */
const int incidence_matrix[THREADS_PLACES_SIZE][THREADS_TRANSITIONS_SIZE] = {
	/*             INIT  ON_Q  SET_RUN  SWITCH_OUT  TO_WAIT   WAKEUP  REMOVE */
	/* INACTIVE */ { -1,   0,     0,        0,         0,       0,      0   },
	/* CAN_RUN  */ {  1,  -1,     0,        1,         0,       1,      1   },
	/* RUNQ     */ {  0,   1,    -1,        0,         0,       0,     -1   },
	/* RUNNING  */ {  0,   0,     1,       -1,        -1,       0,      0   },
	/* INHIBITED*/ {  0,   0,     0,        0,         1,      -1,      0   }
};

const int initial_mark[THREADS_PLACES_SIZE]  = { 0, 1, 0, 0, 0 };
const int initial_mark0[THREADS_PLACES_SIZE] = { 0, 0, 0, 1, 0 };

const char *thread_transitions_names[THREADS_TRANSITIONS_SIZE] = {
	"TRAN_INIT", "TRAN_ON_QUEUE", "TRAN_SET_RUNNING",
	"TRAN_SWITCH_OUT", "TRAN_TO_WAIT_CHANNEL", "TRAN_WAKEUP", "TRAN_REMOVE"
};

const char *thread_places[THREADS_PLACES_SIZE] = {
	"INACTIVE", "CAN_RUN", "RUNQ", "RUNNING", "INHIBITED"
};

/* Internal prototypes */
static inline bool thread_transition_is_sensitized(struct thread *pt, int transition_index);
static void thread_print_net(struct thread *pt);

/* ============================================================
 * Thread net initialization
 * ============================================================
 */

void
init_petri_thread(struct thread *pt_thread)
{
	/* Regular threads start in CAN_RUN */
	for (int i = 0; i < THREADS_PLACES_SIZE; i++)
		pt_thread->mark[i] = initial_mark[i];
	pt_thread->td_frominh = 0;
}

void
init_petri_thread0(struct thread *pt_thread)
{
	/* Thread 0 starts in RUNNING */
	for (int i = 0; i < THREADS_PLACES_SIZE; i++)
		pt_thread->mark[i] = initial_mark0[i];
	pt_thread->td_frominh = 0;
}

/* ============================================================
 * Sensitization helper (diagnostics only).
 * At runtime, the resource net guarantees sensitization,
 * so thread_petri_fire() does not need to re-check it.
 * ============================================================
 */

static inline bool
thread_transition_is_sensitized(struct thread *pt, int transition_index)
{
	for (int places_index = 0; places_index < THREADS_PLACES_SIZE; places_index++) {
		if ((incidence_matrix[places_index][transition_index] < 0) &&
		    ((incidence_matrix[places_index][transition_index] + pt->mark[places_index]) < 0))
			return false;
	}
	return true;
}

/* ============================================================
 * Fire a thread-level transition (hierarchical to resources).
 * ============================================================
 */

void
thread_petri_fire(struct thread *pt, int transition, int print)
{
#ifdef DIAGNOSTIC
	/* In debug builds, detect desynchronizations with the resource net. */
	KASSERT(thread_transition_is_sensitized(pt, transition),
	    ("thread transition %d not sensitized (desync with resource net)", transition));
#endif

	for (int i = 0; i < THREADS_PLACES_SIZE; i++)
		pt->mark[i] += incidence_matrix[i][transition];

	if (print > 0) {
		const char *tname =
		    (transition >= 0 && transition < THREADS_TRANSITIONS_SIZE)
		        ? thread_transitions_names[transition]
		        : "UNKNOWN";
		log(LOG_INFO, "\t(sched_petri) %s fired for thread %d\n", tname, pt->td_tid);
	}
}

/* ============================================================
 * Conditional wakeup (INHIBITED → WAKEUP).
 * ============================================================
 */

void
wakeup_if_needed(struct thread *td)
{
	if (td && (td->td_frominh == 1)) {
		thread_petri_fire(td, TRAN_WAKEUP, -1);
		td->td_frominh = 0;
	}
}

/* ============================================================
 * Debug helper: print the current marking of a thread net.
 * ============================================================
 */

static void
thread_print_net(struct thread *pt)
{
	log(LOG_WARNING, "\t\t(sched_petri) Thread %2d state:", pt->td_tid);
	for (int i = 0; i < THREADS_PLACES_SIZE; i++) {
		if (pt->mark[i] > 0)
			log(LOG_WARNING, " %s(%d)", thread_places[i], pt->mark[i]);
	}
	log(LOG_WARNING, "\n");
}
