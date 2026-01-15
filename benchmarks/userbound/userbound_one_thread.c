// userbound_one_thread.c
// FreeBSD: cc -O2 -pthread userbound_one_thread.c -o userbound_one_thread
//
// Requiere que exista un sysctl que "bindea al thread llamador":
//   kern.sched.userbindme = <cpu>
// (internamente: setea TDF_USERBOUND y llama sched_bind(curthread,cpu))
//
// Ejecuta 2 threads:
//  - T1: se vuelve userbound al CPU_X y hace busy-loop (progresa siempre).
//  - T2: NO userbound (opcional: affinity solo a CPU_X) y hace yield para forzar re-encolado.
//  tirar tu sysctl de "reservar CPU_X" cuando quieras desde otra terminal.

#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <sys/types.h>
#include <sys/sysctl.h>

#include <sys/param.h>
#include <sys/cpuset.h>
#include <sys/thr.h>

static int CPU_X = 3;

/* Cambiá esto al nombre real que implementaste */
static const char *SYSCTL_USERBINDME = "kern.sched.userbindme";

/* Si querés que el hilo no-userbound quede "pegado" al CPU_X via affinity */
static int PIN_UNBOUND_TO_CPU_X = 1;

static volatile uint64_t c_userbound = 0;
static volatile uint64_t c_unbound   = 0;

static int
get_tid(void)
{
    long tid;
    if (thr_self(&tid) != 0)
        return -1;
    return (int)tid;
}

static void
set_affinity_only_cpuX(void)
{
    cpuset_t mask;
    CPU_ZERO(&mask);
    CPU_SET(CPU_X, &mask);

    int tid = get_tid();
    if (tid < 0) {
        perror("thr_self");
        exit(1);
    }

    if (cpuset_setaffinity(CPU_LEVEL_WHICH, CPU_WHICH_TID, tid,
                           sizeof(mask), &mask) != 0) {
        perror("cpuset_setaffinity(CPU_WHICH_TID)");
        exit(1);
    }
}

static void
userbindme_cpuX(void)
{
    int cpu = CPU_X;

    if (sysctlbyname(SYSCTL_USERBINDME, NULL, NULL, &cpu, sizeof(cpu)) != 0) {
        fprintf(stderr, "sysctlbyname(%s) failed: %s\n",
                SYSCTL_USERBINDME, strerror(errno));
        exit(1);
    }
}

static void *
t1_userbound(void *arg)
{
    (void)arg;

    /* Este hilo se vuelve userbound + bound real al CPU_X */
    userbindme_cpuX();

    for (;;) {
        c_userbound++;
        if ((c_userbound & ((1u<<20)-1)) == 0)
            sched_yield(); /* fuerza re-encolado y evidencia del scheduler */
    }
    return NULL;
}

static void *
t2_unbound(void *arg)
{
    (void)arg;

    if (PIN_UNBOUND_TO_CPU_X)
        set_affinity_only_cpuX();  /* NO es bound, solo restringe dónde puede correr */

    for (;;) {
        c_unbound++;
        sched_yield(); /* para que pase por sched_add frecuentemente */
    }
    return NULL;
}

int
main(int argc, char **argv)
{
    if (argc >= 2)
        CPU_X = atoi(argv[1]);
    if (argc >= 3)
        PIN_UNBOUND_TO_CPU_X = atoi(argv[2]); /* 1 o 0 */

    printf("CPU_X=%d\n", CPU_X);
    printf("SYSCTL_USERBINDME=%s\n", SYSCTL_USERBINDME);
    printf("PIN_UNBOUND_TO_CPU_X=%d (affinity solo a CPU_X para el hilo NO userbound)\n",
           PIN_UNBOUND_TO_CPU_X);
    printf("\nTip: en otra terminal tirá tu sysctl para reservar CPU_X cuando quieras.\n\n");
    fflush(stdout);

    pthread_t a, b;
    if (pthread_create(&a, NULL, t1_userbound, NULL) != 0) {
        perror("pthread_create(t1_userbound)");
        return 1;
    }
    if (pthread_create(&b, NULL, t2_unbound, NULL) != 0) {
        perror("pthread_create(t2_unbound)");
        return 1;
    }

    /* Monitor simple */
    for (;;) {
        uint64_t u1 = c_userbound, n1 = c_unbound;
        sleep(1);
        uint64_t u2 = c_userbound, n2 = c_unbound;
        printf("delta/sec: userbound=%llu  unbound=%llu\n",
               (unsigned long long)(u2 - u1),
               (unsigned long long)(n2 - n1));
        fflush(stdout);
    }
}
