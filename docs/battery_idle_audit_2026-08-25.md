# TempLogPro - audyt poboru na baterii 2026-08-25

## Zakres

Audyt dotyczy firmware `0.8.12-power` po pomiarach:

- okolo 60 uA pomiedzy aktywnosciami na baterii;
- okolo 0,97 mA podczas aktywnosci loggera;
- okolo 23,7 mA przy aktywnym USB.

USB, format loggera, rekordy i wirtualny system plikow nie zostaly przebudowane.

## Zweryfikowana sciezka low-power

1. Glowna petla wykonuje USB process, aplikacje i telemetrie, a potem wybiera
   `STOP` albo plytki `WFI`.
2. `WFI` jest dozwolone tylko przy wlaczonym trybie baterii i nieaktywnym
   Winbondzie.
3. `STOP` jest dozwolony po wygaszeniu cyfr LCD, poza aktywna akcja menu,
   zapisem Flash, odbudowa statystyk, alarmowym impulsem LED i aktywnym USB.
4. Przed `STOP` SysTick jest zawieszany. MCU budzi RTC Wakeup Timer co 1 s albo
   zbocze na PA0/PA1 przez EXTI0/EXTI1.
5. Po pobudce z RTC tick HAL jest przesuwany o 1000 ms, wykonywana jest jedna
   iteracja pracy i CPU ponownie moze wejsc w `STOP`.
6. Przez pierwsze 10 s po obsludze menu dziala `WFI` z SysTick 100 Hz. Jest to
   celowe okno responsywnego menu, a nie docelowy stan spoczynku.

Nie znaleziono stalej petli busy-wait w normalnym stanie bateryjnym. Petle
oczekujace wystepuja tylko podczas konkretnej transakcji SPI, ADC, I2C albo
inicjalizacji zegara.

## Stan peryferiow

| Blok | Stan pomiedzy pomiarami |
|---|---|
| CPU | STOP po 10 s bezczynnosci, wczesniej WFI |
| SysTick | 100 Hz w WFI, przerwanie wylaczone w STOP |
| RTC | aktywny, budzenie co 1 s |
| LCD | aktywny celowo; po 10 s pozostaja tylko ikony statusu |
| Winbond W25X40 | komenda Deep Power-down `B9h` po operacji |
| SPI1 | po tej poprawce zegar APB2 i `SPE` sa wylaczane razem z uspieniem Flash |
| ADC/VREFINT | uruchamiany na zadanie, po konwersji ADC, VREFINT i HSI sa wylaczane |
| I2C | programowy; SCL/SDA sa zwalniane do stanu wysokiego |
| Czujnik | pozostaje w swoim domyslnym trybie; nie znamy jeszcze pewnej komendy shutdown |
| USB | po utracie hosta wykonywany jest DeInit, piny PA11/PA12 ida w analog |
| TIM10 | aktywny tylko podczas jednorazowej kalibracji LSI, potem wylaczany |
| SYSCFG | po tej poprawce wylaczany po konfiguracji EXTI i nieudanej probie USB |
| DWT/trace | po tej poprawce aktywny tylko przy wykrytym USB |
| SWD debug | firmware nie ustawia `DBG_STOP`; aktywna sesja debug moze zaburzyc pomiar |

Nie zmieniono trybu pozostalych GPIO, poniewaz ich rzeczywiste polaczenia z PCB
nie sa w pelni potwierdzone.

## Kontrolowane poprawki

- Profil bateryjny MSI 2,097 MHz ustawia regulator na Voltage Scale 3 dopiero po
  bezpiecznym obnizeniu SYSCLK. Profil USB nadal ustawia Scale 1.
- SPI1 jest budzony przed `ABh` i gaszony po poprawnym `B9h`.
- Zegar SYSCFG nie pozostaje wlaczony tylko z powodu skonfigurowania EXTI.
- DWT i trace nie pracuja na baterii tylko dla celow diagnostycznych.
- Gdy RTC pracuje z LSI i ma juz zapisana kalibracje, pomocniczy LSE jest
  wylaczany. LSE pozostaje wlaczony podczas pierwszej kalibracji.

## Interpretacja 60 uA

Wedlug tabeli 22 dokumentacji ST sam STM32L152 w STOP z RTC i aktywnym LCD ma
typowy pobor rzedu kilku uA w temperaturze pokojowej. Wynik 60 uA jest wiec
wyzszy od minimum samego MCU, ale nadal realny dla calego urzadzenia z LCD,
czujnikiem, Flash, pull-upami i pobudka co 1 s.

Zrodlo ST: [STM32L151xC/C-A and STM32L152xC/C-A datasheet](https://www.st.com/resource/en/datasheet/stm32l152rc-a.pdf), tabele 22 i 24.

60 uA odpowiada okolo 1,44 mAh na dobe i 526 mAh na rok, bez doliczania
aktywnych impulsow i samorozladowania baterii.

Dodatkowy sredni prad impulsu mozna przyblizyc wzorem:

`I_extra = (0,97 mA - 0,06 mA) * czas_impulsu / interwal`

Dla interwalu 10 s daje to okolo:

- 0,91 uA dla impulsu 10 ms;
- 9,1 uA dla impulsu 100 ms;
- 45,5 uA dla impulsu 500 ms.

Dlatego kolejnym waznym wynikiem jest czas trwania impulsu 0,97 mA, nie tylko
jego wartosc szczytowa.

## Zalozenia i niewiadome

- Nie znamy jeszcze dokladnego modelu czujnika i jego poboru w trybie ciaglym.
- Nie potwierdzono oscyloskopem ani rejestratorem czasu aktywnego impulsu.
- Odczyt VREFINT pokazuje VDD MCU, a nie osobno zaciski baterii CR3450.
- Spadek baterii z 3,0 V do 2,1 V po dolaczeniu USB wskazuje przede wszystkim na
  tor power-path, wspolna szyne albo backfeed. Szybszy zegar USB moze zwiekszyc
  obciazenie plytki, ale przy prawidlowej separacji nie powinien sam obnizac
  napiecia odizolowanej baterii.

## Nastepne pomiary

1. Mierzyc dopiero po wygaszeniu cyfr LCD i zakonczeniu odbudowy statystyk.
2. Przy USB wlaczonym wpiac amperomierz tylko szeregowo z bateria.
3. Zapisac prad baterii dla USB bezczynnego i podczas kopiowania pliku.
4. Zmierzyc czas impulsu 0,97 mA przy interwale 10 s.
5. Po identyfikacji czujnika rozwazyc jego shutdown/one-shot jako osobny etap.

## Weryfikacja kodu

- Debug: kompilacja bez bledow i ostrzezen.
- Release `-Os`: kompilacja bez bledow i ostrzezen.
- Release: text 128244 B, data 520 B, bss 15432 B.
- `power_accounting_vectors.py`: OK.
- `rtc_monotonic_vectors.py`: OK.
- `logger_runtime_vectors.py`: OK.
- `button_fsm_vectors.py`: OK.
- Obraz nie zostal jeszcze wgrany na urzadzenie; wymagany jest kontrolowany test
  LCD, przyciskow, pomiaru i ponownej enumeracji USB.
