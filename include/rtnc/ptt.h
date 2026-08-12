#ifndef RTNC_PTT_H
#define RTNC_PTT_H

#include "rtnc/platform_config.h"

#include <stdbool.h>

typedef struct {
    void *chip;
    void *line;
    bool  active_high;
    bool  state;
} rtnc_ptt_t;

bool rtnc_ptt_init(rtnc_ptt_t *ptt, const rtnc_gpio_config_t *config);
bool rtnc_ptt_set(rtnc_ptt_t *ptt, bool enabled);
bool rtnc_ptt_is_enabled(const rtnc_ptt_t *ptt);
void rtnc_ptt_deinit(rtnc_ptt_t *ptt);

#endif
