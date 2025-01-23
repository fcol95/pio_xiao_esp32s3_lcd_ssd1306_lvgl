#ifndef UI__H__
#define UI__H__

#include "esp_err.h"

esp_err_t ui_init(void);
void      ui_task(void *pvParameter);

#endif // UI__H__