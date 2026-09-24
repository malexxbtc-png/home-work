#include "sma_filter.h"

void sma_init(sma_filter_t *filter) {
    if (!filter) return;
    filter->index = 0;
    filter->sum = 0;
    filter->count = 0;
    memset(filter->buffer, 0, sizeof(filter->buffer));
}

uint32_t sma_update(sma_filter_t *filter, uint32_t new_sample) {
    if (!filter) return 0;

    if (filter->count < SMA_WINDOW_SIZE) {
        filter->sum += new_sample;
        filter->buffer[filter->index] = new_sample;
        filter->count++;
    } else {
        filter->sum -= filter->buffer[filter->index];
        filter->sum += new_sample;
        filter->buffer[filter->index] = new_sample;
    }

    filter->index = (filter->index + 1) % SMA_WINDOW_SIZE;
    return (uint32_t)(filter->sum / filter->count);
}