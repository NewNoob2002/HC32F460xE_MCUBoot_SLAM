#ifndef BSP_CHARGER_SIGNALS_H
#define BSP_CHARGER_SIGNALS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

struct bsp_charger_signals {
    bool interrupt_asserted; /* MP2762A INT# is low. */
    bool power_good;         /* PG is high, i.e. ACOK# is asserted low and Q9 is off. */
};

/* Requires write protection to be unlocked by the caller. */
bool bsp_charger_signals_init(void);
bool bsp_charger_signals_read(struct bsp_charger_signals* signals);

#ifdef __cplusplus
}
#endif

#endif
