> **Uwaga (2026-10-11):** dokument historyczny z 2026-10-09, częściowo nieaktualny (nazwa, wersja, priorytety). Aktualna dokumentacja: `DOKUMENTACJA.md`.

# TempLogPro - dokument przekazania projektu

Stan dokumentu: 2026-10-09.  Ten plik jest punktem wejścia dla osoby lub AI,
która przejmuje projekt. Opisuje stan faktyczny, a nie listę życzeń. Najpierw
czytaj ten dokument, a dopiero potem szczegółowe pliki w `docs/`.

## 1. Cel projektu

TempLogPro to rozbudowane firmware dla oryginalnego rejestratora temperatury
TempTale Ultra. Urządzenie zachowuje fizyczny sprzęt oryginalnego loggera,
ale ma własne oprogramowanie dla mikrokontrolera STM32L152. Firmware:

- mierzy temperaturę i zapisuje pomiary wraz ze zdarzeniami do zewnętrznej
  pamięci Winbond;
- obsługuje limity temperatury, alarm przekroczenia oraz zdarzenie powrotu do
  zakresu;
- ma fizyczne przyciski START i STOP oraz segmentowy LCD;
- pracuje z baterii CR3450 oraz z USB;
- po USB udostępnia dysk Mass Storage z `TEMPLOGPRO.HTML`, `DATA.CSV`,
  `README.TXT` i `POWER.TXT` oraz port CDC do Web Serial;
- generuje w przeglądarce raporty krótkie, standardowe i pełne, z wykresem,
  zdarzeniami, alarmami i danymi pracy.

Najważniejsza zasada projektu: nie usuwaj surowych danych z Winbond ani nie
wykonuj mass erase mikrokontrolera bez jawnej zgody użytkownika i nowej kopii
zapasowej.

## 2. Lokalizacja i co należy przenieść

Ten folder jest kompletną kopią projektu przeznaczoną do przeniesienia:

```text
ProjektTempTaleCodex/
  Src/             kod firmware
  Inc/             nagłówki projektu
  Drivers/         STM32Cube / HAL
  Middlewares/     stos USB
  Startup/         plik startowy MCU
  Release/         aktualny obraz .elf/.bin/.hex i artefakty kompilacji
  tests/           testy statyczne i testy panelu HTML
  docs/            dokumentacja szczegółowa
  diagnostics/     osobny firmware i wyniki diagnostyki modułów
  backup/          kopie firmware oraz zrzuty pamięci; nie usuwać
  tools/           skrypty pomocnicze
  tmp/             narzędzia tymczasowe, m.in. skrypty GDB
```

Projekt jest skonfigurowany dla STM32CubeIDE. Pliki `.project`, `.cproject`,
linker script i `TempLogPro Debug.launch` są częścią projektu i powinny
pozostać przy przenoszeniu.

## 3. Sprzęt potwierdzony diagnostycznie

| Blok | Ustalenie | Stan pewności |
| --- | --- | --- |
| MCU | STM32L1, device ID `0x427`, Cortex-M3, 256 KiB Flash; projekt używa targetu STM32L152RC | potwierdzone |
| Temperatura | I2C `0x48`, PB6=SCL, PB7=SDA; zgodność z TMP102/TMP112 lub kompatybilnym układem | interfejs potwierdzony, model niepewny |
| Pamięć danych | Winbond `EF 30 13`, rodzina W25X40, 4 Mbit = 512 KiB | potwierdzone |
| Winbond SPI | SPI1: PA4=CS, PA5=SCK, PA6=MISO, PA7=MOSI | potwierdzone przez JEDEC |
| Przyciski | PA0=START, PA1=STOP, aktywne stanem niskim | potwierdzone w firmware; fizyczne zachowanie sprawdzane w użyciu |
| Diody | PC0 zielona, PB8 czerwona | potwierdzone |
| USB | PA11/PA12; Windows widzi Mass Storage + CDC | potwierdzone |
| SWD | PA13/PA14 | potwierdzone |
| RTC | używa LSI i RTC Wakeup Timer | potwierdzone |
| LCD | kontroler LCD STM32, szkło segmentowe, dokładna mapa kilku ikon nie jest jeszcze potwierdzona | częściowo potwierdzone |

Szczegóły badania modułów są w `docs/module_probe_findings_2026-09-19.md`.

## 4. Aktualny firmware i najważniejsze pliki

W interfejsie i plikach USB widnieje wersja `0.8.13-usb-modes`. Aktualny obraz
do wgrania jest w `Release/TempLogPro.bin`, `.hex` i `.elf`.

