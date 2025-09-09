#include "../include/modlib.h"

int         calc_load_score(int score);
int         compareProcesses(const void *a, const void *b); 
int         get_idlest_cpu(void);
int         get_monopolization_info(int *pids_sorted_by_prio);
int         get_n_turned_off(void);
int         get_off_cpu(void);
int         get_procs_needs(void);
int         get_user_load(void);
int         get_sys_load(void);
int         processes_need_score(int *procs_per_need_type);
void        *obtain_stats(const char *sysctl_name, size_t ptr_size);
static void timer_callback_monopolization(void *arg);
static void timer_callback_stats(void *arg);
static void timer_callback_turn_off(void *arg);
static void timer_callback_turn_on(void *arg);

static struct callout timer_monopolization;
static struct callout timer_stats;
static struct callout timer_turn_off;
static struct callout timer_turn_on;

static int monopolization_interval_sec = 1;
static int stats_interval_sec = 1;
static int turn_off_interval_sec = 30;
static int turn_on_interval_sec = 2;

static int MAX_TURNED_OFF;

bool *turned_off_cpus;

int cpus_requested = 0;//max(processes_monopolizing, CPU_NUMBER)
int check = 0;//check "stability" of low load measures 
int stats_score = 0;

static int event_handler(struct module *module, int event, void *arg) {
    int e = 0; /* Error, 0 for normal return status */
    switch (event) 
    {
        case MOD_LOAD:
            init_timer(&timer_monopolization, monopolization_interval_sec*hz, &timer_callback_monopolization, NULL); 
            init_timer(&timer_stats, stats_interval_sec*hz, &timer_callback_stats, NULL); 
            init_timer(&timer_turn_off, turn_off_interval_sec*hz, &timer_callback_turn_off, NULL); 
            init_timer(&timer_turn_on, turn_on_interval_sec*hz, &timer_callback_turn_on, NULL); 
            
            turned_off_cpus = (bool *)init_pointer(CPU_NUMBER * sizeof(bool));
            MAX_TURNED_OFF = (CPU_NUMBER / 2) - 1;

            log(LOG_INFO | LOG_LOCAL2, "toggle_active_cpu loaded\n");
            break;

        case MOD_UNLOAD:
            {
                int off_cpu = get_off_cpu();
                while (off_cpu > 0) {
                    turn_on_cpu(off_cpu);
                    turned_off_cpus[off_cpu] = false;
                    log(LOG_INFO | LOG_LOCAL2, "CPU %d turned on when unloading module\n", off_cpu);
                    off_cpu = get_off_cpu();
                }

                for (int i = 1; i < CPU_NUMBER; i++) {
                    release_cpu(0, i);
                }
            }
            
            free(turned_off_cpus, M_DEVBUF);

            callout_drain(&timer_monopolization);
            callout_drain(&timer_stats);
            callout_drain(&timer_turn_off);
            callout_drain(&timer_turn_on);

            log(LOG_INFO | LOG_LOCAL2, "toggle_active_cpu unloaded\n");
            break;

        default:
            e = EOPNOTSUPP; /* Error, Operation Not Supported */
            break;
    }

    return (e);
}

