#include "../include/modlib.h"

static void timer_callback(void *arg);

static struct callout timer;

static int event_handler(struct module *module, int event, void *arg) {
    int e = 0; /* Error, 0 for normal return status */
    switch (event)
    {
        case MOD_LOAD:
            //hz/2 is equivalent to sample twice every second
            init_timer(&timer, hz/2, &timer_callback, NULL);

            log(LOG_INFO | LOG_LOCAL3, "loading pin_threads\n");

            monopolize_cpu(0, 2); 
            break;

        case MOD_UNLOAD:
            callout_drain(&timer);
            release_cpu(0, 2); 

            log(LOG_INFO | LOG_LOCAL3, "pin_threads unloaded\n");
            break;

        default:
            e = EOPNOTSUPP; /* Error, Operation Not Supported */
            break;
    }

    return (e);
}

static void
timer_callback(void *arg)
{
    
    callout_reset(&timer, obtain_random_freq(), timer_callback, NULL);
}

static moduledata_t pin_threads_data = {
    "pin_threads",
    event_handler,
    NULL
};

DECLARE_MODULE(pin_threads, pin_threads_data, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(pin_threads, 1);