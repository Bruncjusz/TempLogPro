# TempLogPro - test toru zasilania USB/bateria

## Cel

Oddzielic pobor wynikajacy z fizycznego dolaczenia kabla USB od poboru
powodowanego przez HSE, PLL 32 MHz, peryferium USB i stos MSC/CDC.

## Potwierdzone pomiary

- bateria, wygaszony ekran: okolo 0,07 mA;
- bateria, aktywny ekran: okolo 0,35 mA;
- krotki szczyt: okolo 0,5 mA;
- prad z baterii po dolaczeniu USB: okolo 16,35 mA;
- BAT+ bez baterii, przy aktywnym USB: 0 V;
- pomiary wykonano bez podlaczonego ST-Linka;
- PCB nie bylo modyfikowane poza przewodami SWD.

BAT+ jest odseparowany od zasilania USB. Dodatni prad 16,35 mA plynie z
baterii do urzadzenia, a nie z USB do baterii.

## Obraz diagnostyczny

`TempLogPro_usb_power_path_diag.hex` wykrywa hosta na PA11/PA12, ale nie
uruchamia HSE, PLL, pull-upu D+, peryferium USB ani stosu MSC/CDC. MCU pozostaje
w bateryjnym profilu zegara i uspienia. Brak enumeracji USB jest w tym obrazie
zamierzony.

Normalny firmware ma `TEMPLOGPRO_USB_POWER_PATH_DIAG=0` i pozostaje bez zmian.

## Procedura

1. Wgrac obraz diagnostyczny przy odlaczonej baterii.
2. Odlaczyc ST-Link.
3. Wlaczyc baterie przez amperomierz i poczekac na wygaszenie cyfr LCD.
4. Zapisac prad bez kabla USB.
5. Dolaczyc USB i zapisac prad po 10 s. Komputer nie wykryje urzadzenia.
6. Odlaczyc USB i potwierdzic powrot do poprzedniego poboru.

Jezeli po dolaczeniu kabla nadal pozostanie okolo 0,07 mA, wzrost do 16,35 mA
powoduje aktywny tryb USB firmware. Jezeli pobor wzrosnie znacznie mimo
zablokowanego USB, przyczyna lezy w fizycznym torze zasilania lub liniach USB.