static void
timer_callback_monopolization(void *arg)
{
    static int monopolized = 0;

    //este arreglo contiene los pid de los CPU_NUMBER procesos de mayor prioridad que quieren monopolizar
    int *monopolization_pids = (int *)init_pointer(CPU_NUMBER * sizeof(int));

    cpus_requested = get_monopolization_info(monopolization_pids);

    int *cpus = (int *)init_pointer(CPU_NUMBER * sizeof(int));
    get_monopolized_cpus(cpus);//array of cpus with their monopolizer pid

    //release monopolized cpus if the pid it belongs to isnt in the most prioritized
    for (int n_cpu = 1; n_cpu < CPU_NUMBER; n_cpu++) {
        if (cpus[n_cpu] == -1 || is_cpu_suspended(n_cpu))//if not monopolized or is_susp, continue
            continue; 

        bool keep = false;
        for (int j = 0; j < CPU_NUMBER; j++) {//check if proc that monopolizes this cpu is still prioritized
            if (cpus[n_cpu] == monopolization_pids[j]) {
                keep = true;
                break;
            }
        }

        if (!keep) {
            release_cpu(cpus[n_cpu], n_cpu);
            log(LOG_INFO | LOG_LOCAL2, "CPU %d released by %d\n", n_cpu, cpus[n_cpu]);
            monopolized--;
        }
    }

    //monopolize cpu
    if (cpus_requested > 0) {
        //for now, only one cpu can be monopolized (cpu 1). it needs to be improved.
        for (int n_cpu = 1; n_cpu < CPU_NUMBER; n_cpu++) {//skip cpu 0
            if ((cpus[n_cpu] == -1) && !is_cpu_suspended(n_cpu) && (monopolized < MAX_TURNED_OFF)) {
                if (monopolize_cpu(monopolization_pids[n_cpu - 1], n_cpu)) {//minus 1 because started in index 1
                    log(LOG_INFO | LOG_LOCAL2, "CPU %d monopolized by %d\n", n_cpu, monopolization_pids[n_cpu - 1]);
                    monopolized++;
                }
            }
        }
    }

    free(monopolization_pids, M_DEVBUF);
    free(cpus, M_DEVBUF);

    callout_schedule(&timer_monopolization, monopolization_interval_sec*hz);
}

/**
 * cada 1s recopilo estadisticas
 * de uso de cpu y necesidades de usuario
 * y calculo el score
*/
static void 
timer_callback_stats(void *arg) 
{

    int user_load = get_user_load();
    int procs_needs = get_procs_needs();

    stats_score = (user_load*80 + procs_needs*20) / 100;

    callout_schedule(&timer_stats, stats_interval_sec*hz);
}

/**
 * cada ~30 secs, chequeo el uso de cpu del sistema
 * y los requerimientos de los procesos
 * para determinar si puedo apagar un core o no
 * luego de 3 mediciones de bajo uso, apago 1 cpu
*/
static void 
timer_callback_turn_off(void *arg) 
{
    
    if ((stats_score < LOAD_NORMAL) && (cpus_requested < 1)) {
        
        if (check++ >= 3) {//if there isnt a process that request a cpu & at least 90 secs with low load
            check = 0;//reset count of times obtaining a low load measure
            
            if (get_n_turned_off() < MAX_TURNED_OFF) {
                int idlest_cpu = get_idlest_cpu();

                if (idlest_cpu > 0) {
                    turn_off_cpu(idlest_cpu);
                    turned_off_cpus[idlest_cpu] = true;

                    log(LOG_INFO | LOG_LOCAL2, "CPU %d turned off\n", idlest_cpu);
                }
            }
        }
    } 
    
    callout_schedule(&timer_turn_off, turn_off_interval_sec*hz);
}

/**
 * cada ~2s si hay carga o alguien quiere monopolizar
 * chequeo si hay core suspendido y prendo de ser necesario
*/
static void 
timer_callback_turn_on(void *arg) 
{
    
    if ((stats_score >= LOAD_NORMAL) || (cpus_requested > 0)) {
        check = 0;

        int turned_off_cpu = get_off_cpu();

        if (turned_off_cpu > 0) {
            turn_on_cpu(turned_off_cpu);
            turned_off_cpus[turned_off_cpu] = false;

            log(LOG_INFO | LOG_LOCAL2, "CPU %d turned on\n", turned_off_cpu);
        }
    }

    callout_schedule(&timer_turn_on, turn_on_interval_sec*hz);
}

/**
 * escala para normalizar
 * puntajes de cargas
*/
int 
calc_load_score(int score)
{
    if (score < 30)
        return LOAD_IDLE;
    else if (score < 45)
        return LOAD_LOW;
    else if (score < 75)
        return LOAD_NORMAL;
    else if (score < 85)
        return LOAD_HIGH;
    else if (score < 98)
        return LOAD_INTENSE;
    else 
        return LOAD_SEVERE;
}

