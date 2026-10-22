#ifndef DISPLAY_H
#define DISPLAY_H

/* Ініціалізує SPI2, панель ST7789 і LVGL. Викликати до sd_card_init(). */
void display_init(void);

/* Запускає задачу оновлення екрана */
void display_start_task(void);

#endif /* DISPLAY_H */
