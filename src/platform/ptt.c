#include "rtnc/ptt.h"

#include <gpiod.h>
#include <stddef.h>

bool rtnc_ptt_init(rtnc_ptt_t *ptt, const rtnc_gpio_config_t *config) {
    struct gpiod_chip *chip;
    struct gpiod_line *line;
    if (ptt == NULL || config == NULL) {
        return false;
    }
    ptt->chip = NULL;
    ptt->line = NULL;
    ptt->active_high = config->active_high;
    ptt->state = false;
    chip = gpiod_chip_open_by_number((unsigned int) config->port);
    if (chip == NULL) {
        return false;
    }
    line = gpiod_chip_get_line(chip, (unsigned int) config->pin);
    if (line == NULL ||
        gpiod_line_request_output(line, "Reticulum-TNC PTT", config->active_high ? 0 : 1) < 0) {
        gpiod_chip_close(chip);
        return false;
    }
    ptt->chip = chip;
    ptt->line = line;
    return true;
}

bool rtnc_ptt_set(rtnc_ptt_t *ptt, bool enabled) {
    int value;
    if (ptt == NULL || ptt->line == NULL) {
        return false;
    }
    value = enabled == ptt->active_high ? 1 : 0;
    if (gpiod_line_set_value((struct gpiod_line *) ptt->line, value) < 0) {
        return false;
    }
    ptt->state = enabled;
    return true;
}

bool rtnc_ptt_is_enabled(const rtnc_ptt_t *ptt) {
    return ptt != NULL && ptt->state;
}

void rtnc_ptt_deinit(rtnc_ptt_t *ptt) {
    if (ptt == NULL) {
        return;
    }
    if (ptt->line != NULL) {
        (void) rtnc_ptt_set(ptt, false);
        gpiod_line_release((struct gpiod_line *) ptt->line);
    }
    if (ptt->chip != NULL) {
        gpiod_chip_close((struct gpiod_chip *) ptt->chip);
    }
    ptt->chip = NULL;
    ptt->line = NULL;
    ptt->state = false;
}
