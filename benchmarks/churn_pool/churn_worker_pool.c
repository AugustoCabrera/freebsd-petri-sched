// churn_worker_pool_simple.c
// FreeBSD: cc -O2 -pthread churn_worker_pool_simple.c -o churn_simple
//
// Fases alternadas por el main:
//  phase=0 (NO_WORK): work_items=0 -> todos hacen sched_yield() (RUNNABLE churn)
//  phase=1 (SEQ_CONTEND): work_items>0 + trylock(lock_global) -> 1 progresa, resto yield
//
// Uso:
//   ./churn_simple [nthreads] [seconds] [phase_ms]
//   ej: ./churn_simple 32 15 250

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static int g_nthreads = 32;
static int g_seconds  = 15;
static int g_phase_ms = 250;

static atomic_int  g_stop  = 0;
static atomic_int  g_phase = 0;        // 0=NO_WORK, 1=SEQ_CONTEND
static atomic_long g_work_items = 0;

static pthread_mutex_t g_global_lock = PTHREAD_MUTEX_INITIALIZER;

static atomic_ullong g_yields = 0;
static atomic_ullong g_trylock_fail = 0;
static atomic_ullong g_tasks_done = 0;

static inline uint64_t now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void* worker(void* arg) {
  (void)arg;

  while (!atomic_load(&g_stop)) {
    int ph = atomic_load(&g_phase);

    if (ph == 0) {      
      // NO_WORK: cola vacía -> churn puro: runnable + yield
      atomic_fetch_add(&g_yields, 1);
      sched_yield();
      continue;
    }

    // SEQ_CONTEND: hay trabajo, pero solo progresa el que toma el lock global
    long items = atomic_load(&g_work_items);
    if (items <= 0) {
      atomic_fetch_add(&g_yields, 1);       // cuenta cuántas veces un worker hace “churn” (yield).
      sched_yield();                        // El thread cede voluntariamente el CPU.
      continue;     //Salta al comienzo del while: en NO_WORK el worker hace yield una y otra vez.
    }

    if (pthread_mutex_trylock(&g_global_lock) != 0) {
        //Intenta tomar el lock global SIN bloquearse. Si no lo consigue (porque otro worker lo tiene), entra al if
      atomic_fetch_add(&g_trylock_fail, 1);
      atomic_fetch_add(&g_yields, 1);
      sched_yield();
      continue;
    }

    // Dentro del lock: consumir 1 item si había
    if (atomic_fetch_sub(&g_work_items, 1) > 0) {
      // trabajo breve (mantener corto para que se note el overhead)
      for (volatile int i = 0; i < 200; i++) { }
      atomic_fetch_add(&g_tasks_done, 1);
    } else {
      // compensar si se pasó de 0
      atomic_fetch_add(&g_work_items, 1);
    }

    pthread_mutex_unlock(&g_global_lock);   //suelta mutex
  }

  return NULL;
}

int main(int argc, char** argv) {
  if (argc >= 2) g_nthreads = atoi(argv[1]);
  if (argc >= 3) g_seconds  = atoi(argv[2]);
  if (argc >= 4) g_phase_ms = atoi(argv[3]);

  if (g_nthreads <= 0) g_nthreads = 32;
  if (g_seconds  <= 0) g_seconds  = 15;
  if (g_phase_ms <= 0) g_phase_ms = 250;

  printf("churn_simple: nthreads=%d seconds=%d phase_ms=%d\n",
         g_nthreads, g_seconds, g_phase_ms);
  printf("run: vmstat -w 1   | top -SH   | procstat -t <pid>\n");

  pthread_t* ths = calloc((size_t)g_nthreads, sizeof(*ths));
  if (!ths) { perror("calloc"); return 1; }

  for (int i = 0; i < g_nthreads; i++) {
    if (pthread_create(&ths[i], NULL, worker, NULL) != 0) {
      perror("pthread_create");
      return 1;
    }
  }

  uint64_t start = now_ns();
  uint64_t next_report = start + 1000000000ull;

  unsigned long long last_y = 0, last_f = 0, last_d = 0;

  while (!atomic_load(&g_stop)) {
    uint64_t t = now_ns();
    if ((t - start) / 1000000000ull >= (uint64_t)g_seconds) {
      atomic_store(&g_stop, 1);
      break;
    }

    // Alterna fases
    atomic_store(&g_phase, 0);          // NO_WORK
    atomic_store(&g_work_items, 0);
    usleep((useconds_t)g_phase_ms * 1000);

    atomic_store(&g_phase, 1);          // SEQ_CONTEND
    atomic_store(&g_work_items, (long)g_nthreads * 2);
    usleep((useconds_t)g_phase_ms * 1000);

    // Report ~1s
    t = now_ns();
    if (t >= next_report) {
      next_report += 1000000000ull;

      unsigned long long y = (unsigned long long)atomic_load(&g_yields);
      unsigned long long f = (unsigned long long)atomic_load(&g_trylock_fail);
      unsigned long long d = (unsigned long long)atomic_load(&g_tasks_done);

      printf("[stats] yields/s=%llu trylock_fail/s=%llu tasks/s=%llu (tot y=%llu f=%llu d=%llu)\n",
             y - last_y, f - last_f, d - last_d, y, f, d);

      last_y = y; last_f = f; last_d = d;
      fflush(stdout);
    }
  }

  for (int i = 0; i < g_nthreads; i++) pthread_join(ths[i], NULL);

  printf("done.\n");
  printf("final: yields=%llu trylock_fail=%llu tasks=%llu\n",
         (unsigned long long)atomic_load(&g_yields),
         (unsigned long long)atomic_load(&g_trylock_fail),
         (unsigned long long)atomic_load(&g_tasks_done));

  free(ths);
  return 0;
}