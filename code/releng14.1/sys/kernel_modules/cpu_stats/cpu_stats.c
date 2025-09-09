#include "../include/modlib.h"

#include <sys/pcpu.h>
#include <sys/smp.h>

struct cpu_core_stats *cpu_stats = NULL;
//int cpu_load_last_min;

/* funcion que permite publicar una struct de valor dinamico para acceder mediante sysctl */
static int sysctl_get_cpu_stats(SYSCTL_HANDLER_ARGS) {
    if (req->newptr == NULL) 
        return sysctl_handle_opaque(oidp, cpu_stats, CPU_NUMBER * sizeof(struct cpu_core_stats), req);

    return EPERM;
}

SYSCTL_PROC(_kern, OID_AUTO, cpu_stats, CTLTYPE_OPAQUE | CTLFLAG_RD | CTLFLAG_MPSAFE,
    0, 0, sysctl_get_cpu_stats, "A", "proc to share cpu stats structs");

//SYSCTL_INT(_kern_sched_stats, OID_AUTO, cpu_load_last_min, CTLFLAG_RD,
//            &cpu_load_last_min, 0, "loadavg of sys in last minute");

void        calc_cpu_stats(int cpu, long *cp_time);
void        log_cpu_stats(int cpu);
void        read_cpu_stats(void);
static void timer_callback(void *arg);

static struct callout timer;

// this array of strings must follow the same order as the CPU execution modes defined in sys/resources.h
const char *CPU_MODES[CPUSTATES] = { "USER", "NICE", "SYS", "INTR", "IDLE" };

static int event_handler(struct module *module, int event, void *arg) {
    int e = 0; /* Error, 0 for normal return status */
    switch (event)
    {
        case MOD_LOAD:
            //hz/2 is equivalent to sample twice every second
            init_timer(&timer, hz/2, &timer_callback, NULL);

            cpu_stats = (struct cpu_core_stats *)init_pointer(CPU_NUMBER * sizeof(struct cpu_core_stats));

            log(LOG_INFO | LOG_LOCAL1, "loading cpu_stats\n");
            break;

        case MOD_UNLOAD:
            callout_drain(&timer);

            free(cpu_stats, M_DEVBUF);

            log(LOG_INFO | LOG_LOCAL1, "cpu_stats unloaded\n");
            break;

        default:
            e = EOPNOTSUPP; /* Error, Operation Not Supported */
            break;
    }

    return (e);
}

/**
 * TODO:
 * -    ver cuantos hilos en su cola?
 *      -   si son prioritarios, kernel, user algo asi?
 *      -	con esto?
 *          -   struct runq	*ts_runq;
 *          -   ts->ts_runq = &runq_pcpu[cpu];
 * -    testear sched_petri vs sched_4bsd original
*/
void
read_cpu_stats(void)
{
    int cpu;
    static int log_count = 0;

    if (log_count >= 9)
        log(LOG_INFO | LOG_LOCAL1, "****************** READ_CPU_STATS *****************\n");

    CPU_FOREACH(cpu) {
        struct pcpu *pcpu = pcpu_find(cpu);

        calc_cpu_stats(cpu, pcpu->pc_cp_time);
    
        if (log_count >= 9)
            log_cpu_stats(cpu);
    }

    if (log_count++ >= 9)
        log_count = 0;

    //permite calcular la carga usando loadavg. ldavg[0] es la carga en el ultimo minuto.
    //cpu_load_last_min = (averunnable.ldavg[0]* 100 + FSCALE / 2) >> FSHIFT;
}

/*
 * leo estadisticas de los cpus del sistema
 * y los guardo en una struct calculando 
 * tambien el delta entre la ultima medicion y la actual
*/
void
calc_cpu_stats(int cpu, long *cp_time)
{
    long delta_total = 0;
    long ticks_total = 0;

    for (int i = 0; i < CPUSTATES; i++) {
        cpu_stats[cpu].delta[i] = cp_time[i] - cpu_stats[cpu].ticks[i];
        cpu_stats[cpu].ticks[i] = cp_time[i];

        ticks_total += cpu_stats[cpu].ticks[i];
        delta_total += cpu_stats[cpu].delta[i];
    }

    cpu_stats[cpu].ticks_total = ticks_total; //asi para que nunca se ponga en 0 mientras otro lee, sino actuando 
    cpu_stats[cpu].delta_total = delta_total; //solo sobre cpu_stats deberia ponerlo en 0 al principio y podria ser problema
}

void 
log_cpu_stats(int cpu) 
{
    char delta_str[MAX_CPU_STRING_LENGTH];
    char ticks_str[MAX_CPU_STRING_LENGTH];
    
    struct cpu_core_stats core_stats = cpu_stats[cpu];
    int  delta_offset = 0;
    int  ticks_offset = 0;

    for (int i = 0; i < CPUSTATES; i++) {
        ticks_offset += snprintf(ticks_str + ticks_offset, sizeof(ticks_str) - ticks_offset, "CPU_%d_ticks_%s: %ld\n", cpu, CPU_MODES[i], core_stats.ticks[i]);
        delta_offset += snprintf(delta_str + delta_offset, sizeof(delta_str) - delta_offset, "CPU_%d_delta_%s: %ld\n", cpu, CPU_MODES[i], core_stats.delta[i]);
    }

    snprintf(ticks_str + ticks_offset, sizeof(ticks_str) - ticks_offset, "CPU_%d_ticks_TOTAL: %ld\n", cpu, core_stats.ticks_total);
    snprintf(delta_str + delta_offset, sizeof(delta_str) - delta_offset, "CPU_%d_delta_TOTAL: %ld\n", cpu, core_stats.delta_total);

    log(LOG_INFO | LOG_LOCAL1, "%s\n%s\n---------------------------\n\n", ticks_str, delta_str);
}

static void
timer_callback(void *arg)
{
    
    read_cpu_stats();
    callout_reset(&timer, obtain_random_freq(), timer_callback, NULL);
}

static moduledata_t cpu_stats_data = {
    "cpu_stats",
    event_handler,
    NULL
};

DECLARE_MODULE(cpu_stats, cpu_stats_data, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(cpu_stats, 2);