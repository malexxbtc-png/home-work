#pragma once

#include 
#include "config.h"

typedef struct {
    uint32_t buffer[SMA_WINDOW_SIZE];
    uint8_t index;
    uint32_t sum;
    uint8_t count;
} sma_filter_t;

void sma_init(sma_filter_t *filter);
uint32_t sma_update(sma_filter_t *filter, uint32_t new_sample);