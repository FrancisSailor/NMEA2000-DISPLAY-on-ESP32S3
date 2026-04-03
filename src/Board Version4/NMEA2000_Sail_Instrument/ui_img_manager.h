#ifndef _UI_IMG_MANAGER_H
#define _UI_IMG_MANAGER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t* _ui_load_binary(const char* fname, const uint32_t size);
bool ui_assets_all_ok(void);

#define UI_LOAD_IMAGE _ui_load_binary

#ifdef __cplusplus
}
#endif

#endif
