#ifndef MODLIB_H
#define MODLIB_H

#include <sys/param.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/sched_petri.h>
#include <sys/syslog.h>
#include <sys/callout.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/resource.h>

#define MAX_STRING_LENGTH       64
#define MAX_CPU_STRING_LENGTH   MAX_STRING_LENGTH*CPUSTATES
#define MAX_BUFFER_LENGTH       MAX_CPU_STRING_LENGTH*4

#define LOAD_SEVERE     5
#define LOAD_INTENSE    4
#define LOAD_HIGH       3
#define LOAD_NORMAL     2
#define LOAD_LOW        1
#define LOAD_IDLE       0

#define PROC_CRITICAL   3
#define PROC_HIGHPERF   2
#define PROC_STANDARD   1
#define PROC_LOWPERF    0
#define N_PROC_NEEDS    4

#define N_CPU_STATS     3
#define N_THREADS_STATS 4

struct cpu_monopolization
{
    bool requested;
    int prio;
    int pid;
};

struct cpu_core_stats
{
    long ticks[CPUSTATES];
    long delta[CPUSTATES];
    long ticks_total;
    long delta_total;
};

struct threads_stats
{
    int estcpu_idle;
    int estcpu_work;
    int threads_in_system;
    int threads_active;
    int procs_in_system;
    int procs_active;
    int proc_needs[N_PROC_NEEDS];
};

int  obtain_random_freq(void);
void init_timer(struct callout *timer, int freq, void (*timer_callback)(void *), void *arg);

#endif