int 
compareProcesses(const void *a, const void *b) 
{
    return ((const struct cpu_monopolization *)b)->prio - ((const struct cpu_monopolization *)a)->prio;
}

int
get_idlest_cpu(void)
{
    int  cpus_idle_pct[CPU_NUMBER];//idle_ticks*100/delta_ticks
    struct cpu_core_stats *cpu_stats;

    cpu_stats = (struct cpu_core_stats *)obtain_stats("cpu_stats", CPU_NUMBER * sizeof(struct cpu_core_stats));
    
    if (cpu_stats == NULL) 
        return -1;

    for (int i = 0; i < CPU_NUMBER; i++) {
        if (cpu_stats[i].delta_total <= 0)
            return -1;

        cpus_idle_pct[i] = (cpu_stats[i].delta[CP_IDLE]*100) / cpu_stats[i].delta_total;
    }

    free(cpu_stats, M_DEVBUF);

    int max_idle = cpus_idle_pct[1]; //cpu 0 cant be turned off
    int idlest_cpu_index = 1;
    for (int i = 2; i < CPU_NUMBER; i++) {
        if (cpus_idle_pct[i] > max_idle) {
            idlest_cpu_index = i;
            max_idle = cpus_idle_pct[i];
        }
    }
    
    if (max_idle == 0) //todas apagadas
        return -1;
    else
        return idlest_cpu_index;
}

int
get_monopolization_info(int *pids_sorted_by_prio)
{
    struct cpu_monopolization *mono_info;
    int n_requested = 0;

    mono_info = (struct cpu_monopolization *)obtain_stats("monopolization_info", sizeof(struct cpu_monopolization) * CPU_NUMBER);

    //definir un limite de solicitados 
    //si me paso de ese limite, busco los de mayor prioridad
    for (int i = 1; i < CPU_NUMBER; i++) {
        if (mono_info[i].requested)
            n_requested++;
    }

    //mono_info[0] -> highest priority process
    qsort(mono_info, CPU_NUMBER, sizeof(struct cpu_monopolization), compareProcesses);

    for (int i = 0; i < CPU_NUMBER; i++) {
        pids_sorted_by_prio[i] = mono_info[i].pid;
    }
    
    free(mono_info, M_DEVBUF);

    return n_requested;
}

int
get_n_turned_off(void)
{
    int cpus_off = 0;

    for (int i = 0; i < CPU_NUMBER; i++)
        if (turned_off_cpus[i])  
            cpus_off++;
    
    return cpus_off;
}

int
get_off_cpu(void)
{

    for (int i = 0; i < CPU_NUMBER; i++) { //start in i = 1?
        if (turned_off_cpus[i] == true) 
            return i;
    }

    return -1;
}

int
get_procs_needs(void)
{
    int  n_procs_per_need_type[N_PROC_NEEDS];//num of processes classified by cpu usage need
    struct threads_stats *threads_stats;

    threads_stats = (struct threads_stats *)obtain_stats("threads_stats", sizeof(struct threads_stats));
    
    if (threads_stats == NULL) 
        return LOAD_NORMAL; //exception, discard measure

    for (int i = 0; i < N_PROC_NEEDS; i++) 
        n_procs_per_need_type[i] = threads_stats->proc_needs[i];

    free(threads_stats, M_DEVBUF);

    return processes_need_score(n_procs_per_need_type);
}

