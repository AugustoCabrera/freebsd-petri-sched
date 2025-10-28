// loadpulse.c — generador de carga en ráfagas (CPU-on / sleep-off)
// FreeBSD-friendly: afinidad con cpuset(2).
// Compilar: cc -O2 -std=c11 -pthread loadpulse.c -o loadpulse
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>   // PRIu64
#include <stdatomic.h>
#include <stdbool.h>    // bool, true, false
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>     // sigaction / SIGINT
#include <pthread.h>

#include <sys/param.h>
#include <sys/cpuset.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <pthread_np.h>

typedef struct {
    int id;
    int cpu;            // -1 = sin afinidad
    uint64_t on_ms;     // ráfaga activa (ms)
    uint64_t off_ms;    // descanso (ms)
    uint64_t iters;     // 0 = infinito
    int nice_adj;       // ajuste de nice (opcional)
    _Atomic uint64_t done_iters;
} worker_cfg_t;

static atomic_bool stop_flag = false;

static inline uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void busy_spin_ns(uint64_t ns) {
    uint64_t start = now_ns();
    while (now_ns() - start < ns) {
        __asm__ volatile("" ::: "memory");
    }
}

static void *worker(void *arg) {
    worker_cfg_t *cfg = (worker_cfg_t*)arg;

    // Afinidad por CPU (opcional)
    if (cfg->cpu >= 0) {
        cpuset_t set;
        CPU_ZERO(&set);
        CPU_SET(cfg->cpu, &set);
        if (cpuset_setaffinity(CPU_LEVEL_WHICH, CPU_WHICH_TID,
                               (id_t)pthread_getthreadid_np(),
                               sizeof(set), &set) != 0) {
            perror("cpuset_setaffinity");
        }
    }

    // Ajuste de nice (si hay permisos)
    if (cfg->nice_adj) {
        int cur = nice(0);
        (void)cur;
        if (nice(cfg->nice_adj) == -1 && errno != 0) {
            perror("nice");
        }
    }

    uint64_t i = 0;
    const uint64_t on_ns  = cfg->on_ms  * 1000000ull;
    const uint64_t off_ns = cfg->off_ms * 1000000ull;

    while (!atomic_load(&stop_flag)) {
        // ON: CPU a fondo
        uint64_t t0 = now_ns();
        busy_spin_ns(on_ns);
        uint64_t t1 = now_ns();

        // OFF: dormir
        if (off_ns > 0) {
            struct timespec req = {
                .tv_sec  = (time_t)(off_ns / 1000000000ull),
                .tv_nsec = (long)(off_ns % 1000000000ull)
            };
            nanosleep(&req, NULL);
        }
        uint64_t t2 = now_ns();

        // Logging por hilo a stderr (moderado)
        fprintf(stderr,
                "thr=%d iter=%" PRIu64 " on_ms=%.3f off_ms=%.3f dur_on=%.3f dur_off=%.3f\n",
                cfg->id, i,
                cfg->on_ms/1.0, cfg->off_ms/1.0,
                (t1 - t0)/1e6, (t2 - t1)/1e6);

        cfg->done_iters = ++i;
        if (cfg->iters && i >= cfg->iters) break;
    }
    return NULL;
}

static void usage(const char *prog) {
    fprintf(stderr,
      "Uso: %s [-t hilos] [-on ms] [-off ms] [-iters N] [-pin] [-nice k]\n"
      "     -t      número de hilos (por defecto 4)\n"
      "     -on     milisegundos de ráfaga activa (CPU) (por defecto 50)\n"
      "     -off    milisegundos de descanso (sleep) (por defecto 50)\n"
      "     -iters  iteraciones por hilo (0=infinito, por defecto 0)\n"
      "     -pin    fija afinidad: asigna hilos round-robin a CPUs\n"
      "     -nice   ajusta nice por hilo (p.ej. -5 o +10)\n", prog);
}

static void on_sigint(int signo) {
    (void)signo;
    atomic_store(&stop_flag, true);
}

int main(int argc, char **argv) {
    int threads = 4;
    uint64_t on_ms = 50, off_ms = 50, iters = 0;
    int pin = 0, nice_adj = 0;

    for (int i=1; i<argc; ++i) {
        if      (!strcmp(argv[i], "-t")     && i+1<argc) threads  = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-on")    && i+1<argc) on_ms    = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "-off")   && i+1<argc) off_ms   = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "-iters") && i+1<argc) iters    = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "-pin"))               pin      = 1;
        else if (!strcmp(argv[i], "-nice")  && i+1<argc) nice_adj = atoi(argv[++i]);
        else { usage(argv[0]); return 1; }
    }

    // Descubrir cantidad de CPUs lógicas (FreeBSD primero por sysctl, si no, sysconf)
    int ncpu = 1;
    size_t sz = sizeof(ncpu);
    if (sysctlbyname("hw.ncpu", &ncpu, &sz, NULL, 0) != 0 || ncpu <= 0) {
        long sc = sysconf(_SC_NPROCESSORS_ONLN);
        if (sc > 0) ncpu = (int)sc;
    }
    if (ncpu <= 0) ncpu = 1;

    fprintf(stderr, "cfg: threads=%d on=%lums off=%lums iters=%lu pin=%d nice=%d ncpu=%d\n",
            threads, (unsigned long)on_ms, (unsigned long)off_ms,
            (unsigned long)iters, pin, nice_adj, ncpu);

    pthread_t *th = calloc((size_t)threads, sizeof(*th));
    worker_cfg_t *cfg = calloc((size_t)threads, sizeof(*cfg));
    if (!th || !cfg) { perror("calloc"); return 1; }

    for (int i=0;i<threads;++i) {
        cfg[i] = (worker_cfg_t){
            .id=i,
            .cpu = pin ? (i % ncpu) : -1,
            .on_ms=on_ms, .off_ms=off_ms,
            .iters=iters, .nice_adj=nice_adj,
            .done_iters=0
        };
        if (pthread_create(&th[i], NULL, worker, &cfg[i]) != 0) {
            perror("pthread_create");
            return 1;
        }
    }

    // Manejo de Ctrl-C si iters == 0: esperar señal y pedir stop a los hilos
    if (iters == 0) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = on_sigint;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGINT, &sa, NULL);

        fprintf(stderr, "Running (infinite). Press Ctrl-C to stop...\n");
        pause();                    // se despierta con *alguna* señal
        atomic_store(&stop_flag, true);   // asegurar stop si fue otra señal
    }

    for (int i=0;i<threads;++i) pthread_join(th[i], NULL);

    // Resumen simple
    uint64_t total = 0;
    for (int i=0;i<threads;++i)
        total += atomic_load(&cfg[i].done_iters);

    fprintf(stderr, "total iters completadas: %lu\n", (unsigned long)total);

    free(th);
    free(cfg);
    return 0;
}
