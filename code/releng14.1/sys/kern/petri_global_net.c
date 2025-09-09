/*
 ============================================================================
 Name        : petri_global_net.c
 Author      : 
 Version     :
 Copyright   : Your copyright notice
 Description : Hello World in C, Ansi-style
 ============================================================================
 */

#include <sys/types.h>
#include <sys/sched_petri.h>
#include <sys/syslog.h>
#include <sys/malloc.h>

int CPU_NUMBER;
int CPU_NUMBER_PLACES;
int CPU_NUMBER_TRANSITIONS;
int PER_CPU_LAST_TRANSITION;
int PLACE_GLOBAL_QUEUE;
int PLACE_SMP_NOT_READY;
int PLACE_SMP_READY;
int TRAN_REMOVE_GLOBAL_QUEUE;
int TRAN_START_SMP;
int TRAN_QUEUE_GLOBAL;

int print = 0;
int smp_set = 0;
struct petri_cpu_resource_net *resource_net;
int *monopolized_cpus_per_proc = NULL;

const int base_resource_matrix[CPU_BASE_PLACES][CPU_BASE_TRANSITIONS] = {
	/*Base matrix */
//AD EX EXID FRGL REMQ RETIN RETV SUS UNQ WUP	
	{ 0, 0,-1,-1, 0, 1, 1, 0,-1, 0 },//CPU
	{ 0, 1, 0, 0, 0,-1,-1, 0, 0, 0 },//EXEC
	{ 1, 0, 0, 0,-1, 0, 0, 0,-1, 0 },//Q
	{ 0, 0, 0, 0, 0, 0, 0, 1, 0,-1 },//SUSD
	{ 0,-1, 1, 1, 0, 0, 0, 0, 1, 0 }//TOEX
};

const int base_resource_inhibition_matrix[CPU_BASE_PLACES][CPU_BASE_TRANSITIONS] = {
	/*Base inhibition matrix */
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 },
	{ 1, 0, 0, 1, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};

int hierarchical_transitions[HIERARCHICAL_TRANSITIONS] = {
	TRAN_ADDTOQUEUE,
	TRAN_EXEC,
	TRAN_EXEC_IDLE,
	TRAN_RETURN_INVOL,
	TRAN_RETURN_VOL,
	TRAN_REMOVE_QUEUE
};

const int hierarchical_corresponse[HIERARCHICAL_TRANSITIONS] = { 
	TRAN_ON_QUEUE, 
	TRAN_SET_RUNNING, 
	TRAN_ON_QUEUE, 
	TRAN_SWITCH_OUT, 
	TRAN_TO_WAIT_CHANNEL, 
	TRAN_REMOVE, 	
	TRAN_ON_QUEUE, 	
	TRAN_REMOVE 
};

const char *transitions_names[] = {
	"ADDTOQUEUE_P0", "EXEC_P0", "EXEC_IDLE_P0", "FROM_GLOBAL_CPU_P0", "REMOVE_QUEUE_P0", "RETURN_INVOL_P0", "RETURN_VOL_P0", "SUSPEND_PROC_P0", "UNQUEUE_P0", "WAKEUP_PROC_P0",
	"ADDTOQUEUE_P1", "EXEC_P1", "EXEC_IDLE_P1", "FROM_GLOBAL_CPU_P1", "REMOVE_QUEUE_P1", "RETURN_INVOL_P1", "RETURN_VOL_P1", "SUSPEND_PROC_P1", "UNQUEUE_P1", "WAKEUP_PROC_P1",
	"ADDTOQUEUE_P2", "EXEC_P2", "EXEC_IDLE_P2", "FROM_GLOBAL_CPU_P2", "REMOVE_QUEUE_P2", "RETURN_INVOL_P2", "RETURN_VOL_P2", "SUSPEND_PROC_P2", "UNQUEUE_P2", "WAKEUP_PROC_P2",
	"ADDTOQUEUE_P3", "EXEC_P3", "EXEC_IDLE_P3", "FROM_GLOBAL_CPU_P3", "REMOVE_QUEUE_P3", "RETURN_INVOL_P3", "RETURN_VOL_P3", "SUSPEND_PROC_P3", "UNQUEUE_P3", "WAKEUP_PROC_P3",
	"REMOVE_GLOBAL_QUEUE", "START_SMP", "QUEUE_GLOBAL"
};

