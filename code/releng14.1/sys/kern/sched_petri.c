#include <sys/sched_petri.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>

SYSCTL_STRING(_kern_sched, OID_AUTO, cpu_sel, CTLFLAG_RD, "PETRI", 0,
    "Scheduler pickcpu method");

/* GLOBAL VARIABLES */
const int incidence_matrix[THREADS_PLACES_SIZE][THREADS_TRANSITIONS_SIZE] = {
	{-1,  0,  0,  0,  0,  0,  0},
	{ 1, -1,  0,  1,  0,  1,  1},
	{ 0,  1, -1,  0,  0,  0, -1},
	{ 0,  0,  1, -1, -1,  0,  0},
	{ 0,  0,  0,  0,  1, -1,  0}
};

const int initial_mark[THREADS_PLACES_SIZE] = { 0, 1, 0, 0, 0 };
const int initial_mark0[THREADS_PLACES_SIZE] = { 0, 0, 0, 1, 0 };

const char *thread_transitions_names[THREADS_TRANSITIONS_SIZE] = {
	"TRAN_INIT", "TRAN_ON_QUEUE", "TRAN_SET_RUNNING", "TRAN_SWITCH_OUT", "TRAN_TO_WAIT_CHANNEL", "TRAN_WAKEUP", "TRAN_REMOVE"
};

const char *thread_places[THREADS_PLACES_SIZE] = {
	"INACTIVE", "CAN_RUN", "RUNQ", "RUNNING", "INHIBITED"
};

__inline bool thread_transition_is_sensitized(struct thread *pt, int transition_index);
void thread_print_net(struct thread *pt);

void
init_petri_thread(struct thread *pt_thread)
{
	// Create a new petr_thread structure
	for (int i = 0; i < THREADS_PLACES_SIZE; i++) 
		pt_thread->mark[i] = initial_mark[i];
	
	pt_thread->td_frominh = 0;
}

void
init_petri_thread0(struct thread *pt_thread)
{
	// Create a new petr_thread structure
	for (int i = 0; i < THREADS_PLACES_SIZE; i++) 
		pt_thread->mark[i] = initial_mark0[i];
	
	pt_thread->td_frominh = 0;
}

__inline bool
thread_transition_is_sensitized(struct thread *pt, int transition_index)
{
	for (int places_index = 0; places_index < THREADS_PLACES_SIZE; places_index++) {
		if (((incidence_matrix[places_index][transition_index] < 0) && 
			//If incidence is positive we really dont care if there are tokens or not
			((incidence_matrix[places_index][transition_index] + pt->mark[places_index]) < 0))) 
			return false;
	}

	return true;
}

void
thread_petri_fire(struct thread *pt, int transition, int print)
{
	if (thread_transition_is_sensitized(pt, transition)) 
		for(int i = 0; i < THREADS_PLACES_SIZE; i++)
			pt->mark[i] += incidence_matrix[i][transition];
	else {
		log(LOG_WARNING, "\t(sched_petri) %s no estaba sensibilizada para thread %d\n", thread_transitions_names[transition % CPU_BASE_TRANSITIONS], pt->td_tid);
		thread_print_net(pt);
	}
}

void
wakeup_if_needed(struct thread *td)
{
	if (td && (td->td_frominh == 1)) {
		thread_petri_fire(td, TRAN_WAKEUP, -1);
		td->td_frominh = 0;
	}
}

void 
thread_print_net(struct thread *pt)
{
	log(LOG_WARNING, "\t\t(sched_petri) Estado thread %2d: ", pt->td_tid);
	for (int i = 0; i < THREADS_PLACES_SIZE; i++)
		if (pt->mark[i] == 1)
			log(LOG_WARNING, "%s\n", thread_places[i]);
}