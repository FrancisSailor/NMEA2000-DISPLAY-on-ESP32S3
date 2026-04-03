
ESP32-S3 FFat Web Uploader
==========================

Dit project is een eenvoudige en **betrouwbare** manier om bestanden naar je
FFat-partitie te uploaden, zonder USB MSC/MTP-complexiteit en zonder de
wear-levelling van FFat te breken.

Workflow
--------
1. Kies in Arduino IDE een Partition Scheme met een FFat/FATFS partitie
   (bij jou: label 'ffat', ~9-10MB).
2. Flash deze sketch naar je ESP32-S3.
3. Verbind met het WiFi-netwerk:
   - SSID: ESP32S3_FFAT_UPLOAD
   - PASS: 12345678
4. Open in je browser: http://192.168.4.1
5. Upload je SquareLine/LVGL asset-bestanden.
   Ze worden direct in FFat opgeslagen.
6. Flash daarna je echte firmware die `FFat.begin()` gebruikt en de files leest.

Waarom dit beter is dan raw MSC naar FFat
-----------------------------------------
- FFat in de Espressif/Arduino omgeving gebruikt meestal een wear-levelling laag.
- USB MSC presenteert ruwe blocks aan de host.
- Laat je Windows rechtstreeks in die blocks schrijven, dan klopt de mapping
  met de wear-levelling van FFat niet meer → risico op corrupte data.
- Deze web-uploader schrijft via `FFat` zelf, dus altijd veilig.

Opmerkingen
-----------
- De implementatie ondersteunt meerdere uploads (browsers sturen ze meestal
  als losse POSTs). Alle bestanden komen in de root van FFat.
- Pas AP_SSID/AP_PASS desgewenst aan.