const char *cpu_places_names[] = { "CPU", "EXECUTING", "QUEUE", "SUSPENDED", "TOEXEC" };

static void resource_fire_single_transition(struct thread *pt, int transition_index);
int get_monopolized_cpu_by_proc_id(int proc_id);
bool toggle_active_cpu(int cpu, bool turn_off);
bool toggle_pin_cpu_to_proc(int proc_id, int cpu, bool release);
void allocate_resource_net(void);
void init_cpu_mark(int cpu_n);
void init_cpu_matrix(int cpu_n);
void init_global_resources(void);
void init_global_variables(void);
void init_per_cpu_resources(void);
void print_resource_net(void);

/* this is needed because mp_ncpus is set at runtime */
void 
init_global_variables(void)
{

	CPU_NUMBER 						= mp_ncpus;
	CPU_NUMBER_PLACES 				= (CPU_BASE_PLACES*CPU_NUMBER) + GLOBAL_PLACES;
	CPU_NUMBER_TRANSITIONS 			= (CPU_BASE_TRANSITIONS*CPU_NUMBER) + GLOBAL_TRANSITIONS;
	PER_CPU_LAST_TRANSITION	 		= (CPU_NUMBER_TRANSITIONS - GLOBAL_TRANSITIONS);
	PLACE_GLOBAL_QUEUE 				= (CPU_NUMBER_PLACES - 3);
	PLACE_SMP_NOT_READY 			= (CPU_NUMBER_PLACES - 2);
	PLACE_SMP_READY 				= (CPU_NUMBER_PLACES - 1);
	TRAN_REMOVE_GLOBAL_QUEUE 		= (CPU_NUMBER_TRANSITIONS - 3);
	TRAN_START_SMP 					= (CPU_NUMBER_TRANSITIONS - 2);
	TRAN_QUEUE_GLOBAL 				= (CPU_NUMBER_TRANSITIONS - 1);
}

void 
allocate_resource_net(void)
{

	resource_net = (struct petri_cpu_resource_net *)init_pointer(sizeof(struct petri_cpu_resource_net));
    
	resource_net->mark = (int *)init_pointer(CPU_NUMBER_PLACES * sizeof(int));

	resource_net->incidence_matrix = (char **)init_double_pointer(CPU_NUMBER_PLACES, CPU_NUMBER_TRANSITIONS, sizeof(char));
	resource_net->inhibition_matrix = (char **)init_double_pointer(CPU_NUMBER_PLACES, CPU_NUMBER_TRANSITIONS, sizeof(char));
}

void 
init_global_resources(void) 
{

	//add global hierarchical transitions
	hierarchical_transitions[PER_CPU_HIER_TRANSITIONS] = TRAN_QUEUE_GLOBAL;
	hierarchical_transitions[PER_CPU_HIER_TRANSITIONS + 1] = TRAN_REMOVE_GLOBAL_QUEUE;

	//Transition to remove from global queue
	resource_net->incidence_matrix[PLACE_GLOBAL_QUEUE][TRAN_REMOVE_GLOBAL_QUEUE] = -1;

	//Represents arc to queue on the global queue
	resource_net->incidence_matrix[PLACE_GLOBAL_QUEUE][TRAN_QUEUE_GLOBAL] = 1;

	//Transitions to go from smp not ready to ready
	resource_net->incidence_matrix[PLACE_SMP_NOT_READY][TRAN_START_SMP] = -1;
	resource_net->incidence_matrix[PLACE_SMP_READY][TRAN_START_SMP] = 1;

	//We add a token to SMP NOT READY
	resource_net->mark[PLACE_SMP_NOT_READY] = 1;

	//cpu pinned array: CPU_NUMBER elems initialized in -1
	monopolized_cpus_per_proc = (int *)init_pointer(CPU_NUMBER * sizeof(int));
	memset(monopolized_cpus_per_proc, -1, CPU_NUMBER * sizeof(int));
}