| Plik | Rola |
| --- | --- |
| `Src/main.c` | start modułów i main loop (od 2026-10-09 sterowniki są w `board.c` i `drv_*.c`, patrz `docs/claude_handover_2026-10-09.md`) |
| `Src/templogpro_app.c` | logika aplikacji, menu LCD, alarmy, akcje START/STOP/CLEAR |
| `Src/templogpro_logger.c` | rekordy pomiarów, zdarzenia, statystyki, formatowanie logiczne loggera |
| `Src/templogpro_sensor.c` | obsługa czujnika I2C i one-shot/shutdown |
| `Src/templogpro_config.c` | zapis/odczyt CONFIG.TXT i trwała konfiguracja |
| `Src/templogpro_usb_files.c` | pliki wirtualnego dysku i cały osadzony panel HTML/JS/CSS |
| `Src/st_usb/msc/usbd_msc.c` | CDC, MSC oraz parser komend Web Serial |
| `Src/templogpro_power.c` | wybór profilu zegara/zasilania |
| `Inc/templogpro_hw_bridge.h` | maski ikon, przez które aplikacja steruje LCD |
| `Inc/templogpro_storage_layout.h` | format i rozmieszczenie danych Winbond |

Przełącznik `FIRMWARE_MODE` i stare tryby diagnostyczne usunięto 2026-10-09;
obrazy diagnostyczne buduje się osobnymi skryptami w `tools/`.

## 5. Co zostało wykonane

### Funkcje loggera i danych

- Pomiary temperatury są zapisywane jako rekordy, a normalny pomiar ma kod
  `M000`.
- Zdarzenia obejmują co najmniej start, stop/pauzę, wznowienie, alarmy i błędy.
- Przekroczenie progu tworzy alarm; powrót temperatury do dozwolonego zakresu
  również jest rejestrowany jako osobne zdarzenie.
- Pamięć ma pojemność około 42 284 wpisów. Panel pokazuje liczbę wolnych
  wpisów oraz procent wolnej pamięci.
- Raporty pokazują minimum, maksimum, średnią, progi i porównanie wyniku z
  progiem, zakresy pracy oraz łączny czas pracy.
- Zdarzenia alarmowe są wydzielone nad zwykłą tabelą zdarzeń.

### USB i panel HTML

- Urządzenie wystawia jednocześnie MSC i CDC ACM.
- `TEMPLOGPRO.HTML` jest całkowicie offline. Do konfiguracji używa Web Serial w
  Chrome/Edge; nie wymaga serwera ani Internetu.
- Panel ma język polski i angielski, zmianę jednostki C/F, wybór rodzaju
  raportu oraz widoczne statusy loggera.
- Konfiguracja obejmuje interwał, limity, tryb baterii, LED, ekran startowy,
  tryb pracy po USB oraz wybór ekranów LCD dostępnych w menu.
- Zapis konfiguracji przechodzi przez walidację firmware, a nie bezpośredni
  zapis przeglądarki do Winbond.
- Pełny raport zawiera dodatkowo surowe pomiary; raport zwykły zawiera dane
  pracy, alarmy i zdarzenia bez wszystkich próbek.

Pełny protokół CDC jest w `docs/usb_cdc_config_protocol.md`.

### LCD i przyciski

- Menu ma 12 konfigurowalnych kart: temperatura, stan, minimum, maksimum,
  średnia, dolny/górny próg, czas pracy, rekordy, interwał, bateria i LED.
  Ekran temperatury zawsze pozostaje dostępny.
- Lista kart jest trwała w konfiguracji v4 jako `lcd_menu_mask`.
- Nawigacja fizycznymi przyciskami pomija ukryte karty. Ekran startowy, jeżeli
  został ukryty, bezpiecznie wraca do temperatury.
- Dłuższe przytrzymanie wywołuje odliczanie START, STOP albo CLEAR. Zwolnienie
  przycisku lub zmiana przycisku w dowolnej cyfrze `r5..r0` anuluje akcję;
  nie wykonuje restartu ani formatowania.
- START/STOP/CLEAR z CDC najpierw anulują lokalne odliczanie, więc nie mieszają
  się z przyciskami.

### Energia i tryby pracy

- Na baterii firmware przechodzi w STOP po bezczynności; RTC budzi układ co
  sekundę, aby pilnować harmonogramu. Ekran po 10 s wygasza cyfry i pozostawia
  ikony stanu.
