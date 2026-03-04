// churn_worker_pool.c
// FreeBSD: cc -O2 -pthread churn_worker_pool.c -o churn_worker_pool
//
// Ejecuta un worker pool con fases:
//  (1) NO_WORK: wakeups espurios -> cola vacía -> yield loop (RUNNABLE sin progreso)
//  (2) SEQ_CONTEND: "trabajo" + lock global con trylock -> si falla -> yield (RUNNABLE + churn)
//
// Observación sugerida:
//  - vmstat -w 1          (mirar "cs" context switches)
//  - top -SH              (ver threads del proceso, estado/CPU)
//  - procstat -t <pid>    (ver estado de threads)
//
// Uso:
//  ./churn_worker_pool [nthreads] [seconds] [phase_ms]
//  ej: ./churn_worker_pool 32 20 250

#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

typedef enum {
  PH_NO_WORK = 0,
  PH_SEQ_CONTEND = 1
} phase_t;

static int g_nthreads = 32;
static int g_seconds  = 20;
static int g_phase_ms = 250;

static atomic_int g_stop = 0;

// "Cola" simplificada: solo un contador de items disponibles.
static atomic_long g_work_items = 0;

// Fase global controlada por manager.
static atomic_int g_phase = PH_NO_WORK;

// Para sincronizar “pulsos” (despertar a todos).
static pthread_mutex_t g_pulse_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_pulse_cv  = PTHREAD_COND_INITIALIZER;
static atomic_ulong    g_pulse_id  = 0;

// Lock global que simula recurso secuencial (trylock para mantener RUNNABLE).
static pthread_mutex_t g_global_lock = PTHREAD_MUTEX_INITIALIZER;

// Contadores de churn/actividad
static atomic_ullong g_yields = 0;
static atomic_ullong g_trylock_fail = 0;
static atomic_ullong g_tasks_done = 0;

static inline uint64_t
now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void
pulse_all(void) {
  pthread_mutex_lock(&g_pulse_mtx);
  atomic_fetch_add(&g_pulse_id, 1);
  pthread_cond_broadcast(&g_pulse_cv);
  pthread_mutex_unlock(&g_pulse_mtx);
}

static void*
worker_main(void *arg) {
  (void)arg;

  unsigned long last_pulse = atomic_load(&g_pulse_id);

  while (!atomic_load(&g_stop)) {
    // Espera a que el manager dispare un nuevo "pulso"
    pthread_mutex_lock(&g_pulse_mtx);
    while (!atomic_load(&g_stop) && atomic_load(&g_pulse_id) == last_pulse) {
      pthread_cond_wait(&g_pulse_cv, &g_pulse_mtx);
    }
    last_pulse = atomic_load(&g_pulse_id);
    pthread_mutex_unlock(&g_pulse_mtx);

    if (atomic_load(&g_stop)) break;

    int ph = atomic_load(&g_phase);

    // Cada pulso dura ~g_phase_ms, el worker se comporta “mal” en esa ventana.
    uint64_t end = now_ns() + (uint64_t)g_phase_ms * 1000000ull;

    if (ph == PH_NO_WORK) {
      // Cola vacía (o casi): RUNNABLE pero sin trabajo útil -> yield loop.
      while (!atomic_load(&g_stop) && now_ns() < end) {
        if (atomic_load(&g_work_items) <= 0) {
          atomic_fetch_add(&g_yields, 1);
          sched_yield();
        } else {
          // Si justo hay algo, lo consume (para que no se “trabe” el demo)
          atomic_fetch_sub(&g_work_items, 1);
          atomic_fetch_add(&g_tasks_done, 1);
        }
      }
    } else {
      // PH_SEQ_CONTEND: hay trabajo, pero se serializa por lock global.
      while (!atomic_load(&g_stop) && now_ns() < end) {
        long items = atomic_load(&g_work_items);
        if (items <= 0) {
          // Sin items: de nuevo churn (simula wakeups sin trabajo real)
          atomic_fetch_add(&g_yields, 1);
          sched_yield();
          continue;
        }

        // Intento de avanzar: tomar lock global (sección secuencial)
        if (pthread_mutex_trylock(&g_global_lock) != 0) {
          // No se bloquea -> sigue RUNNABLE -> churn de contención
          atomic_fetch_add(&g_trylock_fail, 1);
          atomic_fetch_add(&g_yields, 1);
          sched_yield();
          continue;
        }

        // En lock: consume 1 item y hace trabajo MUY corto (para maximizar churn).
        if (atomic_fetch_sub(&g_work_items, 1) > 0) {
          // trabajo breve
          // (si lo hacés más largo, baja el churn y se vuelve más “útil”)
          for (volatile int i = 0; i < 200; i++) { }
          atomic_fetch_add(&g_tasks_done, 1);
        } else {
          // se pasó de 0
          atomic_fetch_add(&g_work_items, 1);
        }

        pthread_mutex_unlock(&g_global_lock);
      }
    }
  }

  return NULL;
}