void
init_cpu_matrix(int cpu_n)
{

	for (int num_place = 0; num_place < CPU_BASE_PLACES; num_place++) {
		for (int num_transition = 0; num_transition < CPU_BASE_TRANSITIONS; num_transition++) {
 			resource_net->incidence_matrix[PLACE(cpu_n, num_place)][TRANSITION(cpu_n, num_transition)] = base_resource_matrix[num_place][num_transition];
			resource_net->inhibition_matrix[PLACE(cpu_n, num_place)][TRANSITION(cpu_n, num_transition)] = base_resource_inhibition_matrix[num_place][num_transition];
		}
	}

	//incidence between each cpu and global resources
	resource_net->incidence_matrix[PLACE_GLOBAL_QUEUE][TRANSITION(cpu_n, TRAN_FROM_GLOBAL_CPU)] = -1;

	if (cpu_n != 0) { //inhibit executing to cpus other than 0 because smp hasnt started
		resource_net->inhibition_matrix[PLACE_SMP_NOT_READY][TRANSITION(cpu_n, TRAN_FROM_GLOBAL_CPU)] = 1;
		resource_net->inhibition_matrix[PLACE_SMP_NOT_READY][TRANSITION(cpu_n, TRAN_EXEC)] = 1;
	}

	//inhibit smp execution when not ready
	resource_net->inhibition_matrix[PLACE_SMP_NOT_READY][TRANSITION(cpu_n, TRAN_ADDTOQUEUE)] = 1;
}

void
init_cpu_mark(int cpu_n)
{

	if (cpu_n == 0) //cpu 0 starts executing, others start available
		resource_net->mark[PLACE(cpu_n, PLACE_EXECUTING)] = 1;
	else
		resource_net->mark[PLACE(cpu_n, PLACE_CPU)] = 1;
}

void 
init_per_cpu_resources(void) 
{

	for (int cpu_n = 0; cpu_n < CPU_NUMBER; cpu_n++) {
		init_cpu_matrix(cpu_n);
		init_cpu_mark(cpu_n);
	}
}

void 
init_resource_net(void)
{

	init_global_variables();
	allocate_resource_net();
	init_per_cpu_resources();
	init_global_resources();

	log(LOG_KERN, "Petri scheduler resource net initialized\n");
}

static __inline int 
is_inhibited(int places_index, int transition_index) 
{
	return ((resource_net->inhibition_matrix[places_index][transition_index] == 1) && (resource_net->mark[places_index] > 0));
}

static __inline int 
is_hierarchical(int transition) 
{

	for (int i = 0; i < HIERARCHICAL_TRANSITIONS; i++) {
		if (transition == hierarchical_transitions[i] ||
			((transition < TRAN_REMOVE_GLOBAL_QUEUE) && 
			(transition % CPU_BASE_TRANSITIONS) == hierarchical_transitions[i]))
			return hierarchical_corresponse[i];
	}
	return 0;
}

bool 
is_cpu_suspended(int cpu_n)
{
	return resource_net->mark[PLACE(cpu_n, PLACE_SUSPENDED)] > 0;
}

/**
 * fire the transition passed as param to the function
 * and check for automatic transitions to be fired
*/
void resource_fire_net(struct thread *pt, int transition_index, char *func)
{

	if (pt) {
		if (!smp_set && smp_started) {
			smp_set = 1;
			resource_fire_single_transition(pt, TRAN_START_SMP);
		}

		if (transition_is_sensitized(transition_index)) {
			if (print > 0) {
				log(LOG_INFO, "(resource_net) from %s\tThread %2d (%s)\t-> %s\n", func, pt->td_tid, pt->td_proc->p_comm, transitions_names[transition_index]);
				print--;
			}
			resource_fire_single_transition(pt, transition_index);

		} else {
			log(LOG_WARNING, "(resource_net) from %s Thread %2d (%s), CPU%2d: %s (%d) no sensibilizada\n", func, pt->td_tid, pt->td_proc->p_comm, PCPU_GET(cpuid), transitions_names[transition_index], transition_index);
			print_resource_net();
		} 
	}
}