- Winbond po operacji otrzymuje `B9h` (deep power-down), a SPI1 jest bramkowane.
- ADC/VREFINT i pomocnicze zegary są włączane tylko na czas odczytu.
- Czujnik I2C działa w shutdown/one-shot: inicjalizacja ustawia shutdown,
  pomiar uruchamia one-shot, odczyt następuje po gotowości, po czym czujnik
  wraca do shutdown.
- Konfiguracja `usb_logging=1` pozwala logować zasilając urządzenie tylko USB.
  `usb_logging=0` robi z USB tryb raportów i ustawień: logger zostaje zatrzymany
  i START po USB jest odmówiony z błędem `E007`.

### Integralność pamięci i naprawa błędu `Err`

Zaobserwowano realny błąd zapisu do Winbond: niedokończony rekord blokował
kolejne zapisy i aplikacja przechodziła w `ERROR` / `Err`. Przed naprawą
wykonano pełną kopię 512 KiB pamięci. Następnie naprawiono wyłącznie sektor
`0x7000`: zachowano pierwsze 492 bajty (41 prawidłowych rekordów), skasowano
wadliwy dalszy slot i zweryfikowano odczyt. Zachowano pierwszych 1405
poprawnych rekordów; nie formatowano całej pamięci.

W kodzie usunięto również ryzyko współdzielenia SPI: odczyt plików MSC działa
w `USB_LP_IRQHandler`, a zapis loggera w main loop. Na czas transakcji
Winbond firmware maskuje tylko IRQ20 USB, zachowując SysTick i przyciski.
To jest poprawka ważnego wyścigu programowego, ale nie dowodzi, że był jedyną
przyczyną historycznego uszkodzenia rekordu.

Dokładne dane odzyskania: `docs/lcd_usb_followup_2026-09-22.md`.

## 6. Potwierdzone wyniki pomiarów i testów

Pomiary użytkownika na baterii:

| Sytuacja | Odczyt |
| --- | --- |
| Bateria, ekran wygaszony | około 0,07 mA |
| Bateria, ekran aktywny | około 0,35 mA |
| Krótki pik | około 0,5 mA |
| Wcześniejszy pomiar spoczynku | około 60-90 uA, zależnie od obrazu testowego i stanu LCD |
| Aktywność pomiaru | około 0,97 mA |
| Bateria przy podłączonym USB w normalnym firmware | około 14-16,35 mA oraz zgłoszony spadek napięcia do około 0,7 V |

Wyniki oprogramowania:

- testy USB/sensor/config: 229 asercji;
- testy LCD: 111 asercji;
- testy akcji/menu/pojemności: 218 asercji;
- testy osłony IRQ dla SPI/USB: 36 asercji;
- testy button FSM i cztery warianty testów idle: po 63 sprawdzenia;
- test panelu HTML w Edge/Chromium na szerokościach 1440 px i 390 px.

To są dobre testy regresyjne kodu, lecz nie zastępują testu fizycznego LCD,
zasilania i zapisu Winbond pod realnym obciążeniem.

## 7. Rzeczy nierozwiązane - priorytety

### Priorytet 1: pobór z baterii po podłączeniu USB

To jest najważniejszy nierozwiązany problem sprzętowo-systemowy. Użytkownik
zmierzył około 14-16 mA z baterii przy normalnej pracy USB i chwilowy spadek
napięcia baterii nawet do około 0,7 V. Jednocześnie pomiar BAT+ bez baterii
przy USB dał 0 V, czyli USB nie ładuje prostą drogą zacisku baterii.

Nie wolno twierdzić, że problem został rozwiązany. Odczyt VDD MCU około 3,3 V
nie jest pomiarem napięcia baterii. Firmware może wpływać na pobór, ale nie
jest jeszcze znany tor zasilania PCB, jego element separujący ani źródło
dużego prądu. Szczegóły i procedura testowa: `docs/usb_power_path_test_2026-09-19.md`.

Następny rozsądny krok:

1. Zmierzyć amperomierzem wyłącznie gałąź baterii: bez USB, po USB w trybie
   raportowym i po USB podczas kopiowania pliku.
2. Porównać normalny firmware z obrazem `USB power path diagnostic`, który
   wykrywa kabel, ale nie uruchamia USB/PLL/stosu USB.
3. Jeżeli różnica pozostaje w obrazie diagnostycznym, szukać hardware
   power-path/backfeed. Jeżeli znika, profil USB firmware jest bezpośrednią
   przyczyną obciążenia do dalszej optymalizacji.
4. Nie podłączać baterii, gdy znów obserwowany jest spadek do 0,7 V, dopóki
   nie zostanie ustalona przyczyna.

