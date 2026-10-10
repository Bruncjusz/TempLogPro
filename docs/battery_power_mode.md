# TempLogPro - tryb zasilania bateryjnego

## Fakty zweryfikowane

- Mikrokontroler: STM32L152RCT6.
- Zasilanie bateryjne: bateria litowa CR3450 3 V (informacja od uzytkownika).
- Po podlaczeniu USB plyta jest zasilana z USB i VDD podczas testu wynosilo 3,30 V.
- USB korzysta z PA11/PA12 oraz z HSE 8 MHz i PLL 96 MHz.
- Dzialajacy profil USB wymaga SYSCLK 32 MHz i zegara USB 48 MHz.
- Logger, LCD, przyciski, czujnik i Winbond dzialaja przy domyslnym MSI okolo 2,1 MHz.

## Zaimplementowane profile

Docelowy obraz do wgrywania jest budowany z konfiguracji `Release` i opcja
`-Os`. Wersja z 20.08.2026 po dodaniu trybu STOP zajmuje 120948 B kodu i
520 B danych inicjalnych; bufory oraz stan aplikacji zajmuja 14272 B RAM. Konfiguracja `Debug` pozostaje
dostepna do diagnostyki, ale nie jest obrazem zalecanym do dlugiego testu baterii.

### Bateria

- SYSCLK: MSI range 5, okolo 2,097 MHz.
- HSE, PLL i HSI16 sa wylaczane, gdy nie sa potrzebne.
- SysTick pracuje z czestotliwoscia 100 Hz, zachowujac jednostke czasu HAL w ms.
- Gdy LCD lub menu sa aktywne, rdzen wykonuje zwykly `WFI` i budzi sie co 10 ms.
  Zachowuje to debounce, odliczanie i reakcje przyciskow bez zmian.
- Po wygaszeniu LCD firmware przechodzi do trybu `STOP` z regulatorem low-power,
  wlaczonym ULP oraz fast wake-up. SysTick jest wtedy zatrzymany.
- RTC Wakeup Timer wybudza rdzen co 1 s. Po wybudzeniu czas HAL jest uzupelniany
  o czas snu, a logger sprawdza termin kolejnego pomiaru.
- PA0 i PA1 sa zrodlami EXTI na obu zboczach. Przerwania tylko budza rdzen;
  wszystkie akcje nadal pochodza z jednej maszyny stanow `templogpro_buttons`.
- Po pobudce przyciskiem STOP jest blokowany przez 250 ms, aby debounce i pelne
  zwolnienie przycisku zostaly obsluzone przy aktywnym SysTick.
- Gleboki sen nie jest uruchamiany podczas aktywnego USB, zapisu Winbonda,
  odbudowy statystyk, akcji menu ani krotkiego impulsu LCD/LED.
- Po wygaszeniu cyfr LCD jest przeliczany co 600 ms zamiast co 250 ms. Pomiar
  wymusza natychmiastowe pokazanie impulsu serca, wiec wskazanie nie jest opoznione.
- Odswiezenie konfiguracji i statystyk w tle jest wykonywane co 5 s na baterii
  oraz co 1 s podczas aktywnego USB.
- Zielona dioda po pomiarze ma impuls 120 ms na baterii i 600 ms na USB.
  Czerwona sygnalizacja alarmu pozostaje 100 ms co 1,1 s.
- Przy braku USB kolejne sprawdzenie linii PA11/PA12 jest wykonywane co 5 s.
  Aktywny stos USB nie jest spowalniany, aby zachowac stabilna enumeracje MSC/CDC.
- Na USB ekran domowy pokazuje `USB` i duza strzalke.
- Na baterii pelny ekran pozostaje aktywny przez 10 s od ostatniej akcji.
- Po 10 s cyfry sa wygaszane, ale kontroler LCD nadal pokazuje serce, status CHECK/X,
  kierunek zapamietanych przekroczen oraz STOP, gdy logger nie pracuje.
- Pierwsze krotkie nacisniecie budzi pelne menu i nie wykonuje dodatkowej akcji.
- ADC1, VREFINT i HSI16 sa wylaczane po kazdym odczycie VDD.
- PA11 i PA12 przechodza w tryb analogowy po zatrzymaniu USB.
- Winbond W25X40CL przechodzi do Power-down (`B9h`) po kazdej zakonczonej
  operacji. Przed kolejnym odczytem lub zapisem jest budzony komenda `ABh`
  z zachowaniem czasu `tRES1`. Sterownik budzi pamiec rowniez po resecie MCU,
  poniewaz sam reset mikrokontrolera nie musi resetowac zewnetrznego Winbonda.