static void*
manager_main(void *arg) {
  (void)arg;

  uint64_t start = now_ns();
  uint64_t next_report = start + 1000000000ull;

  unsigned long last_y = 0, last_f = 0, last_d = 0;

  while (!atomic_load(&g_stop)) {
    uint64_t t = now_ns();
    if ((t - start) / 1000000000ull >= (uint64_t)g_seconds) {
      atomic_store(&g_stop, 1);
      pulse_all();
      break;
    }

    // Alterna fases
    atomic_store(&g_phase, PH_NO_WORK);
    atomic_store(&g_work_items, 0);     // vacío a propósito
    pulse_all();
    usleep((useconds_t)g_phase_ms * 1000);

    atomic_store(&g_phase, PH_SEQ_CONTEND);
    atomic_store(&g_work_items, (long)g_nthreads * 2); // "algo" de trabajo, pero serializado
    pulse_all();
    usleep((useconds_t)g_phase_ms * 1000);

    // Report cada ~1s
    t = now_ns();
    if (t >= next_report) {
      next_report += 1000000000ull;

      unsigned long y = (unsigned long)atomic_load(&g_yields);
      unsigned long f = (unsigned long)atomic_load(&g_trylock_fail);
      unsigned long d = (unsigned long)atomic_load(&g_tasks_done);

      printf("[stats] yields/s=%lu  trylock_fail/s=%lu  tasks/s=%lu  (tot y=%lu f=%lu d=%lu)\n",
             y - last_y, f - last_f, d - last_d, y, f, d);

      last_y = y; last_f = f; last_d = d;
      fflush(stdout);
    }
  }

  return NULL;
}

int
main(int argc, char **argv) {
  if (argc >= 2) g_nthreads = atoi(argv[1]);
  if (argc >= 3) g_seconds  = atoi(argv[2]);
  if (argc >= 4) g_phase_ms = atoi(argv[3]);

  if (g_nthreads <= 0) g_nthreads = 32;
  if (g_seconds  <= 0) g_seconds  = 20;
  if (g_phase_ms <= 0) g_phase_ms = 250;

  printf("worker_pool churn demo: nthreads=%d seconds=%d phase_ms=%d\n",
         g_nthreads, g_seconds, g_phase_ms);
  printf("run: vmstat -w 1   | top -SH   | procstat -t <pid>\n");

  pthread_t *ths = calloc((size_t)g_nthreads, sizeof(*ths));
  if (!ths) { perror("calloc"); return 1; }

  for (int i = 0; i < g_nthreads; i++) {
    if (pthread_create(&ths[i], NULL, worker_main, NULL) != 0) {
      perror("pthread_create");
      return 1;
    }
  }

  pthread_t mgr;
  if (pthread_create(&mgr, NULL, manager_main, NULL) != 0) {
    perror("pthread_create(manager)");
    return 1;
  }

  pthread_join(mgr, NULL);
  for (int i = 0; i < g_nthreads; i++) pthread_join(ths[i], NULL);

  printf("done.\n");
  printf("final: yields=%llu trylock_fail=%llu tasks=%llu\n",
         (unsigned long long)atomic_load(&g_yields),
         (unsigned long long)atomic_load(&g_trylock_fail),
         (unsigned long long)atomic_load(&g_tasks_done));

  free(ths);
  return 0;
}