### Priorytet 2: prawdziwa mapa ikon LCD

Cyfry, temperatura, stopień C, check, klepsydra i część ikon są działające.
Mapa segmentów MIN/MAX/AVG oraz niektórych kropek nie została fizycznie
zweryfikowana. W kodzie obecne adresy ikon są hipotezą, dlatego mogą wyświetlać
nie ten symbol lub dawać podwójną kropkę.

W firmware istnieje jednorazowy test RAM-only `g_lcd_probe_index` w
`Src/templogpro_app.c`. Działa tylko z USB i wygasa po 180 s; nie zapisuje
konfiguracji i nie zatrzymuje loggera. Test kandydata 091 odpowiada RAM6 bit15,
który aktualnie nazywa się `MAX`. Zdjęcia z sesji nie dały jednoznacznej
identyfikacji symbolu, więc nie należy na ich podstawie przesuwać mapy na
chybił-trafił.

Metoda dalszej pracy:

1. Zbudować/uruchomić pojedynczy test dla jednego bitu.
2. Zrobić ostre zdjęcie LCD z widoczną wyłącznie badaną ikoną i jej numerem.
3. Zapisać wynik jako `RAMx bit y -> nazwa fizycznego znaku`.
4. Dopiero po co najmniej dwóch potwierdzeniach poprawić `SEG_MIN`, `SEG_MAX`,
   `SEG_AVG` i ewentualne kropki w `Src/main.c`.

Nie dodawać na LCD napisów tekstowych typu `SEC`, `Lo`, `Hi`, `rEC`, `bAt`,
`dur`. Użytkownik wyraźnie odrzucił takie napisy: interfejs ma używać
oryginalnych segmentów i ikon szkła.

### Priorytet 3: dalsza obserwacja trwałości Winbond

Po naprawie rekordy ponownie przyrastały na USB, ale jedna naprawa nie jest
dowodem długookresowej niezawodności. Należy wykonać kontrolowany test:

- logowanie na baterii i na USB przez dłuższy czas;
- okresowe otwieranie `TEMPLOGPRO.HTML` i `DATA.CSV` podczas logowania;
- porównanie liczby rekordów i CRC;
- natychmiastowy pełny zrzut Winbond, jeśli znów pojawi się `Err`;
- nigdy nie formatować przed kopią surowej pamięci.

### Priorytet 4: test realnego UX

Przed uznaniem wersji za wydanie należy ręcznie sprawdzić:

- START, STOP i CLEAR z puszczeniem przycisku na każdej cyfrze odliczania;
- zmianę wszystkich checkboxów menu w HTML i zapis/odczyt konfiguracji;
- ekran startowy ustawiony na kartę ukrytą w masce;
- logowanie z `usb_logging=1` i blokadę logowania z `usb_logging=0`;
- alarm górny, alarm dolny oraz powrót do zakresu;
- raporty w jasnym i ciemnym motywie oraz druk do PDF.

## 8. Konfiguracja i zgodność danych

Trwała konfiguracja ma obecnie format v4 o rozmiarze 44 B. Nowe pola:

```text
usb_logging=0|1
lcd_menu_mask=0..4095
```

`lcd_menu_mask` używa 12 bitów; bit 0 (temperatura) jest obowiązkowy.
Konfiguracje v1-v3 podczas odczytu domyślnie włączają wszystkie karty menu.
Starszy firmware może nie umieć odczytać zapisu v4, dlatego przed cofnięciem
firmware zachowaj kopię konfiguracji i całej pamięci.

Przykładowe komendy CDC:

```text
PING
GET CONFIG
GET POWER
LOGGER STATUS
LOGGER START
LOGGER PAUSE
LOGGER CLEAR
LOGGER CLEAR CONFIRM
```

Konfigurację wielopolową wysyła się przez `CONFIG BEGIN` / `CONFIG END`.
Maksymalna linia ma 96 znaków. Nie zakładaj, że port COM jest wolny, gdy
`TEMPLOGPRO.HTML` jest połączony przez Web Serial - przeglądarka może trzymać port.

## 9. Bezpieczny build i wgrywanie

Kompilacja była wykonywana przez STM32CubeIDE (Release, `-Os`). Na komputerze z
CubeIDE sprawdź ścieżki do `arm-none-eabi-gcc`, `make`, STM32CubeProgrammer i
ST-LINK GDB server.

Ogólny przebieg:

