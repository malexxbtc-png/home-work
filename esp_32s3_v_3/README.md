# ESP32-S3 node: дисплей, погода, зв'язок з Pico

Складання (ESP-IDF 5.3 або новіший, перевірено лише читанням коду; ваш лог показує IDF 6.1):

    idf.py set-target esp32s3
    idf.py menuconfig        # Weather forecast -> Wi-Fi SSID / password (не комітьте їх)
    idf.py build flash monitor

ВАЖЛИВО: цільова плата має бути esp32s3. Якщо в логу є `Building ESP-IDF components for target esp32`,
виконайте `idf.py set-target esp32s3` (видаляє build і старий sdkconfig). `sdkconfig.defaults`
застосовується лише коли файлу sdkconfig немає, тому без цього кроку шрифти LVGL, обмін кольорів
і PSRAM не ввімкнуться.

`sdkconfig.defaults` розрахований на ESP32-S3 N16R8 (16 МБ flash, 8 МБ octal PSRAM).
Для іншої плати закоментуйте блок FLASHSIZE / SPIRAM.

## Піни

| Призначення | GPIO |
|---|---|
| UART TX -> майстер GPIO16 | 17 |
| UART RX <- майстер GPIO17 | 18 |
| IRQ -> майстер (необов.) | 16 |
| TFT SCK / MOSI (шина SPI2; MISO не потрібен) | 39 / 38 |
| TFT CS / DC / RST | 41 / 42 / 47 |
| SD SCK / MOSI / MISO / CS (окрема шина SPI3) | 48 / 3 / 40 / 21 |

Підсвітка TFT (BLK) має бути підключена до 3V3.
Дисплей на SPI2, SD на власній шині SPI3: вони не пов'язані. Піни SD змінюються в menuconfig → Weather SD card.
GPIO3 є strapping-піном (використовується лише як вихід MOSI після старту), на деяких платах на GPIO48 стоїть світлодіод.

## Протокол з майстром

Рядок: `<JSON>*<CRC16-CCITT hex>\n` (поліном 0x1021, початкове значення 0xFFFF).

ESP32-S3 -> майстер: `hb` (кожні 5 с), `wx` (щохвилини і після кожного запиту), `time` (щохвилини),
`snap_done`. Поле `epoch` у `time` це Unix-час місцевого годинника (UTC + зсув Києва).
Вік погоди `wx_age` передається в хвилинах, 999 означає «немає даних».

Майстер -> ESP32-S3: `data`, `snap`.

## Екран

Три блоки: час, дата і день тижня; INSIDE: лише температура в приміщенні (від майстра); OUTSIDE: температура надворі великою,
вітер (Wind, м/с) і опади (Rain, мм або Snow, см), діапазон дня (Today min..max). Унизу: тривоги і подяка Open-Meteo.

## Якщо зображення зсунуте або кольори негативні

menuconfig -> Weather display: поміняйте GAP_X і GAP_Y місцями або вимкніть Invert colours.

## Камера і фото

OV7670 (без FIFO) через драйвер `espressif/esp32-camera`: кадр RGB565 (VGA або QVGA, menuconfig → Weather camera),
кодується в JPEG програмно і зберігається на SD як `/sdcard/photos/YYYYMMDD/HHMMSS_<причина>.jpg`
(причина: pir, wind, fog, ice, rain, snow, manual). Якщо вільного місця менше за 200 МБ, видаляються найстаріші
папки днів (остання не чіпається). Подія пишеться ще й у `/sdcard/weather_log.csv` (колонка `photo`).

Знімок робиться за запитом майстра `{"t":"snap","reason":..,"seq":N}` в окремій задачі; у відповідь
`{"t":"snap_done","ok":true,"file":"20261025/194512_pir.jpg","seq":N}` (або `ok:false` з `err`:
`no_camera`, `no_sd`, `capture`, `encode`, `write`, `busy`). Статуси `cam` і `sd` йдуть у heartbeat.

Піни камери: XCLK15 SIOD4 SIOC5 VSYNC6 HREF7 PCLK13 D0-D7 = 11,9,8,10,12,14,1,2 (змінюються в menuconfig).
Якщо кольори хибні — увімкніть «Swap bytes»; якщо зображення дзеркальне — «Mirror» / «Flip».
Telegram не реалізований.

Схема підключення: `docs/schema_node.svg` (PNG поруч).

## Що не перевірено

Код не запускався на залізі; камера і SD компілювалися лише з заглушками. OV7670 чутлива до довжини дротів
і живлення, початкова експозиція налаштовується пропуском перших кадрів (`WARMUP_FRAMES`).

Weather data by Open-Meteo.com (CC BY 4.0) показується на екрані.

## Збірка під ESP32-S3 (важливо)

Якщо в папці проєкту вже є `sdkconfig` або `build` від іншої цілі (esp32), `sdkconfig.defaults`
не застосовується і збірка йде під esp32, де немає GPIO 38-47. Видаліть їх і задайте ціль:

    idf.py fullclean
    del sdkconfig            (Windows; у Linux: rm sdkconfig)
    idf.py set-target esp32s3
