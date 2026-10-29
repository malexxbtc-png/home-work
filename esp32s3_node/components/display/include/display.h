#ifndef DISPLAY_H
#define DISPLAY_H

/* Ініціалізує SPI2 (лише дисплей), панель ST7789 і LVGL. SD працює на окремій шині SPI3. */
void display_init(void);

/* Запускає задачу оновлення екрана */
void display_start_task(void);

#endif /* DISPLAY_H */
