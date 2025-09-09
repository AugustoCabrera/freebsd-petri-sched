#include "../include/modlib.h"

#include <sys/metadata_elf_reader.h>

struct threads_stats threads_stats, stats_aux;
struct cpu_monopolization *cpu_monopolization_info = NULL;

/* funcion que permite publicar una struct de valor dinamico para acceder mediante sysctl */
static int sysctl_get_threads_stats(SYSCTL_HANDLER_ARGS) {
    if (req->newptr == NULL) 
        return sysctl_handle_opaque(oidp, &threads_stats, sizeof(struct threads_stats), req);

    return EPERM;
}

static int sysctl_get_monopolization_info(SYSCTL_HANDLER_ARGS) {
    if (req->newptr == NULL) 
        return sysctl_handle_opaque(oidp, cpu_monopolization_info, CPU_NUMBER * sizeof(struct cpu_monopolization), req);

    return EPERM;
}

SYSCTL_PROC(_kern, OID_AUTO, threads_stats, CTLTYPE_OPAQUE | CTLFLAG_RD | CTLFLAG_MPSAFE,
    0, 0, sysctl_get_threads_stats, "A", "proc to share threads stats struct");

SYSCTL_PROC(_kern, OID_AUTO, monopolization_info, CTLTYPE_OPAQUE | CTLFLAG_RD | CTLFLAG_MPSAFE,
    0, 0, sysctl_get_monopolization_info, "A", "proc to share cpu monopolization info");

__inline bool   is_idle_proc(char *proc_name);
void            get_proc_metadata_sched_info(struct proc *p);
void            get_sys_stats_from_procs(bool idle_proc, int estcpu_proc, int pctcpu_proc);
void            get_threads_in_proc_stats(struct proc *p, int *estcpu_proc, int *pctcpu_proc);
void            log_threads_stats(void);
void            read_thread_stats(void);
static void     timer_callback(void *arg);

static struct callout timer;

const char *PROC_NEEDS_TYPES[N_PROC_NEEDS] = { "LOWPERF" , "STANDARD" , "HIGHPERF" , "CRITICAL" };

static int event_handler(struct module *module, int event, void *arg) {
    int e = 0; /* Error, 0 for normal return status */
    switch (event) 
    {
        case MOD_LOAD:
            //hz/2 is equivalent to sample twice every second
            init_timer(&timer, hz/2, &timer_callback, NULL); 

            cpu_monopolization_info = (struct cpu_monopolization*)init_pointer(sizeof(struct cpu_monopolization) * CPU_NUMBER);

            for (int i = 0; i < CPU_NUMBER; i++) {
                cpu_monopolization_info[i].requested = false;
                cpu_monopolization_info[i].pid = -1;
                cpu_monopolization_info[i].prio = 10000;//simulate a low priority process for sorting purposes
            }

            log(LOG_INFO | LOG_LOCAL0, "loading thread_stats\n");
            break;
        
        case MOD_UNLOAD:
            callout_drain(&timer);

            free(cpu_monopolization_info, M_DEVBUF);

            log(LOG_INFO | LOG_LOCAL0, "thread_stats unloaded\n");
            break;

        default:
            e = EOPNOTSUPP; /* Error, Operation Not Supported */
            break;
    }

    return (e);
}

void 
read_thread_stats(void) 
{
	struct proc *p;
    static int num_procs = -1;
    static int log_count = 0;

    //reset aux struct (except metadata info)
    stats_aux.estcpu_idle = 0;
    stats_aux.estcpu_work = 0;
    stats_aux.procs_active = 0;
    stats_aux.procs_in_system = 0;
    stats_aux.threads_active = 0;
    stats_aux.threads_in_system = 0;
	
	FOREACH_PROC_IN_SYSTEM(p) {
        int estcpu_proc = 0, pctcpu_proc = 0;

		get_threads_in_proc_stats(p, &estcpu_proc, &pctcpu_proc);

        get_sys_stats_from_procs(is_idle_proc(p->p_comm), estcpu_proc, pctcpu_proc);
        
    }
   
    //obtain processes info only when there are new/deleted ones
    if (num_procs != stats_aux.procs_in_system) {
        memset(&stats_aux.proc_needs, 0, sizeof(stats_aux.proc_needs));
        memset(cpu_monopolization_info, -1, sizeof(struct cpu_monopolization) * CPU_NUMBER);

        for (int i = 0; i < CPU_NUMBER; i++) {//i need to obtain the info of cpus requested again
            cpu_monopolization_info[i].requested = false;//so we "release" them
        }

	    FOREACH_PROC_IN_SYSTEM(p) {
            get_proc_metadata_sched_info(p);
        }
    } 

    num_procs = stats_aux.procs_in_system; //update nprocs

    // Update the "published" struc
    memcpy(&threads_stats, &stats_aux, sizeof(struct threads_stats));

    if (log_count++ >= 9) {
        log_threads_stats();
        log_count = 0;
    }
}

