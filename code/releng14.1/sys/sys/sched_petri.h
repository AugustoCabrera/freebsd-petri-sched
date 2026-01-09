#ifndef SCHED_PETRI_H
#define SCHED_PETRI_H

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/cpuset.h>
#include <sys/smp.h>

#define THREAD_CAN_SCHED(td, cpu)       \
       CPU_ISSET((cpu), &(td)->td_cpuset->cs_mask)

/* macros for common operations to obtain indexes of net places and transitions */
#define CPU_BASE_TRANSITION(cpu)  ((cpu) * CPU_BASE_TRANSITIONS)
#define CPU_BASE_PLACE(cpu)       ((cpu) * CPU_BASE_PLACES)

#define TRANSITION(cpu, transition)  (CPU_BASE_TRANSITION(cpu) + (transition))
#define PLACE(cpu, place)            (CPU_BASE_PLACE(cpu) + (place))

/* Definition of constants of the resource petri net */
#define CPU_BASE_PLACES 		6
#define CPU_BASE_TRANSITIONS	14
extern int CPU_NUMBER; //will be defined at runtime with mp_ncpus			
extern int CPU_NUMBER_PLACES; 		
extern int CPU_NUMBER_TRANSITIONS; 	
extern int PER_CPU_LAST_TRANSITION;	

/* constants for handling hierarchical transitions */
#define PER_CPU_HIER_TRANSITIONS 	6
#define GLOBAL_HIER_TRANSITIONS 	2
// #define HIERARCHICAL_TRANSITIONS 	(PER_CPU_HIER_TRANSITIONS + GLOBAL_HIER_TRANSITIONS)

/* Definitions of places of the CPU resource net */
#define PLACE_CPU        0
#define PLACE_EXECUTING  1
#define PLACE_QUEUE      2
#define PLACE_RESERVED   3
#define PLACE_TOEXEC     4
#define PLACE_DISABLED   5


#define GLOBAL_PLACES	3
extern int PLACE_GLOBAL_QUEUE; 	
extern int PLACE_SMP_NOT_READY; 
extern int PLACE_SMP_READY; 	

/* Definitions of transitions of the CPU resource net */
#define TRAN_ADDTOQUEUE             0   // AD
#define TRAN_EXEC                  1   // EX
#define TRAN_EXEC_IDLE             2   // EXID
#define TRAN_FROM_GLOBAL_CPU       3   // FRGL
#define TRAN_REMOVE_QUEUE          4   // REMQ
#define TRAN_RETURN_INVOL          5   // RETIN
#define TRAN_RETURN_VOL            6   // RETV

#define TRAN_CPU_RESERVE           7   // RESERV
#define TRAN_UNQUEUE               8   // UNQ
#define TRAN_CPU_UNRESERVE         9   // UNRES
#define TRAN_CPU_DISABLE          10   // DISABLE
#define TRAN_CPU_ENABLE           11   // ENABLE

#define TRAN_ADDTOQUEUE_BOUND     12   // ADDBOU
#define TRAN_FROM_GLOBAL_BOUND_CPU 13  // FRGLBOU

#define GLOBAL_TRANSITIONS	3
extern int TRAN_REMOVE_GLOBAL_QUEUE; 	
extern int TRAN_START_SMP; 				
extern int TRAN_QUEUE_GLOBAL; 		

#define TURN_OFF	true
#define TURN_ON 	false

#define RELEASE		true
#define MONOPOLIZE 	false

#define MALLOC_FLAGS (M_WAITOK | M_ZERO)

struct petri_cpu_resource_net {
	int *mark;
	char **incidence_matrix;
	char **inhibition_matrix;
};


/* Índices de plazas del net de HILOS (no confundir con los de recursos) */
enum thread_place_fsm {
    THREAD_INACTIVE  	= 0,
    THREAD_CAN_RUN   	= 1,
    THREAD_RUNQ      	= 2,
    THREAD_RUNNING   	= 3,
    THREAD_INHIBITED 	= 4
};

/* Tabla one-hot: fila = estado destino (5 enteros por fila) */
extern const int *thread_fire[THREADS_PLACES_SIZE];



//make the contract explicit for the thread FSM implementation */
#ifndef THREADS_PLACES_SIZE
#define THREADS_PLACES_SIZE 5
#endif


//Petri thread Methods
void init_petri_thread(struct thread *pt_thread);
void init_petri_thread0(struct thread *pt_thread);
//void thread_petri_fire(struct thread *pt, int transition, int print);
void wakeup_if_needed(struct thread *td);


/* =========================
 * Thread FSM (one-hot) API
 * =========================
 * The thread net is modeled as a one-hot FSM.
 * Each helper below writes the final marking in O(1) (no incidence loops).
 * Call them explicitly right after resource_fire_net(...) from sched_4bsd.
 */


//Petri Global Methods
int  resource_choose_cpu(struct thread *td);
bool cpu_available_for_proc(int proc_id, int cpu);
bool is_cpu_disabled(int cpu_n);
void get_monopolized_cpus(int *dst);
void free_double_pointer(void** pointer, int rows); 
bool transition_is_sensitized(int transition_index);
void **init_double_pointer(int rows, int cols, size_t size); 
void *init_pointer(size_t size); 
void init_resource_net(void);
bool monopolize_cpu(int proc_id, int cpu); 
bool release_cpu(int proc_id, int cpu); 
void resource_fire_net(struct thread *pt, int transition_index, const char *func);
void resource_expulse_thread(struct thread *td, int flags, const char *func);
void toggle_pin_thread_to_cpu(int thread_id, int cpu);
void turn_off_cpu(int cpu);
void turn_on_cpu(int cpu);

#endif