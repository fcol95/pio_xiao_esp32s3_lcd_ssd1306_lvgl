#ifndef LCD__H__
#define LCD__H__

#include "esp_err.h"

esp_err_t lcd_init(void);
void      lcd_task(void *pvParameter);

#endif // LCD__H__
