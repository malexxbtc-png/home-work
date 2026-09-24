#ifndef SMA_FILTER_H
#define SMA_FILTER_H

#include <string.h>
#include <stdint.h>
#include "config.h"

typedef struct {
    uint32_t buffer[SMA_WINDOW_SIZE];
    uint64_t sum;
    uint8_t index;
    uint8_t count;
} sma_filter_t;

void sma_init(sma_filter_t *filter);
uint32_t sma_update(sma_filter_t *filter, uint32_t new_sample);

#endif // SMA_FILTER_H