/**
 * update the resource net state and in case
 * of firing a transition hierarchical to another
 * in the threads net, fire the hierarchical transition
*/
static void 
resource_fire_single_transition(struct thread *pt, int transition_index) 
{
	int local_transition = 0;
	
	//Fire cpu net	
	for (int num_place = 0; num_place < CPU_NUMBER_PLACES; num_place++)
		resource_net->mark[num_place] += resource_net->incidence_matrix[num_place][transition_index];
	
	local_transition = is_hierarchical(transition_index);
	if (local_transition) //If we need to fire a local thread transition we fire it here
		thread_petri_fire(pt, local_transition, print); 
}

bool 
transition_is_sensitized(int transition_index) 
{

	for (int places_index = 0; places_index < CPU_NUMBER_PLACES; places_index++) {
		if (((resource_net->incidence_matrix[places_index][transition_index] < 0) && 
			//If incidence is positive we really dont care if there are tokens or not
			((resource_net->incidence_matrix[places_index][transition_index] + resource_net->mark[places_index]) < 0)) ||
			is_inhibited(places_index, transition_index)) {
			return false;
		}
	}

	return true;
}

/**
 * similar functioning to sched_4bsd pickcpu, but adding monopolizing cpus by threads
 * first check if the thread monopolized a cpu
 * if not, check if last cpu used by thread is available
 * and then check for other cpu availables to queue
*/
int 
resource_choose_cpu(struct thread* td) 
{
	int monopolized_cpu, last_cpu, proc_id;

	proc_id = td->td_proc->p_pid;

	monopolized_cpu = get_monopolized_cpu_by_proc_id(proc_id);
	if (monopolized_cpu != -1)
		return TRANSITION(monopolized_cpu, TRAN_ADDTOQUEUE);
	
	last_cpu = td->td_lastcpu;
	if (last_cpu != NOCPU && 
		THREAD_CAN_SCHED(td, last_cpu) &&
		transition_is_sensitized(TRANSITION(last_cpu, TRAN_ADDTOQUEUE)) &&
		cpu_available_for_proc(proc_id, last_cpu))
			return TRANSITION(last_cpu, TRAN_ADDTOQUEUE);

	//Only check for transitions of addtoqueue
	for (int transition_index = TRAN_ADDTOQUEUE; transition_index < PER_CPU_LAST_TRANSITION; transition_index += CPU_BASE_TRANSITIONS) {
		int cpu_number = (transition_index / CPU_BASE_TRANSITIONS);
		if (THREAD_CAN_SCHED(td, cpu_number) &&
			transition_is_sensitized(transition_index) &&
			cpu_available_for_proc(proc_id, cpu_number))
			return transition_index;
	}
	
	return TRAN_QUEUE_GLOBAL;
}

void 
resource_expulse_thread(struct thread *td, int flags, char *func) 
{
	int transition_number;

	transition_number = (flags & SW_VOL) ? 
						TRANSITION(td->td_lastcpu, TRAN_RETURN_VOL) : 
						TRANSITION(td->td_lastcpu, TRAN_RETURN_INVOL);
	td->td_frominh = (flags & SW_VOL) ? 1 : 0;
		
	resource_fire_net(td, transition_number, func);
}

bool 
toggle_active_cpu(int cpu, bool turn_off)
{

	if (cpu <= 0 || cpu >= CPU_NUMBER) {
		log(LOG_WARNING, "CPU %d on-off state cannot be changed\n", cpu);
		return false;
	}

	if (resource_net->mark[PLACE_SMP_READY] == 0) {
		log(LOG_WARNING, "cannot change CPU on-off state before SMP_READY\n");
		return false;
	}

	int transition = turn_off ? TRAN_SUSPEND_PROC : TRAN_WAKEUP_PROC;
	char *action = turn_off ? "turned off" : "turned on";

	if (transition_is_sensitized(TRANSITION(cpu, transition))) {
		//if turning on, we need to check if its already on? i.e. if i can suspend it
		//because i have doubts that if im not suspended, i can trigger TRAN_WAKEUP_PROC multiple times
		resource_fire_net(curthread, TRANSITION(cpu, transition), action);
		return true;
	} 
		
	log(LOG_WARNING, "CPU %d cannot be %s\n", cpu, action);

	return false;
}

