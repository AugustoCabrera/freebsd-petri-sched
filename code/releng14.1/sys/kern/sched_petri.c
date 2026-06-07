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

/* One-hot (orden: INACTIVE, CAN_RUN, RUNQ, RUNNING, INHIBITED) */
static const int MARK_INACTIVE [5] = {1,0,0,0,0};
static const int MARK_CAN_RUN  [5] = {0,1,0,0,0};
static const int MARK_RUNQ     [5] = {0,0,1,0,0};
static const int MARK_RUNNING  [5] = {0,0,0,1,0};
static const int MARK_INHIBITED[5] = {0,0,0,0,1};

const int *thread_fire[THREADS_PLACES_SIZE] = {
    MARK_INACTIVE, MARK_CAN_RUN, MARK_RUNQ, MARK_RUNNING, MARK_INHIBITED
};



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

// /* Internal prototypes */
// static void thread_print_net(struct thread *pt);

/* ============================================================
 * Thread net initialization
 * ============================================================
 */

/* Inits usando asignación directa de puntero (sin casts) */
void init_petri_thread(struct thread *pt) {
    pt->mark = thread_fire[THREAD_CAN_RUN];
    pt->td_frominh = 0;
}

void init_petri_thread0(struct thread *pt) {
    pt->mark = thread_fire[THREAD_RUNNING];
    pt->td_frominh = 0;
}




/* ============================================================
 * Conditional wakeup (INHIBITED → WAKEUP).
 * ============================================================
 */

void wakeup_if_needed(struct thread *td) {
    if (td && td->td_frominh == 1) {
        td->mark = thread_fire[THREAD_CAN_RUN];
        td->td_frominh = 0;
    }
}

/* ============================================================
 * Debug helper: print the current marking of a thread net.
 * ============================================================
 */

// static void
// thread_print_net(struct thread *pt)
// {
// 	log(LOG_WARNING, "\t\t(sched_petri) Thread %2d state:", pt->td_tid);
// 	for (int i = 0; i < THREADS_PLACES_SIZE; i++) {
// 		if (pt->mark[i] > 0)
// 			log(LOG_WARNING, " %s(%d)", thread_places[i], pt->mark[i]);
// 	}
// 	log(LOG_WARNING, "\n");
// }
