# TempLogPro - power startup safety build

Data: 2026-07-31

## Objaw

Po odlaczeniu USB i zasilania urzadzenie nie startowalo przewidywalnie z baterii.

## Wprowadzone zmiany

1. Wylaczono automatyczne wejscie rdzenia w `WFI` w glownej petli aplikacji.

   Plik: `Src/templogpro_app.c`

   Domyslnie:

   ```c
   #define TEMPLOGPRO_IDLE_WFI_ENABLE 0
   ```

   Funkcja `templogpro_app_should_idle_sleep()` zwraca teraz `0`, dopoki tryb `WFI` nie zostanie ponownie swiadomie wlaczony i przetestowany na baterii.

2. Zmieniono domyslny config nowo formatowanego urzadzenia:

   Plik: `Src/templogpro_config.c`

   ```c
   battery_mode_enable = 0
   ```

   Zapisany w Winbondzie config nadal moze zawierac `battery_mode=1`, ale w tym buildzie nie usypia CPU, bo blokuje to makro `TEMPLOGPRO_IDLE_WFI_ENABLE`.

3. Dodano komentarz w generowanym `CONFIG.TXT`, ze `battery_mode` jest przechowywany, ale sen CPU jest tymczasowo wylaczony w safety buildzie.

## Weryfikacja po wgraniu

Build przeszedl poprawnie.

Odczyt boot-stage z RTC backup register po resecie:

```text
0x40002854 : 0000000A
```

Znaczenie:

- `10` = `TEMPLOGPRO_BOOT_STAGE_MAIN_LOOP`
- firmware doszedl do glownej petli po resecie

## Option Bytes

Odczyt bez modyfikacji:

- RDP: `0xAA`, Level 0, brak ochrony odczytu
- Write protection: nieaktywne
- BOR: Level 1, prog okolo 1.69-1.8 V
- IWDG: software
- nRST_STOP: no reset
- nRST_STDBY: no reset

## Wniosek

Aktualny firmware nie powinien juz zatrzymywac pracy aplikacji przez `WFI`.

Jesli urzadzenie nadal nie startuje po calkowitym odlaczeniu USB i zasilania, najbardziej prawdopodobny problem jest po stronie zasilania/resetu na PCB:

- bateria nie podaje poprawnego VDD na MCU,
- styk baterii lub tor zasilania ma przerwe,
- zasilanie spada ponizej progu BOR,
- uklad wlaczania zasilania wymaga dodatkowego sygnalu/przycisku.

Nastepny etap diagnostyki: test startu tylko z baterii i pomiar VDD widziany przez ST-Link po ponownym podlaczeniu SWD w trybie HOTPLUG.

## Aktualizacja po tescie zielonej diody

Objaw: po chwili migala tylko zielona dioda.

Interpretacja: rdzen dochodzil do glownej petli i aplikacja byla w stanie `LOGGING`, ale LCD nie byl poprawnie wlaczony.

Odczyt przed poprawka:

```text
LCD_CR  = 0x00000000
RCC_CSR = 0x00410003
```

Wniosek: LCD nie dostawal poprawnego zegara. RTC/LCD clock source mogl pozostac ustawiony na LSE mimo braku gotowego LSE.

Poprawka:

- `Src/templogpro_rtc.c`: przy zmianie zrodla RTC/LCD zegara najpierw wylaczane jest `RTCEN`, potem wykonywany jest reset RTC i ustawiany jest `RTCSEL`.
- `Src/main.c`: `init_lcd()` ma fallback do LSI, gdy wybrany zegar RTC/LCD nie jest gotowy.

Weryfikacja po poprawce i wgraniu:

```text
LCD_CR  = 0x000000CD
LCD_SR  = 0x0000003B
RCC_CSR = 0x00410300
BKP1R   = 0x0000000A
```

Znaczenie:

- `LCD_CR != 0` - LCD wlaczony
- `RCC_CSR` ma gotowy zegar LSE w tym tescie
- `BKP1R = 10` - aplikacja doszla do glownej petli

## Aktualizacja RTC i loggera

Po kolejnych testach znaleziono dwa dodatkowe problemy:

1. `rtc_exit_init_mode()` czyscil `INIT`, a nastepnie drugim zapisem RMW mogl przypadkiem ustawic `INIT` ponownie. Skutek: `RTC_ISR` zostawal w trybie init, a aplikacja pozniej widziala `RTC_INVALID`.

   Poprawka: `INIT` i `RSF` sa teraz czyszczone jednym zapisem, a kod czeka az `INIT` zostanie wyzerowany.

2. Rekord loggera ma 12 bajtow, a sektor Winbonda 4096 bajtow. Po 341 rekordach nastepny adres wynosil `0x3FFC`, czyli rekord przechodzil przez granice sektora. To powodowalo `FLASH_WRITE`, bo kolejny sektor nie byl skasowany.

   Poprawka: layout logow pomija koncowke sektora, jesli rekord nie miesci sie w calosci. Po rekordzie 340 kolejny zapis przechodzi na `0x4000`.

Weryfikacja po poprawce:

```text
RTC_ISR      = 0x00000037
LCD_CR       = 0x000000CD
app_state    = 1 (LOGGING)
last_error   = 0 (NONE)
record_count = 345
next_write   = 0x00004030
```

Znaczenie:

- RTC wyszedl z trybu init.
- LCD jest wlaczony.
- Logger przekroczyl granice pierwszego sektora i zapisuje dalej.
- Aplikacja nie jest juz w stanie `ERROR`.