void 
turn_off_cpu(int cpu) 
{

	if (toggle_active_cpu(cpu, TURN_OFF)) 
		log(LOG_INFO, "CPU %d turned off by Thread %2d\n", cpu, curthread->td_tid);
}

void 
turn_on_cpu(int cpu) 
{

	if (toggle_active_cpu(cpu, TURN_ON))
		log(LOG_INFO, "CPU %d turned on by Thread %2d\n", cpu, curthread->td_tid);
}

bool 
toggle_pin_cpu_to_proc(int proc_id, int cpu, bool release) 
{
	if (cpu >= CPU_NUMBER || cpu <= 0) {
		log(LOG_WARNING, "pin_cpu_to_proc error - CPU %d cannot be used to pin this thread\n", cpu);
		return false;
	}

	if (release) { //early release
		monopolized_cpus_per_proc[cpu] = -1;
		log(LOG_INFO, "CPU %d released by Process %2d\n", cpu, proc_id);
		return true;
	}

	if (!cpu_available_for_proc(proc_id, cpu) || is_cpu_suspended(cpu) || (proc_id < 1))
		return false;
		
	monopolized_cpus_per_proc[cpu] = proc_id; //monopolize
	log(LOG_INFO, "CPU %d monopolized by Process %2d\n", cpu, proc_id);

	return true;
}

bool 
release_cpu(int proc_id, int cpu) 
{

	return toggle_pin_cpu_to_proc(proc_id, cpu, RELEASE);
}

bool 
monopolize_cpu(int proc_id, int cpu) 
{

	return toggle_pin_cpu_to_proc(proc_id, cpu, MONOPOLIZE);
}

bool 
cpu_available_for_proc(int proc_id, int cpu) 
{

	return (monopolized_cpus_per_proc[cpu] == proc_id || monopolized_cpus_per_proc[cpu] == -1);
}

int 
get_monopolized_cpu_by_proc_id(int proc_id) 
{
    
	for (int i = 0; i < CPU_NUMBER; i++) 
        if (monopolized_cpus_per_proc[i] == proc_id) 
            return i;

    return -1;
}

void
get_monopolized_cpus(int *dst) 
{
    
	memcpy(dst, monopolized_cpus_per_proc, CPU_NUMBER * sizeof(int));
}

void 
print_resource_net(void) 
{

	for (int cpu_n = 0; cpu_n < CPU_NUMBER; cpu_n++) {
		int cpu_base_place = CPU_BASE_PLACE(cpu_n);
		log(LOG_WARNING, "\t(resource_net) CPU%2d: ", cpu_n);
		
		for (int i = 0; i < CPU_BASE_PLACES; i++) {
				log(LOG_WARNING, "%s (%d) | ", cpu_places_names[i], resource_net->mark[i + cpu_base_place]);
		}
		log(LOG_WARNING, "\n");
	}
	log(LOG_WARNING, "\t(resource_net) Cola global: %d | SMP_%s\n", resource_net->mark[PLACE_GLOBAL_QUEUE], resource_net->mark[PLACE_SMP_NOT_READY] == 1 ? "NOT_READY" : "READY");
}

/**
 * checking for null result of alloc
 * isnt needed as the flag M_WAITOK used
 * causes that the syscall never fail to alloc memory
 * i.e., keeps waiting until mem is available
*/
void** 
init_double_pointer(int rows, int cols, size_t size) 
{

    void** pointer = malloc(rows * sizeof(void*), M_DEVBUF, MALLOC_FLAGS);

    for (int i = 0; i < rows; i++) 
        pointer[i] = malloc(cols * size, M_DEVBUF, MALLOC_FLAGS);

    return pointer;
}

void * 
init_pointer(size_t size) 
{

    void* pointer = malloc(size, M_DEVBUF, MALLOC_FLAGS);

    return pointer;
}