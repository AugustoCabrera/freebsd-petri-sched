#include "modlib.h"

void 
init_timer(struct callout *timer, int freq, void (*timer_callback)(void *), void *arg) 
{
    callout_init(timer, CALLOUT_MPSAFE);
    callout_reset(timer, freq, timer_callback, arg);
}

int 
obtain_random_freq(void)
{
    int random_interval_ms;
    int freq;

    int min_ms = 400;
    int max_ms = 600;

    random_interval_ms = min_ms + (random() % (max_ms - min_ms + 1));
    freq = (random_interval_ms * hz)/1000;
    
    return freq;
}