# ESP32-S3 node: дисплей, погода, зв'язок з Pico


## Піни

| Призначення | GPIO |
|---|---|
| UART TX -> Pico GP1 | 17 |
| UART RX <- Pico GP0 | 18 |
| IRQ -> Pico GP2 | 16 |
| TFT SCK / MOSI / MISO | 39 / 38 / 40 |
| TFT CS / DC / RST | 41 / 42 / 47 |
| SD CS (та сама шина SPI2) | 21 |

Підсвітка TFT (BLK) має бути підключена до 3V3.
SD і дисплей на одній шині: перший ініціалізується дисплей, потім SD.

## Протокол з Pico

Рядок: `<JSON>*<CRC16-CCITT hex>\n` (поліном 0x1021, початкове значення 0xFFFF).

ESP -> Pico: `hb` (кожні 5 с), `wx` (щохвилини і після кожного запиту), `time` (щохвилини),
`snap_done`. Поле `epoch` у `time` це Unix-час місцевого годинника (UTC + зсув Києва).
Вік погоди `wx_age` передається в хвилинах, 999 означає «немає даних».

Pico -> ESP: `data`, `snap`.

## Якщо зображення зсунуте або кольори негативні

menuconfig -> Weather display: поміняйте GAP_X і GAP_Y місцями або вимкніть Invert colours.

## Що ще не реалізовано

Камера OV7670, збереження кадрів на SD і Telegram-бот. На `snap` ESP записує подію
в /sdcard/weather_log.csv і відповідає `snap_done` з `ok:false`.

Weather data by Open-Meteo.com (CC BY 4.0) показується на екрані.