void 
get_proc_metadata_sched_info(struct proc *p)
{
	char str[SCHEDPAYLOAD_STRING_MAXSIZE] = {0}; 
    bool monopolize;

    if (is_idle_proc(p->p_comm))
        return;
    
    if (p->p_metadata_section_flag == 1) {
        decodeMetadataSectionProc(p, str, &monopolize);

        if (monopolize) {
            log(LOG_INFO | LOG_LOCAL0, "%s: wants a CPU\n\n", p->p_comm);

            int less_index = -1;
            int less_prio = -1000;

            //see if there is a cpu not requested, or the cpu requested by the lesser prioritized process
            for (int i = 1; i < CPU_NUMBER; i++) { //avoid cpu 0 just in case
                if (!cpu_monopolization_info[i].requested) {
                    less_index = i;
                    break;
                }

                if (cpu_monopolization_info[i].prio > less_prio) {//if there are more than CPU_NUMBER requesteds 
                    less_index = i;//i look for the lesser prioritized process
                    less_prio = cpu_monopolization_info[i].prio;
                }
            }
            
            //if there is cpu not requested or the current priority is higher than the lesser process that requested (less in number)
            if (!cpu_monopolization_info[less_index].requested || curthread->td_priority < less_prio) {
                cpu_monopolization_info[less_index].requested = true;
                cpu_monopolization_info[less_index].pid = p->p_pid;
                cpu_monopolization_info[less_index].prio = curthread->td_priority;
            }
        }

        if (strncmp(str, "LOWPERF", strlen("LOWPERF")) == 0) 
            stats_aux.proc_needs[PROC_LOWPERF]++;
        else if (strncmp(str, "STANDARD", strlen("STANDARD")) == 0) 
            stats_aux.proc_needs[PROC_STANDARD]++;
        else if (strncmp(str, "HIGHPERF", strlen("HIGHPERF")) == 0) 
            stats_aux.proc_needs[PROC_HIGHPERF]++;
        else if (strncmp(str, "CRITICAL", strlen("CRITICAL")) == 0) 
            stats_aux.proc_needs[PROC_CRITICAL]++;
    } else
        stats_aux.proc_needs[PROC_STANDARD]++; //procs with no info treated as standard
}

/**
 * si los hilos de un proceso tuvieron tiempo de cpu ultimamente
 * son considerados activos, asi puedo saber cuantos procesos
 * hay en total y cuantos estan usando cpu
 * (por el momento, esta estadistica se colecta pero no tiene uso)
*/
void
get_sys_stats_from_procs(bool idle_proc, int estcpu_proc, int pctcpu_proc)
{

    if (estcpu_proc > 0 || pctcpu_proc > 0) {
        if (idle_proc)
            stats_aux.estcpu_idle += estcpu_proc;
        else 
            stats_aux.estcpu_work += estcpu_proc;

        stats_aux.procs_active++;
    }
    stats_aux.procs_in_system++;
}

/**
 * me fijo la actividad de los hilos de cada proceso
 * para saber cuantos hay en total y cuantos activos
 * y en caso de ser activos sumo al total de la actividad
 * del proceso al que pertenecen
 * (por el momento, esta estadistica se colecta pero no tiene uso)
*/
void 
get_threads_in_proc_stats(struct proc *p, int *estcpu_proc, int *pctcpu_proc)
{
    struct thread *td;

    FOREACH_THREAD_IN_PROC(p, td) {
        u_int td_estcpu = sched_estcpu(td);
        fixpt_t td_pctcpu = sched_pctcpu(td);

        if (td_estcpu > 0 || td_pctcpu > 0) {
            (*estcpu_proc) += td_estcpu;
            (*pctcpu_proc) += td_pctcpu;

            stats_aux.threads_active++;
        }   
        stats_aux.threads_in_system++;
    }
}

__inline bool 
is_idle_proc(char *proc_name)
{
    return (strncmp(proc_name, "idle", 4) == 0 && proc_name[4] == '\0');
}  

void
log_threads_stats(void)
{
    char procs_needs[MAX_STRING_LENGTH];

    log(LOG_INFO | LOG_LOCAL0, "IDLE_ESTCPU: %d\nWORKING_ESTCPU: %d\n\n", threads_stats.estcpu_idle, threads_stats.estcpu_work);

    log(LOG_INFO | LOG_LOCAL0, "THREADS_IN_SYS: %d\nACTIVE_THREADS: %d\n\n", threads_stats.threads_in_system, threads_stats.threads_active);

    log(LOG_INFO | LOG_LOCAL0, "PROCS_IN_SYS: %d\nACTIVE_PROCS: %d\n\n", threads_stats.procs_in_system, threads_stats.procs_active);

    int  offset = 0;
    for (int i = 0; i < N_PROC_NEEDS; i++) {
        offset += snprintf(procs_needs + offset, sizeof(procs_needs) - offset, "%s: %d\n", PROC_NEEDS_TYPES[i], threads_stats.proc_needs[i]);
    }
    
	log(LOG_INFO | LOG_LOCAL0, "%s\n---------------------------\n\n", procs_needs);
}

static void 
timer_callback(void *arg) 
{

    read_thread_stats();
    callout_reset(&timer, obtain_random_freq(), timer_callback, NULL);
}

static moduledata_t thread_stats_data = {
    "thread_stats",
    event_handler,
    NULL
};

DECLARE_MODULE(thread_stats, thread_stats_data, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(thread_stats, 2);