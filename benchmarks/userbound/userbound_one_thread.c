// userbound_one_thread.c
// CORRER EN FreeBSD: cc -O2 -pthread userbound_one_thread.c -o userbound_one_thread
//
//  sysctl que "bindea al thread llamador": kern.sched.userbindme = <cpu>
// (internamente: setea TDF_USERBOUND y llama sched_bind(curthread,cpu))
//
// Ejecuta 2 threads:
//  - T1: se vuelve userbound al CPU_X y hace busy-loop (progresa siempre).
//  - T2: NO userbound (opcional: affinity solo a CPU_X) y hace yield para forzar reencolado.
//  tirar sysctl de "reservar CPU_X"  desde otra terminal.

/*
./userbound_one_thread 2 X
        -> CPU_X = 2                 T1 queda bound al CPU2 (por sysctl kern.sched.userbindme=2).
        -> PIN_UNBOUND_TO_CPU_X = 1

        2 1: forzás que ambos threads solo puedan correr en CPU2 (uno bound, el otro solo affinity). 
             Sirve para ver competencia directa y luego cómo “reserva” expulsa/bloquea al no bound.
        2 0: solo el thread crítico (T1) queda clavado a CPU2; el otro queda libre. Sirve para ver el 
             caso “realista” donde el sistema puede mover al no bound a otros cores.
*/

#define _GNU_SOURCE
#include <pthread.h>
#include <pthread_np.h>
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
static const char *SYSCTL_USERBINDME = "kern.sched.userbindme";

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

static void
print_tid_and_mask(const char *thread_name)
{
    int tid = get_tid();
    cpuset_t mask;
    CPU_ZERO(&mask);

    if (cpuset_getaffinity(CPU_LEVEL_WHICH, CPU_WHICH_TID, tid, sizeof(mask), &mask) == 0) {
        printf("[INFO] %s -> TID: %d | cpuset mask: ", thread_name, tid);
        for (int i = 0; i < CPU_SETSIZE; i++) {
            if (CPU_ISSET(i, &mask)) {
                printf("%d ", i);
            }
        }
        printf("\n");
    } else {
        perror("cpuset_getaffinity failed in print_tid_and_mask");
    }
    fflush(stdout);
}

static void *
t1_userbound(void *arg)
{
    pthread_set_name_np(pthread_self(), "T1_USERBOUND");

    (void)arg;

    userbindme_cpuX();

    print_tid_and_mask("T1_USERBOUND");

    for (;;) {
        c_userbound++;
        if ((c_userbound & ((1u<<20)-1)) == 0)
            sched_yield(); 
    }
    return NULL;
}

static void *
t2_unbound(void *arg)
{
    pthread_set_name_np(pthread_self(), "T2_UNBOUND");

    (void)arg;

    if (PIN_UNBOUND_TO_CPU_X)
        set_affinity_only_cpuX();

    print_tid_and_mask("T2_UNBOUND");

    for (;;) {
        c_unbound++;
        sched_yield(); 
    }
    return NULL;
}

int
main(int argc, char **argv)
{
    pthread_set_name_np(pthread_self(), "MAIN_MONITOR");

    if (argc >= 2)
        CPU_X = atoi(argv[1]);
    if (argc >= 3)
        PIN_UNBOUND_TO_CPU_X = atoi(argv[2]); 

    printf("CPU_X=%d\n", CPU_X);
    printf("SYSCTL_USERBINDME=%s\n", SYSCTL_USERBINDME);
    printf("PIN_UNBOUND_TO_CPU_X=%d (affinity solo a CPU_X para el hilo NO userbound)\n",
           PIN_UNBOUND_TO_CPU_X);
    printf("\n OBJ!: en otra terminal EL sysctl para reservar CPU_X\n\n");
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