### USB

- HSE 8 MHz, PLL x12, PLLDIV3.
- PLL VCO: 96 MHz.
- SYSCLK: 32 MHz.
- USB: 48 MHz.
- SysTick: 1 kHz.
- Po zaniku aktywnosci magistrali stos USB jest odlaczany, a firmware wraca do profilu bateryjnego.

### Start po zaniku zasilania

- Odbudowa statystyk z historii Winbonda nie blokuje juz funkcji inicjalizujacej.
- Historia jest czytana w tle porcjami po maksymalnie 32 rekordy.
- Podczas odbudowy LCD pokazuje przesuwajacy sie srodkowy segment i migajaca
  klepsydre, a aplikacja nadal obsluguje USB, przyciski i harmonogram pomiarow.
- Po zakonczeniu skanu ekran automatycznie wraca do normalnego widoku loggera.
- RTC i LCD startuja natychmiast z LSI, bez blokujacego oczekiwania na LSE.
- LSE uruchamia sie w tle i nie jest przelaczany na zrodlo RTC, dlatego nie
  powoduje ponownej inicjalizacji LCD ani zmiany konfiguracji przyciskow.
- Po uzyskaniu LSERDY TIM10 zlicza impulsy LSE podczas 16 okresow RTC, wyznacza
  rzeczywista czestotliwosc LSI i koryguje preskalery oraz RTC_CALR.
- TIM10 zostaje wylaczony po pomiarze, a wynik jest zachowywany w rejestrach
  backupowych RTC i odtwarzany przy kolejnym resecie.

## Kalibracja RTC zweryfikowana na urzadzeniu

- zmierzona czestotliwosc LSI: 40853 Hz
- ustawiony produkt preskalerow: 40836
- RTC_PRER: `0x007A014B`
- RTC_CALR: `0x000001B4`, korekta ujemna 436 impulsow na okno 2^20
- po kalibracji zegar TIM10 jest wylaczony w RCC_APB2ENR
- po resecie wynik ma stan `RESTORED` i nie jest wykonywany ponowny pomiar
- PA0/PA1 pozostaja wejsciami z pull-up, a kontroler LCD pozostaje gotowy

Plik `TIME.TXT` udostepnia pola `RTC_CALIBRATION` i `RTC_LSI_HZ`. Zmierzona
czestotliwosc jest cecha konkretnego egzemplarza i moze zmieniac sie z
temperatura oraz napieciem; wartosc 40853 Hz nie jest stala dla wszystkich MCU.

## Zalozenia wymagajace dalszej weryfikacji

- Na PCB nie zostal potwierdzony osobny GPIO wykrywajacy VBUS.
- Firmware nie rozpoznaje juz USB na podstawie VDD. Przed uruchomieniem stosu sprawdza rezystory pull-down hosta na PA11/PA12, a nastepnie potwierdza polaczenie zdarzeniami USB RESET, SETUP i SOF.
- Brak enumeracji lub zanik aktywnosci magistrali powoduje wylaczenie USB, HSE i PLL oraz powrot do profilu bateryjnego.
- Kabel z samym zasilaniem, bez aktywnego hosta USB, nie uruchomi trybu dysku.
- Nie zmierzono jeszcze rzeczywistego pradu w trybie bateryjnym.
- Test z interwalem 10 s zapisujacy 7913 pomiarow zakonczyl sie bez bledow
  czujnika i rekordow. Szczegoly sa w `docs/battery_run_24h_2026-08-17.md`.
- Zmierzony przez uzytkownika spadek napiecia baterii do okolo 2.1 V przy USB
  wymaga weryfikacji toru zasilania. Nie wolno interpretowac go jako normalnego
  skutku przelaczenia SYSCLK na 32 MHz.

## Test na urzadzeniu

1. Uruchomic urzadzenie z podlaczonym USB i sprawdzic dysk oraz port CDC.
2. Odlaczyc USB, pozostawiajac baterie.
3. Potwierdzic dzialanie LCD, przyciskow i dalsze zapisy co ustawiony interwal.
4. Ponownie podlaczyc USB i potwierdzic enumeracje bez resetowania danych.
5. Po 10 s bezczynnosci potwierdzic natychmiastowe wybudzenie pierwszym
   nacisnieciem PA0 lub PA1 bez zmiany karty.
6. W trybie RUN potwierdzic kolejny zapis po ustawionym interwale mimo wygaszenia LCD.
7. Podlaczyc USB do uspionego urzadzenia. Wykrycie hosta moze trwac do okolo 5 s.
8. Zmierzyc prad baterii przy aktywnym menu oraz po przejsciu do STOP.
