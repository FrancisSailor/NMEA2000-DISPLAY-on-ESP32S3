#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool lvgl_port_init(void);
bool lvgl_port_lock(int timeout_ms);
bool lvgl_port_unlock(void);

#ifdef __cplusplus
}
#endif
