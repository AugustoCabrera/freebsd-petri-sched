// churn_worker_pool.c  (simple + modo bloqueante instrumentado)
// FreeBSD: cc -O2 -pthread churn_worker_pool.c -o churn_worker_pool
//
// Uso:
//   ./churn_worker_pool [nthreads] [seconds] [phase_ms] [--block]
// Ej:
//   ./churn_worker_pool 32 15 250
//   ./churn_worker_pool 32 15 250 --block

// actualizar: rsync -avz -e ssh benchmarks/churn_pool/churn_worker_pool.c \
  root@192.168.1.32:/usr/local/src/benchmarks/churn_pool/

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
static atomic_ullong g_trylock_fail = 0;   // solo para modo trylock+yield
static atomic_ullong g_tasks_done = 0;
static atomic_ullong g_lock_blocks = 0;  //cuantas veces el hilo detectó lock ocupado y tuvo que BLOQUEAR (dormirr)
static int g_blocking_lock = 0;           // 0 = trylock + yield (activo), 1 = bloqueante instrumentado

static inline uint64_t now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}  //devuelve el tiempo actual del reloj en nanosegundo

static void* worker(void* arg) {
  (void)arg;

  while (!atomic_load(&g_stop)) {
    int ph = atomic_load(&g_phase);

    /*
    El hilo sigue ejecutándose mientras g_stop sea 0. atomic_load lee esa variable atómica de forma segura
     entre múltiples threads (exclusion mutua). Cuando el thread main pone g_stop = 1, este loop termina
    */

    if (ph == 0) {
      // Lee la fase actual del benchmark -> NO_WORK: cola vacía -> churn puro: runnable + yield
      atomic_fetch_add(&g_yields, 1);
      sched_yield();
      continue;
    }

    // SEQ_CONTEND: hay trabajo, pero serializado por lock global
    long items = atomic_load(&g_work_items);
    if (items <= 0) {
      atomic_fetch_add(&g_yields, 1);
      sched_yield();
      continue;
    }

    if (!g_blocking_lock) {
      // Modo activo:no bloquea, genera churn por contencion
      if (pthread_mutex_trylock(&g_global_lock) != 0) {
        atomic_fetch_add(&g_trylock_fail, 1);
        atomic_fetch_add(&g_yields, 1);
        sched_yield();
        continue;
      }
      // si entra, ya tiene el lock
    } else {
      // Modo bloqueante instrumentado:
      // intentamos trylock solo para detectar si el lock está ocupado.
      if (pthread_mutex_trylock(&g_global_lock) != 0) {
        // lock ocupado => este hilo tendrá que dormir en mutex_lock()
        atomic_fetch_add(&g_lock_blocks, 1);
        pthread_mutex_lock(&g_global_lock);
      }
      // si trylock funciono, ya tenemos el lock y NO bloqueamos
    }

    // Dentro del lock: consumir 1 item si había
    if (atomic_fetch_sub(&g_work_items, 1) > 0) {
      for (volatile int i = 0; i < 200; i++) { }
      atomic_fetch_add(&g_tasks_done, 1);
    } else {
      atomic_fetch_add(&g_work_items, 1);
    }

    pthread_mutex_unlock(&g_global_lock);
  }

  return NULL;
}

static void usage(const char* prog) {
  printf("Uso:\n");
  printf("  %s [nthreads] [seconds] [phase_ms] [--block]\n", prog);
  printf("Ej:\n");
  printf("  %s 32 15 250\n", prog);
  printf("  %s 32 15 250 --block\n", prog);
}

int main(int argc, char** argv) {
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--block") == 0) g_blocking_lock = 1;
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      usage(argv[0]);
      return 0;
    }
  }

  if (argc >= 2) g_nthreads = atoi(argv[1]);
  if (argc >= 3) g_seconds  = atoi(argv[2]);
  if (argc >= 4) g_phase_ms = atoi(argv[3]);

  if (g_nthreads <= 0) g_nthreads = 32;
  if (g_seconds  <= 0) g_seconds  = 15;
  if (g_phase_ms <= 0) g_phase_ms = 250;

  printf("churn: nthreads=%d seconds=%d phase_ms=%d lock_mode=%s\n",
         g_nthreads, g_seconds, g_phase_ms,
         g_blocking_lock ? "BLOCKING(instrumented)" : "TRYLOCK+YIELD");
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

  unsigned long long last_y = 0, last_f = 0, last_d = 0, last_b = 0;

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
      unsigned long long b = (unsigned long long)atomic_load(&g_lock_blocks);

      printf("[stats] yields/s=%llu trylock_fail/s=%llu lock_blocks/s=%llu tasks/s=%llu "
             "(tot y=%llu f=%llu b=%llu d=%llu)\n",
             y - last_y, f - last_f, b - last_b, d - last_d,
             y, f, b, d);

      last_y = y; last_f = f; last_b = b; last_d = d;
      fflush(stdout);
    }
  }

  for (int i = 0; i < g_nthreads; i++) pthread_join(ths[i], NULL);

  printf("done.\n");
  printf("final: yields=%llu trylock_fail=%llu lock_blocks=%llu tasks=%llu\n",
         (unsigned long long)atomic_load(&g_yields),
         (unsigned long long)atomic_load(&g_trylock_fail),
         (unsigned long long)atomic_load(&g_lock_blocks),
         (unsigned long long)atomic_load(&g_tasks_done));

  free(ths);
  return 0;
}