/*
 * segun el uso de cpu por procesos
 * de usuario en el ultimo delta de tiempo
 * obtengo un puntaje entre 0 y 100 y luego un puntaje normalizado
*/
int
get_user_load(void)
{
    int  cpus_user_pct_load[CPU_NUMBER];//user_ticks*100/delta_ticks
    struct cpu_core_stats *cpu_stats;

    cpu_stats = (struct cpu_core_stats *)obtain_stats("cpu_stats", CPU_NUMBER * sizeof(struct cpu_core_stats));
    
    if (cpu_stats == NULL) 
        return LOAD_NORMAL; //exception, discard measure

    for (int i = 0; i < CPU_NUMBER; i++) {
        //if delta_total is 0 or less, the pct used is 0, if not calculate it
        cpus_user_pct_load[i] = cpu_stats[i].delta_total <= 0 ? 0 : (cpu_stats[i].delta[CP_USER]*100) / cpu_stats[i].delta_total;
    }

    free(cpu_stats, M_DEVBUF);

    int system_load = 0, on_cpus = CPU_NUMBER;
    for (int i = 0; i < CPU_NUMBER; i++) {
        system_load += calc_load_score(cpus_user_pct_load[i]);
        if (turned_off_cpus[i] == true)  
            on_cpus--;
    }

    return imin((system_load / on_cpus), LOAD_SEVERE); 
}

/* 
 * posibilidad de usar en lugar de get_user_load
 * https://forums.freebsd.org/threads/load-average-calculation.72066/ 
 */
int
get_sys_load(void)
{
    int *sys_load;

    sys_load = (int *)obtain_stats("cpu_load_last_min", sizeof(int));

    int sys_load_pct = (*sys_load / CPU_NUMBER);

    free(sys_load, M_DEVBUF);

    return calc_load_score(sys_load_pct);
}

void *
obtain_stats(const char *sysctl_name, size_t ptr_size) 
{
    char sysctl_node[MAX_STRING_LENGTH];

    void *ptr = init_pointer(ptr_size);
 
    snprintf(sysctl_node, sizeof(sysctl_node), "kern.%s", sysctl_name);
    
    if (kernel_sysctlbyname(curthread, sysctl_node, ptr, &ptr_size, NULL, 0, NULL, 0) == -1) {
        free(ptr, M_DEVBUF);
        return NULL;
    }

    return ptr;
}

/*
 * segun la clasificacion de los procesos
 * por requerimientos de cpu, obtengo un 
 * puntaje entre 0 y 100 y luego un puntaje normalizado
*/
int
processes_need_score(int *procs_per_need_type)
{
    const int MAX_SCORE_BY_TYPE[N_PROC_NEEDS] = { 30, 50, 94, 100 };

    int total_procs = procs_per_need_type[PROC_LOWPERF]  + 
                      procs_per_need_type[PROC_STANDARD] +
                      procs_per_need_type[PROC_HIGHPERF] + 
                      procs_per_need_type[PROC_CRITICAL] + 
                      1; //idle process

    if (total_procs <= 0)
        return -1;

    int score = 0;

    int lowperf_pct   = (procs_per_need_type[PROC_LOWPERF] * 100)  / total_procs;
    int standard_pct  = (procs_per_need_type[PROC_STANDARD] * 100) / total_procs;
    int highperf_pct  = (procs_per_need_type[PROC_HIGHPERF] * 100) / total_procs;
    int critical_pct  = (procs_per_need_type[PROC_CRITICAL] * 100) / total_procs;

    int score_lowperf  = lowperf_pct  * MAX_SCORE_BY_TYPE[PROC_LOWPERF];
    int score_standard = standard_pct * MAX_SCORE_BY_TYPE[PROC_STANDARD];
    int score_highperf = highperf_pct * MAX_SCORE_BY_TYPE[PROC_HIGHPERF];
    int score_critical = critical_pct * MAX_SCORE_BY_TYPE[PROC_CRITICAL];

    score = (score_lowperf + score_standard + score_highperf + score_critical) / 100;

    return calc_load_score(score);
}

static moduledata_t toggle_active_cpu_data = {
    "toggle_active_cpu",
    event_handler,
    NULL
};

DECLARE_MODULE(toggle_active_cpu, toggle_active_cpu_data, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(toggle_active_cpu, 1);
MODULE_DEPEND(toggle_active_cpu, cpu_stats, 2, 2, 2);
MODULE_DEPEND(toggle_active_cpu, thread_stats, 2, 2, 2);