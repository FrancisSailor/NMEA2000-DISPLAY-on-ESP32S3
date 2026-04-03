#include <Arduino.h>
#include <FFat.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "ui_img_manager.h"

static bool s_ui_assets_all_ok = true;

extern "C" bool ui_assets_all_ok(void)
{
    return s_ui_assets_all_ok;
}

extern "C" uint8_t* _ui_load_binary(const char* fname, const uint32_t size)
{
    if (!fname || size == 0) {
        Serial.println("[ASSET] invalid fname/size, using dummy");
        size_t s = size ? size : 4;
        uint8_t* dummy = (uint8_t*)heap_caps_malloc(s, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (dummy) memset(dummy, 0, s);
        s_ui_assets_all_ok = false;
        return dummy;
    }

    const char* rel = fname;
    if (rel[0] != '\0' && rel[1] == ':') {
        rel += 2;
    }

    char path1[196];
    char path2[196];

    if (rel[0] == '/') {
        snprintf(path1, sizeof(path1), "/ffat%s", rel);
    } else {
        snprintf(path1, sizeof(path1), "/ffat/%s", rel);
    }

    if (rel[0] == '/') {
        snprintf(path2, sizeof(path2), "%s", rel);
    } else {
        snprintf(path2, sizeof(path2), "/%s", rel);
    }

    const char* candidates[3] = { path1, path2, nullptr };

    File f;
    const char* used = nullptr;
    for (int i = 0; candidates[i]; ++i) {
        f = FFat.open(candidates[i], FILE_READ);
        if (f) {
            used = candidates[i];
            break;
        }
    }

    if (!f) {
        Serial.printf("[ASSET] open FAIL %s (tried '%s', '%s') -> using dummy\n",
                      fname, path1, path2);
        size_t s = size ? size : 4;
        uint8_t* dummy = (uint8_t*)heap_caps_malloc(s, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (dummy) memset(dummy, 0, s);
        s_ui_assets_all_ok = false;
        return dummy;
    }

    uint8_t* buf = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        Serial.printf("[ASSET] PSRAM alloc FAIL %s (%u bytes) -> using dummy\n",
                      used, (unsigned)size);
        f.close();
        size_t s = size ? size : 4;
        uint8_t* dummy = (uint8_t*)heap_caps_malloc(s, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (dummy) memset(dummy, 0, s);
        s_ui_assets_all_ok = false;
        return dummy;
    }

    size_t rd = f.read(buf, size);
    f.close();

    if (rd != size) {
        Serial.printf("[ASSET] read FAIL %s (read=%u exp=%u) -> using dummy\n",
                      used, (unsigned)rd, (unsigned)size);
        heap_caps_free(buf);
        size_t s = size ? size : 4;
        uint8_t* dummy = (uint8_t*)heap_caps_malloc(s, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (dummy) memset(dummy, 0, s);
        s_ui_assets_all_ok = false;
        return dummy;
    }

    Serial.printf("[ASSET] OK %s -> %p (%u bytes)\n",
                  used, buf, (unsigned)size);
    return buf;
}
