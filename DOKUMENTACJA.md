# TempLogPro — dokumentacja i raport z projektu

Wersja dokumentu: 2026-10-13 · firmware **1.1.0**
Autor projektu: **Brunon Kucharski** · opracowane we współpracy z **Codex (OpenAI)** i **Claude (Anthropic)**

Instrukcja obsługi dla użytkownika: [docs/INSTRUKCJA.md](docs/INSTRUKCJA.md). Sprzęt, złącze SWD i pinout: [docs/HARDWARE.md](docs/HARDWARE.md).

Ten dokument opisuje stan faktyczny projektu: co urządzenie robi, jak jest
zbudowane, jak je budować, testować i wgrywać, co zostało zrobione i co jest
jeszcze otwarte. Szczegółowe notatki z poszczególnych etapów są w `docs/`.

---

## 1. Podsumowanie

TempLogPro to własne firmware dla rejestratora temperatury zbudowanego na
sprzęcie oryginalnego loggera **TempTale Ultra** (mikrokontroler STM32L152,
segmentowy LCD, dwa przyciski, pamięć Winbond, bateria CR3450, USB).
Oryginalne oprogramowanie zostało całkowicie zastąpione.

Urządzenie:

- mierzy temperaturę w zadanym interwale (1–600 s) i zapisuje pomiary oraz
  zdarzenia w pamięci Winbond (do 42 284 wpisów);
- pilnuje dwóch progów alarmu, rejestruje przekroczenia i powrót do zakresu;
- pokazuje wartości na oryginalnym LCD (12 konfigurowalnych ekranów, tylko
  segmenty i ikony szkła, bez napisów tekstowych);
- sygnalizuje pracę sercem na LCD i zieloną diodą (heartbeat przy każdym
  pomiarze) oraz alarm krzyżykiem X i czerwoną diodą;
- pracuje miesiącami na baterii (tryb STOP, ~16 µA w spoczynku);
- po podłączeniu USB jest dyskiem z raportem `TempLogPro.html`, plikiem
  `DATA.CSV`, `README.TXT` i `POWER.TXT` oraz portem szeregowym do ustawień;
- raport HTML działa całkowicie offline w Chrome/Edge: wykres, przekroczenia,
  dane, ustawienia urządzenia przez Web Serial i eksport do PDF.

**Stan na 2026-10-13:** firmware 1.1.0 wgrane i działające na urządzeniu,
wszystkie testy automatyczne przechodzą, panel i raport PDF zaakceptowane przez
użytkownika. Najważniejszy otwarty problem: spadek napięcia baterii przy
podłączonym USB (rozdział 12).

---

## 2. Sprzęt

