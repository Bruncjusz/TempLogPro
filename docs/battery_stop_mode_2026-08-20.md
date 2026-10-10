# TempLogPro - STOP mode 2026-08-20

## Cel

Ograniczenie wybudzen rdzenia na baterii bez zmiany obslugi LCD, przyciskow,
loggera, Winbonda ani aktywnego USB.

## Implementacja

- Nowy modul `templogpro_power` zarzadza profilem MSI 2,097 MHz i trybem STOP.
- Pelne menu oraz wszystkie akcje pracuja jak dotychczas na SysTick 100 Hz i `WFI`.
- STOP jest dozwolony dopiero po wygaszeniu cyfr LCD po 10 s bezczynnosci.
- RTC Wakeup Timer jest uzbrajany jako jednorazowe okno 1 s przed kazdym snem.
- PA0 i PA1 wybudzaja przez EXTI0/EXTI1 na obu zboczach.
- ISR przyciskow nie tworza zdarzen menu. Ustawiaja tylko flage pobudki,
  a fizyczny stan nadal czyta wylacznie `templogpro_buttons`.
- Po pobudce z RTC `uwTick` jest przesuwany o 1000 ms. Po pobudce przyciskiem
  zegar nie dostaje sztucznej pelnej sekundy.
- Po STOP sprzet automatycznie wraca do MSI; firmware odtwarza `SystemCoreClock`
  i SysTick 100 Hz.
- USB nie jest usypiane po wykryciu hosta. Przy braku hosta RTC pozwala wykonywac
  probe PA11/PA12 co 5 s.

## Zabezpieczenia

STOP jest blokowany, gdy:

- `battery_mode_enable` jest wylaczone;
- aktywne jest USB lub wykryto VBUS hosta;
- trwa zapis/odczyt wymagajacy aktywnego Winbonda;
- trwa odbudowa statystyk;
- LCD lub akcja menu sa aktywne;
- przycisk jest wcisniety albo trwa 250 ms okna debounce po wybudzeniu;
- widoczny jest impuls serca, zielonej LED albo aktywna faza czerwonego alarmu.

## Weryfikacja

- `button_fsm_vectors.py`: OK.
- `logger_runtime_vectors.py`: OK.
- Osobna kompilacja sterownika przyciskow z `TEMPLOGPRO_BUTTONS_TEST`: OK.
- Debug: 0 bledow, 0 ostrzezen.
- Release `-Os`: 0 bledow, 0 ostrzezen.
- Release: text 120948 B, data 520 B, bss 14272 B.
- SHA-256 `Release/TempLogPro.elf`:
  `8E94812FD91D923435F8DD88C139C18DF97E1FDACFD02EAB5E3FE5B454B2B702`.
- Obraz Release zapisano do STM32L152RCT6 przy SWD 100 kHz.
- Po wygaszeniu LCD domena debug jest niedostepna dla polaczenia hot-plug,
  co jest zgodne z rzeczywistym wejsciem MCU do STOP bez `DBG_STOP`.

## Test uzytkownika

1. Po 10 s bezczynnosci nacisnac krotko dowolny przycisk: ma tylko obudzic menu.
2. Przejsc kartami w obie strony i wykonac dlugie START/STOP.
3. W RUN pozostawic LCD wygaszony przez co najmniej dwa interwaly i sprawdzic
   wzrost liczby rekordow.
4. Podlaczyc USB do uspionego loggera i odczekac do 5 s na enumeracje.
5. Po odlaczeniu USB sprawdzic ponowny powrot do wygaszania i STOP.