1. Zbuduj konfigurację Release.
2. Uruchom testy Python oraz `node tests/usb_modes_panel.cjs`.
3. Podłącz urządzenie po SWD, najlepiej najpierw bez baterii i z USB.
4. Wgraj wyłącznie `Release/TempLogPro.bin` pod `0x08000000` z weryfikacją.
5. Nie wybieraj Mass Erase, nie zmieniaj Option Bytes i nie kasuj zewnętrznego
   Winbond w ramach zwykłego wgrywania firmware.
6. Po restarcie sprawdź enumerację USB, `PING`, `GET CONFIG`, `LOGGER STATUS`
   oraz pierwszą próbkę.

ST-Link może wymagać połączenia `under reset` / hardware reset. Gdy układ
przestaje odpowiadać, nie traktuj tego automatycznie jako uszkodzenia firmware:
sprawdź zasilanie i połączenia NRST/SWD przed kasowaniem czegokolwiek.

## 10. Kopie zapasowe i czego nie dotykać

Najważniejsze kopie:

| Lokalizacja | Zawartość |
| --- | --- |
| `backup/before_lcd_selector_20260922_004956/winbond_before_slot_repair.bin` | pełny zrzut 512 KiB sprzed punktowej naprawy; SHA-256 `b5271a5db643d65b917ae304e78701ed1ae3ff56aa17c420b3701e9fbfb97372` |
| `backup/before_lcd_selector_20260922_004956/failed_sector_7000.bin` | sektor z niedokończonym rekordem |
| `backup/before_lcd_selector_20260922_004956/sector_7000_after_repair.bin` | sektor po naprawie |
| `backup/before_battery_flash_diag_20260921/winbond.bin` | pełny zrzut z wcześniejszego incydentu `Err` |
| `backup/main_before_module_diag_20260919_231527/` | obraz main oraz zrzuty pamięci sprzed diagnostyki modułów |
| `backup/main_before_usb_modes_20260920_043904/` | stan przed trybem USB |
| `backup/usb_modes_hardware_20260921_213142/` | pliki i surowy stan pamięci z sesji USB |

Skrypty GDB w `tmp/` są narzędziami roboczymi, nie poleceniami do uruchomienia
bez lektury. Szczególnie nie uruchamiaj ponownie skryptów naprawiających sektor
ani formatujących logger: zostały napisane dla konkretnego stanu liczników.

## 11. Dokumentacja szczegółowa

| Temat | Plik |
| --- | --- |
| Stan LCD, anulowanie akcji, odzyskanie danych, wyścig USB/SPI | `docs/lcd_usb_followup_2026-09-22.md` |
| Diagnostyka MCU, I2C i Winbond | `docs/module_probe_findings_2026-09-19.md` |
| Tryby USB i shutdown czujnika | `docs/usb_modes_and_sensor_sleep_2026-09-20.md` |
| Audyt oszczędzania baterii | `docs/battery_idle_audit_2026-08-25.md` |
| Test toru USB/bateria | `docs/usb_power_path_test_2026-09-19.md` |
| Raport 24 h na baterii | `docs/battery_run_24h_2026-08-17.md` |
| Badanie pinów Winbond | `docs/winbond_jedec_pinout_report.md` |
| Protokół USB CDC | `docs/usb_cdc_config_protocol.md` |
| Opis raportów HTML/PDF | `docs/usb_html_report.md` |
| Oryginalny plan architektury | `docs/rearchitecture_plan_2026-08-05.md` |

## 12. Moja ocena i rekomendowany kierunek

Projekt ma już solidny rdzeń: logger, raporty, interfejs USB, zabezpieczenia
konfiguracji i testy są daleko poza poziomem jednorazowego prototypu. Największe
ryzyko nie leży dziś w funkcjach HTML ani w samym algorytmie loggera, tylko w
relacji firmware z nie w pełni poznanym hardware: pobór baterii przy USB,
rzeczywiste segmenty LCD i długoterminowa odporność Winbond na zakłócone zapisy.

Najlepsza kolejność dalszej pracy to:

1. Zakończyć diagnostykę toru baterii/USB na pomiarach rozdzielających firmware
   od hardware.
2. Zmapować fizycznie brakujące ikony LCD pojedynczymi, udokumentowanymi
   testami segmentów.
3. Przeprowadzić długi test zapisu z okresowym używaniem USB i sporządzić
   automatyczną kontrolę CRC/zrzutów.
4. Dopiero później wykonywać większe zmiany UI, dalszą optymalizację USB albo
   refaktory logiki.

W ten sposób nie tracimy działających funkcji ani danych, a każde kolejne
usprawnienie ma mierzalne kryterium powodzenia.