| Blok | Ustalenie | Pewność |
| --- | --- | --- |
| MCU | STM32L1, device ID `0x427`, Cortex-M3, 256 KiB Flash, 32 KiB RAM; target STM32L152RC | potwierdzone |
| Czujnik temperatury | I2C `0x48`, PB6 = SCL, PB7 = SDA, zgodny z TMP102/TMP112; tryb shutdown/one-shot | interfejs potwierdzony, model niepewny |
| Pamięć danych | Winbond `EF 30 13` (W25X40), 4 Mbit = 512 KiB | potwierdzone |
| SPI pamięci | SPI1: PA4 = CS, PA5 = SCK, PA6 = MISO, PA7 = MOSI | potwierdzone (JEDEC) |
| Przyciski | PA0 = START, PA1 = STOP, aktywne stanem niskim | potwierdzone |
| Diody | PC0 zielona, PB8 czerwona | potwierdzone |
| USB | PA11/PA12, Mass Storage + CDC | potwierdzone |
| SWD | PA13/PA14 (ST-Link, połączenie „under reset”) | potwierdzone |
| RTC | LSI + RTC Wakeup Timer | potwierdzone |
| LCD | kontroler LCD STM32, szkło TempTale Ultra; mapa segmentów wg `temptale-ultra_LCD.pdf` (źródło: [hackaday.io/project/189442](https://hackaday.io/project/189442-temptale-ultra-teardown-repurpose)), potwierdzona na szkle | potwierdzone |
| Zasilanie | bateria CR3450 3 V lub USB | potwierdzone |

Szczegóły badań: `docs/module_probe_findings_2026-09-19.md`,
`docs/winbond_jedec_pinout_report.md`,
`docs/temptale_ultra_sensor_research_2026-09-19.md`.

---

## 3. Obsługa urządzenia

### Przyciski

| Akcja | Działanie |
| --- | --- |
| krótko START / STOP | poprzedni / następny ekran LCD |
| przytrzymanie START 3 s | start lub wznowienie zapisu (odliczanie `r 5`…`r 0`, potem `run`) |
| przytrzymanie STOP 3 s | pauza (odliczanie `S 5`…`S 0`, potem `StP`) |
| oba przyciski 3 s na ekranie CLR | wyczyszczenie logu (`C 5`…`C 0`, potem `don`) |

Puszczenie lub zmiana przycisku w trakcie odliczania anuluje akcję na każdej
cyfrze. Polecenia z panelu przerywają lokalne odliczanie.

### Ekrany LCD (kolejność na urządzeniu)

| Nr bitu | Ekran | Ikony |
| --- | --- | --- |
| 0 | temperatura (zawsze dostępny; na USB napis `USB`) | TEMP, °C/°F |
| 15 | diody LED: `On` / `OFF`; obsługa jak na karcie 12 (zapis `led_enable`) | SUN, PROBE |
| 16 | tryb eco: na zmianę `Eco` i `On` / `OFF`; obsługa jak na karcie 12 (zapis `eco_mode`) | BATTERY |
| 12 | zegar 12 h `H:MM` (czas lokalny strefy, dwukropek miga); przytrzymanie START włącza, STOP wyłącza tryb zegarka (odliczanie `On5…On0` / `OF5…OF0`, zapis `clock_mode`; na baterii < 2,7 V odmowa) | TIME, SUN po południu (12:00–23:59) |
| 1 | stan loggera (`rdY` / `run` / `StP`) | PROBE |
| 4 | średnia | TEMP + AVG |
| 3 | maksimum | TEMP + MAX |
| 6 | próg górny | PROBE + TEMP + MAX |
| 2 | minimum | TEMP + MIN |
| 5 | próg dolny | PROBE + TEMP + MIN |
| 7 | czas pracy | klepsydra + jednostka |
| 8 | liczba zapisów | PROBE |
| 9 | interwał (sekundy) | PROBE + TIME |
| 10 | bateria (na USB `USB`); oba przyciski 3 s → OFF (przechowywanie) | bateria + % |
| 11 | kasowanie (CLR) | — |

Widoczność ekranów ustawia się w panelu (`lcd_menu_mask`, 17 bitów; konfiguracje sprzed v7 dostają zegar, sprzed v8 kartę OFF, sprzed v9 kartę trybu zegarka, sprzed v10 kartę diod, sprzed v11 kartę eco). Po 10 s bez
dotykania ekran wraca do temperatury; na baterii cyfry gasną, zostają ikony
stanu. Temperatura na LCD może być w °C albo °F (dane w pamięci zawsze w °C).

### Sygnalizacja

- **Serce na LCD + zielona dioda**: jedno wspólne mignięcie przy każdym
  zapisanym pomiarze (600 ms na USB, 250 ms na baterii).
- **Ptaszek**: brak alarmu w bieżącym logu.
- **X na LCD**: alarm w bieżącym logu (zatrzaśnięty do CLR), świeci na stałe;
  obok małe trójkąty ↑ / ↓ pokazują kierunek przekroczenia.
- **Czerwona dioda**: przy alarmie dwa krótkie błyski co 3 s.
- **Strzałki ↑/↓** na ekranie bezczynności: kierunek przekroczenia.
- **Znak STOP**: logger nie zapisuje.

Diody można wyłączyć w ustawieniach (`led_enable`).

---

## 4. Panel i raport (`TempLogPro.html`)

Plik jest generowany przez urządzenie przy każdym odczycie z dysku: szablon
z pamięci Flash + aktualne dane `DATA.CSV` + konfiguracja w JSON. Nie potrzebuje
internetu ani serwera. Ustawienia działają w Chrome/Edge (Web Serial).

### Zakładki

- **Raport** — pasek werdyktu (w zakresie / przekroczenie, z maks. i min.),
  kafelki: ostatni pomiar, minimum, średnia, maksimum, czas poza zakresem,
  liczba przekroczeń; wykres z pasem dozwolonego zakresu, liniami progów,
  min/max/średniej, oznaczeniami START/STOP/PAUZA i szarymi strefami
  „BRAK DANYCH” (przerwa w zasilaniu lub zmiana zegara); zakres Całość / 1 h /
  24 h / 7 dni / Własny oraz zaznaczanie myszą; dane pracy i podsumowanie
  przekroczeń.
- **Dane** — tabela przekroczeń (kierunek, początek, koniec, czas, ekstremum),
  zakresy pracy, zdarzenia, legenda kodów, wszystkie pomiary po 200 na stronę
  z filtrem (wszystkie / poza zakresem / zdarzenia), przycisk „Pobierz CSV”.
- **Informacje** — urządzenie, wersja, autor, pamięć, interwał, progi.
- **Ustawienia** — Połącz / Odczytaj / Zapisz, karta Loggera (Start, Stop,
  Odśwież, Wyczyść, wolna pamięć z paskiem), grupy: Pomiar i alarmy,
  Wyświetlacz i diody, Zasilanie, Ekrany w menu LCD, szczegóły techniczne.

Przełączniki w nagłówku: rodzaj raportu, Generuj PDF, motyw jasny/ciemny,
°C → °F, PL/EN.

### Raport PDF

„Generuj PDF” otwiera okno z polami **Uwagi** i **Sprawdził** (trafiają tylko
do PDF; nazwisko jest zapamiętywane w przeglądarce). Rodzaje:

- **Skrócony** — 1 strona: werdykt, kafelki, wykres, dane pracy, ramki uwag.
- **Raport** — + strona 2: przekroczenia, zakresy pracy, zdarzenia, legenda.
- **Pełny** — + wszystkie wpisy od najstarszego w 4 kolumnach, pogrupowane
  według dni (pełna pamięć ≈ 160 stron).

Każda strona ma własny nagłówek i stopkę (okres, data wygenerowania, wersja,
„Strona X z Y”); nazwa pliku PDF to `TempLogPro_<data>_<od>-<do>`. PDF obejmuje
zakres widoczny na wykresie.

### Definicje w raporcie

- **Przekroczenie** — kolejne próbki poza jednym progiem; kończy się na
  pierwszej próbce w zakresie albo przed przerwą dłuższą niż 3 interwały.
- **Czas poza zakresem** — suma czasów przekroczeń; procent liczony względem
  czasu objętego pomiarami.
- **Brak danych** — przerwa dłuższa niż 20 interwałów / 10 min albo cofnięcie
  zegara.
- **Opóźnienie alarmu** (`alarmDelay`) — przekroczenia krótsze niż opóźnienie są
  „krótkimi wyjściami”: nie wchodzą do werdyktu, kafelków i czasu poza
  zakresem, w tabeli są szare. Firmware stosuje tę samą regułę do X i czerwonej
  diody (zatrzask w `RTC_BKP4R`, kasowany przy CLR i nowym starcie); kody
  `T001` w `DATA.CSV` zostają bez zmian.
- **Najniższe napięcie baterii** (`vbatMin`) — minimum VDD od startu urządzenia,
  gdy pracowało na baterii; poniżej 2,7 V raport zaleca wymianę.

---

## 5. Pliki na dysku USB

| Plik | Zawartość |
| --- | --- |
| `TempLogPro.html` | raport i panel (alias 8.3: `TEMPLO~1.HTM`) |
| `DATA.CSV` | `Sequence,Date,Time,Temperature_C,Code,UTC_Offset` — wszystkie wpisy; Date/Time w czasie lokalnym, `UTC_Offset` np. `+02:00` |
| `README.TXT` | krótka instrukcja, autor, kody |
| `POWER.TXT` | diagnostyka zasilania i uśpienia (programowa, nie pomiar prądu); `LAST_RESET` = przyczyna ostatniego restartu |

Przyczyny restartu (`LAST_RESET` w `POWER.TXT`, pole `reset` w danych raportu,
wiersz „Ostatni restart” w zakładce Informacje): `POWER_ON` (włączenie zasilania
lub spadek napięcia), `RESET_PIN` (NRST / programator), `SOFTWARE`, `WATCHDOG`,
`POWER_DROP_CLEAR` / `POWER_DROP_START` (restart w trakcie kasowania pamięci
przez CLR/START). Przed kasowaniem firmware zapisuje znacznik w rejestrze
zapasowym RTC (`RTC_BKP2R` = `0x5A5A00xx`), a po skończonej operacji go zeruje;
znacznik zastany przy starcie oznacza przerwaną operację. Na baterii CLR/START
są odrzucane poniżej 2,7 V (minimum Winbond W25X40): przez 3 s napis `bAt`
i migające ikony baterii i X (bez ptaszka i innych ikon). Po wykrytej przerwanej operacji ikona baterii miga 5 s po starcie.
Pełny zanik zasilania (wyjęta bateria) zeruje rejestry zapasowe, więc wtedy
raport pokaże zwykłe `POWER_ON`.

Etykieta dysku: `TEMPLOGPRO`; w Windows: „TempLog Pro USB Device”, port
szeregowy „USB Serial Device (COMx)”.

### Kody w `DATA.CSV`

| Kod | Znaczenie |
| --- | --- |
| M000 | pomiar |
| T001 / T002 | pomiar powyżej / poniżej progu |
| T003 | powrót do zakresu (wyliczany w panelu) |
| S001 | błąd odczytu czujnika |
| R001 / R002 / R003 | start / wznowienie / pauza |
| R004 | ustawienie zegara |
| R005 | ochrona przed cofnięciem zegara (przy każdym starcie) |
| E001 | pominięty, uszkodzony slot (ukrywany w panelu) |

---

## 6. Konfiguracja

Format v11 (96 B, układ jak v6; bajt 60 `clock_mode`, 61 `eco_mode`, 51 bity 16+ maski menu), dwie kopie w Winbond (A `0x7F000`, B `0x00000`), wygrywa
poprawna kopia z wyższym numerem `generation`. Zapis zawsze nadpisuje kopię
nieaktualną, więc przerwany zapis zostawia poprzednie ustawienia.

| Klucz | Zakres | Opis |
| --- | --- | --- |
| `interval_seconds` | 1–600 | interwał pomiaru |
| `temperature_low_c` / `temperature_high_c` | °C | progi alarmu |
| `battery_mode` | 0/1 | oszczędzanie energii na baterii |
| `usb_logging` | 0/1 | 1 = pomiary także na USB, 0 = USB tylko do raportów (START odmawia `E007`) |
| `lcd_mode` | 0–12 | ekran po starcie |
| `lcd_menu_mask` | 0–131071 | widoczne ekrany (bit 0 zawsze, bit 12 = zegar, 13 = OFF, 14 = tryb zegarka, 15 = diody, 16 = eco; bity 13 i 14 nieużywane - dawne karty OFF i trybu zegarka) |
| `alarm_delay_min` | 0–240 | opóźnienie alarmu: X / czerwona dioda / przekroczenie w raporcie dopiero po tylu minutach ciągłego wyjścia poza zakres (bajt 57 konfiguracji) |
| `eco_mode` | 0/1 | tryb eco: na baterii po 10 s bezczynności LCD całkiem wyłączone, bez serca i diod; budzenie tylko na pomiar |
| `led_enable` | 0/1 | diody |
| `clock_mode` | 0/1 | tryb zegarka: zatrzymany logger na baterii pokazuje H:MM (bajt 60 konfiguracji; starsze zapisy mają tam 0) |
| `temperature_unit` | C/F | jednostka na LCD |
| `pdf_enable` | 0/1 | pole historyczne |
| `tz_name` | do 31 znaków | strefa IANA do raportu, np. `Europe/Warsaw` |
| `tz_offset_min` | −720…840 (co 15) | przesunięcie czasu zimowego (standardowego) od UTC |
| `tz_dst_min` | 0…120 | przesunięcie czasu letniego (0 = bez zmiany czasu) |
| `tz_dst_start` / `tz_dst_end` | `miesiąc.tydzień.dzień.minuta` | początek czasu letniego (czas zimowy) i zimowego (czas letni); tydzień 5 = ostatni, dzień 0 = niedziela, np. `3.5.0.120` |

Starsze formaty v1–v4 są czytane automatycznie. Starsze firmware nie odczyta
v5 — przed cofnięciem firmware zrób zrzut pamięci.

---

### Czas i strefa czasowa

Zegar urządzenia i wszystkie rekordy są w **UTC**, więc log nigdy się nie
cofa, także przy zmianie czasu na zimowy. Czas lokalny liczy się dopiero przy
tworzeniu `DATA.CSV` i raportu: przesunięcie standardowe + ewentualne
przesunięcie letnie według reguły „n-ta niedziela miesiąca o godzinie”
(jak POSIX TZ, działa też na półkuli południowej). Reguła jest zapisana
w konfiguracji, więc zmiany czasu w kolejnych latach liczą się same.

- **Wstaw czas i strefę z komputera** (Ustawienia → Czas i strefa czasowa)
  wypełnia pola strefą komputera i regułą z przeglądarki; **Zapisz w urządzeniu**
  wysyła wszystko naraz, a przy zaznaczonym „Ustaw zegar przy zapisie” także
  czas UTC (`SET TIME`). Panel odczytuje potem zegar (`GET TIME`) i pokazuje go
  na żywo z różnicą względem komputera.
- **Ręcznie**: wybór strefy, daty i godziny lokalnej oraz własnych momentów
  przejścia na czas letni i zimowy (np. gdy reguła przeglądarki nie pasuje).
- **Raport**: znaczek strefy (np. `UTC+02:00 Europe/Warsaw · czas letni`),
  strefa w opisie wykresu i nagłówku PDF, fioletowa linia „CZAS LETNI /
  CZAS ZIMOWY” na wykresie i zdarzenia `Z001` / `Z002` w tabeli zdarzeń.
- Godzina powtarzana przy przejściu na czas zimowy ma w CSV dwa różne
  `UTC_Offset`, więc kolejność wpisów jest jednoznaczna.
- Wpisy sprzed firmware 1.0.0 (bez strefy) zostały zapisane w czasie lokalnym
  i po ustawieniu strefy wyświetlają się przesunięte o jej przesunięcie.
- Zegar zatrzymuje się bez zasilania i pracuje na wewnętrznym oscylatorze
  LSI, więc po utracie zasilania trzeba go zsynchronizować ponownie.

## 7. Protokół USB CDC (Web Serial)

Linie ASCII zakończone `\n`, maksymalnie 96 znaków. Najważniejsze polecenia:

```text
PING                      -> OK PING
GET CONFIG                -> CONFIG BEGIN ... CONFIG END
GET POWER                 -> POWER STATUS ...
GET TIME                  -> TIME utc=YYYY-MM-DD HH:MM:SS local=YYYY-MM-DD HH:MM:SS offset=+120
LOGGER STATUS             -> LOGGER STATUS app=RUN logger=LOGGING records=... capacity=... interval=... min=... max=... avg=...
LOGGER START | LOGGER PAUSE  (PAUSE anuluje też oczekiwanie na opóźniony start)
LOGGER START IN <sekundy> | LOGGER START AT YYYY-MM-DD HH:MM:SS (UTC)
                          -> OK LOGGER WAIT; LOGGER STATUS dopisuje start_at=YYYY-MM-DD HH:MM:SS
LOGGER CLEAR CONFIRM      (samo LOGGER CLEAR jest odrzucane, E006); po OK urządzenie restartuje się po 0,8 s,
                          a panel przeładowuje stronę po 8 s (świeży dysk z pustym raportem)
POWER OFF                 -> OK POWER OFF (zatrzymuje zapis; OFF po odłączeniu USB; LOGGER STATUS dopisuje off=ARMED)
POWER OFF CANCEL          -> OK POWER OFF CANCEL
SET TIME YYYY-MM-DD HH:MM:SS     (UTC; odrzucane, gdy wcześniejsze niż ostatni zapisany pomiar - po CLR dowolne)
CONFIG BEGIN / key=value ... / CONFIG END   -> OK CONFIG
```

Błędy: `E001` nieznane polecenie, `E002` zły argument, `E003` niedozwolone
w tym stanie, `E004` błąd zapisu, `E005` błąd RTC, `E006` wymagane
potwierdzenie, `E007` logowanie na USB wyłączone. Pełny opis:
`docs/usb_cdc_config_protocol.md`.

---

## 8. Pamięć danych (Winbond, format v3)

| Adres | Zawartość |
| --- | --- |
| `0x00000` | konfiguracja, kopia B |
| `0x01000` / `0x02000` | metadane loggera, sektory A/B (sloty po 64 B) |
| `0x03000`–`0x7EFFF` | rekordy: 124 sektory × 341 × 12 B = 42 284 |
| `0x7F000` | konfiguracja, kopia A |

Rekord (12 B): znacznik czasu u32, temperatura ×256 i16, flagi u16, numer
u16, CRC16-CCITT. Rekordy są źródłem prawdy: koniec logu wyznacza skan przy
starcie, metadane zapisywane są tylko przy zmianie stanu. Reset lub utrata
zasilania w dowolnym momencie nie blokuje loggera (sprawdzone testem na 818
punktach przerwania zasilania). Opis: `docs/storage_format_v3.md`.

---

## 9. Energia

| Sytuacja | Pomiar użytkownika |
| --- | --- |
| tryb najbardziej oszczędny (spoczynek) | ~16 µA |
| bateria, ekran wygaszony | ~0,07 mA |
| bateria, ekran aktywny | ~0,35 mA |
| pomiar | ~0,97 mA |
| bateria przy podłączonym USB | 14–16 mA, spadek napięcia baterii z 3,0 V do ~2,1 V (problem otwarty) |

Rozwiązania w firmware: STOP z budzeniem z RTC co 1 s w czasie pomiarów,
a przy zatrzymanym loggerze co 60 s (co 3 s, gdy miga dioda alarmu);
przyciski i podłączenie kabla USB (przerwanie na D+) budzą od razu;
wyświetlacz odświeżany ~75 Hz zamiast ~300 Hz; Winbond w deep
power-down (`B9h`) i bramkowany SPI, ADC tylko na czas odczytu, czujnik
w shutdown/one-shot, harmonogram pomiarów trzymający siatkę od START,
heartbeat skrócony na baterii.

**Tryb zegarka** (`clock_mode=1`): na baterii wygaszony ekran pokazuje `H:MM`
ze stałym dwukropkiem i ikonami TIME/SUN (bez X, STOP, ptaszka). Zatrzymany
logger budzi się z RTC dokładnie na pełną minutę (`60 − sekunda`); podczas
zapisu zegar odświeża się przy zwykłym budzeniu co 1 s, a serce miga z diodą.

**Tryb eco** (`eco_mode=1`): na baterii, gdy ekran zasypia (10 s bez przycisków),
kontroler LCD jest wyłączany (`LCD_CR = 0`), serce i obie diody są wygaszone,
a w RUN timer RTC budzi procesor dopiero na następny pomiar
(`next_measurement − teraz`, maks. 60 s) zamiast co 1 s; zatrzymany logger co
60 s. Przycisk włącza LCD z powrotem (`templogpro_hw_lcd_init`). Eco ma
pierwszeństwo przed trybem zegarka; przy USB jest nieaktywne.

**Opóźniony start**: czas startu (UTC) w RAM i w `RTC_BKP3R`, więc przeżywa
reset. Czekając, logger budzi się na sam start oraz na każdą zmianę cyfr
odliczania (`M:SS` co 1 s poniżej 21 min, potem co pełną minutę; w eco co
najwyżej co 60 s). Przy słabej baterii start jest ponawiany co minutę.

**Ostrzeżenie baterii**: na baterii poniżej 2,7 V ikona baterii świeci stale
na zwykłych ekranach i przy wygaszonym ekranie.

**Odświeżanie dysku**: po zmianie plików (START/STOP/CLR, zapis ustawień, OFF)
dysk odpowiada „brak nośnika” na dwa zapytania TEST UNIT READY, więc Windows
odrzuca pamięć podręczną i montuje go od nowa.

**OFF (przechowywanie)**: `POWER OFF` przez USB albo ekran baterii na LCD
(oba przyciski, odliczanie `o 5…o 0`, napis `OFF` przez 1,2 s) uzbraja tryb, po odłączeniu
kabla firmware zatrzymuje ewentualny zapis, gasi diody, wyłącza kontroler
LCD (`LCD_CR = 0`, także przetwornica) i wchodzi w STOP **bez timera RTC**
(`templogpro_app_stop_wake_seconds() == 0`). Budzą tylko przyciski (EXTI0/1)
i kabel USB. Oba przyciski przez 3 s (odliczanie 3-2-1) albo USB kończą OFF;
wcześniejsze puszczenie wraca do OFF. RTC liczy dalej. Stan OFF jest
w RAM — reset (np. wymiana baterii) uruchamia urządzenie normalnie.

Notatki: `docs/battery_*.md`,
`docs/usb_idle_optimization_2026-09-19.md`.

---

## 10. Architektura oprogramowania

| Plik | Rola |
| --- | --- |
| `Src/main.c` | start modułów i pętla główna |
| `Src/board.c`, `Src/board_regs.h` | GPIO, diody, obsługa wyjątków |
| `Src/drv_lcd.c` | mapa segmentów, rysowanie, ikony |
| `Src/drv_sensor_i2c.c` | programowe I2C i czujnik 0x48 |
| `Src/drv_w25x40.c` | SPI1 + Winbond, osłona przerwania USB |
| `Src/drv_adc.c` | VDD przez VREFINT |
| `Src/templogpro_app.c` | logika aplikacji, menu LCD, alarmy, heartbeat, akcje przycisków |
| `Src/templogpro_logger.c` | rekordy, metadane, statystyki |
| `Src/templogpro_config.c` | konfiguracja w dwóch kopiach |
| `Src/templogpro_sensor.c` | pomiar temperatury |
| `Src/templogpro_rtc.c` | zegar, ochrona przed cofnięciem czasu |
| `Src/templogpro_power.c` | profile zegara i uśpienia |
| `Src/templogpro_buttons.c` | maszyna stanów przycisków |
| `Src/templogpro_usb_files.c` | pliki wirtualnego dysku (HTML, CSV, README, POWER) |
| `Src/st_usb/templogpro_usb_msc_storage.c` | wirtualny FAT (katalog, długie nazwy) |
| `Src/st_usb/msc/usbd_msc.c` | USB MSC + CDC, parser poleceń |
| `Inc/templogpro_version.h` | **wersja, nazwa produktu, autor — jedno miejsce** |
| `panel/templogpro.html` | **źródło panelu HTML** |
| `Src/templogpro_panel_html.inc` | panel w C, generowany przez `tools/gen_panel.py` |

Zasoby: Flash ~158 KB z 256 KB (w tym panel ~108 KB), RAM statyczny ~16 KB
z 32 KB.

Zasady szablonu panelu: łamania linii i wcięcia są usuwane (spacje pisać
jawnie, JavaScript ze średnikami), linie `##` to komentarze, `{{NL}}` = nowa
linia, `{{FW_VERSION}}`, `{{PRODUCT}}`, `{{AUTHOR}}` z `templogpro_version.h`,
`{{DEVICE_DATA}}` = miejsce danych.

---

## 11. Budowanie, testy, wgrywanie

### Wymagania (poza folderem)

- **STM32CubeIDE 2.2.0** zainstalowane w `C:\ST\STM32CubeIDE_2.2.0` (kompilator
  arm-none-eabi, make, STM32CubeProgrammer) i **ST-Link**;
- **Python 3** z biblioteką `unicorn` (`pip install -r tests/requirements.txt`; w lokalnej kopii jest też w `tmp/test_deps`);
- **Node.js** i **Microsoft Edge** (test panelu).

### Polecenia (PowerShell w folderze projektu)

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_release.ps1 [-Clean]   # build + SHA-256
powershell -ExecutionPolicy Bypass -File tools\flash_release.ps1            # wgranie pod 0x08000000, weryfikacja, reset
powershell -ExecutionPolicy Bypass -File tools\flash_release.ps1 -ListOnly  # lista ST-Linków
python tools\gen_panel.py                                                   # panel -> .inc + podgląd tmp\panel_preview.html
node tests\panel_edge_vectors.cjs                                           # test panelu w Edge
Get-ChildItem tests\*.py | % { python $_.FullName }                         # testy firmware
```

Wgrywanie nie kasuje pamięci Winbond ani nie robi mass erase. Nigdy nie
kasuj Winbond ani nie rób mass erase bez świadomej decyzji i zrzutu pamięci.

### Testy automatyczne (wszystkie przechodzą)

| Test | Zakres |
| --- | --- |
| `storage_vectors.py` | prawdziwy kod loggera w emulatorze, 818 punktów utraty zasilania |
| `usb_modes_vectors.py` | 249 sprawdzeń USB/czujnik/konfiguracja na obrazie Release |
| `lcd_menu_vectors.py`, `lcd_actions_vectors.py`, `lcd_segment_map_vectors.py` | menu, akcje, mapa segmentów (152 + 218 + mapa) |
| `measurement_grid_vectors.py` | siatka pomiarów (360 pomiarów/h bez dryfu) |
| `usb_idle_vectors.py` | warunki uśpienia, 4 warianty kompilacji × 65 |
| `flash_usb_guard_vectors.py` | osłona SPI/USB (36) |
| `button_fsm_vectors.py`, `rtc_monotonic_vectors.py`, `power_accounting_vectors.py`, `logger_runtime_vectors.py` | logika pomocnicza |
| `module_probe_*.py` | obrazy diagnostyczne |
| `tz_vectors.py` | prawdziwy kod stref czasowych: przesunięcia wokół zmian czasu (UE, USA, Australia) |
| `panel_edge_vectors.cjs` | panel w Edge przy 1440 i 480 px oraz scenariusz zmiany czasu (54 sprawdzenia) |

---

## 12. Sprawy otwarte

1. **Bateria przy podłączonym USB** (najważniejsze): pobór 14–16 mA z baterii
   i spadek napięcia do ~2,1 V. Plan: zmierzyć prąd samej gałęzi baterii bez
   USB, z USB w trybie raportów i z USB podczas kopiowania; porównać z obrazem
   `tools/build_usb_power_path_diag.ps1` (wykrywa USB, ale go nie uruchamia).
   Jeśli różnica zostaje — tor zasilania na płytce; jeśli znika — profil USB
   w firmware. Opis: `docs/usb_power_path_test_2026-09-19.md`.
2. **Długi test trwałości zapisu** na baterii i USB z okresowym otwieraniem
   raportu i porównywaniem liczby rekordów.
3. **Ręczny test przed wydaniem**: anulowanie START/STOP/CLEAR na każdej
   cyfrze, maska menu, `usb_logging`, alarm górny i dolny z powrotem do zakresu,
   PDF w obu motywach.
4. **Dokładność zegara**: RTC pracuje na wewnętrznym oscylatorze LSI, który
   może się spieszyć lub spóźniać; czas warto ustawiać z panelu
   („Ustaw datę i godzinę z komputera”) przy każdym starcie logu.

---

## 13. Historia projektu (skrót)

| Okres | Etap |
| --- | --- |
| lipiec–sierpień 2026 | diagnostyka sprzętu, pierwsze firmware, logger, raport HTML, tryby baterii (Codex) |
| wrzesień 2026 | badanie modułów, tryby USB, czujnik one-shot, naprawa błędu `Err` w pamięci (Codex) |
| 2026-10-09 | mapa LCD wg dokumentacji szkła, format zapisu v3 odporny na reset, podział `main.c` na sterowniki, menu LCD i °F (Claude) |
| 2026-10-10 | nowy panel i raport PDF, wspólny heartbeat serca i diody, wersja 0.10.0 w jednym miejscu (Claude) |
| 2026-10-10/11 | rebranding na TempLogPro: logo, nazwy, plik `TempLogPro.html`, nazwy w kodzie; nowy rytm diody alarmu (Claude) |
| 2026-10-13 | **wersja 1.1.0**: zegar 12 h na LCD (słońce po południu), zegar urządzenia na żywo w panelu (`GET TIME`), nazwa `TempLogPro` w Windows, ochrona CLR/START przy baterii < 2,7 V, przyczyna restartu w `POWER.TXT` i raporcie, nowe zrzuty w instrukcji (Claude) |
| 2026-10-12 | **wersja 1.0.0**: strefa czasowa i zmiana czasu (zegar w UTC, czas lokalny w CSV i raporcie), niższy pobór przy zatrzymanym loggerze, stały X ze strzałkami kierunku alarmu (Claude) |

Szczegółowy dziennik zmian z sumami SHA-256 wgranych obrazów:
`docs/claude_handover_2026-10-09.md`.

---

## 14. Kopia zapasowa projektu

Folder projektu jest kompletną paczką: źródła, biblioteki STM32 (`Drivers`),
narzędzia, testy (z biblioteką `unicorn`), dokumentacja, obrazy firmware
i zrzuty pamięci. Po skopiowaniu w dowolne miejsce build z terminala działa
(skrypt sam poprawia ścieżkę skryptu linkera — sprawdzone na kopii).

Do odtworzenia potrzebne są tylko narzędzia z rozdziału 11. W STM32CubeIDE
projekt importuje się przez File → Import → Existing Projects into Workspace.

Repozytorium na GitHubie zawiera kod, narzędzia, testy, dokumentację i gotowy
obraz w `firmware/`; `backup/`, `tmp/` i zdjęcia robocze są tylko w kopii lokalnej.

Foldery, których nie trzeba archiwizować (da się je odtworzyć):
`tmp/chrome-*` (stare profile przeglądarki, ~42 MB), `Debug/` (stary build
z IDE, ~16 MB). Nie usuwaj `backup/` (zrzuty pamięci Winbond i poprzednie
obrazy) ani `tmp/test_deps` (biblioteka do testów).
