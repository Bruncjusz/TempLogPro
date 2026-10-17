# Przekazanie do lokalnej sesji Claude Code (2026-10-09)

Czytaj po `PROJECT_HANDOVER.md`. Ten plik opisuje, co zmieniło się 2026-10-09
i co jest następne. Tam, gdzie różni się od `PROJECT_HANDOVER.md`, ważny jest
ten plik.

## Stan

- Folder roboczy to ten folder (`Desktop\ProjektTempTaleCodex`). STM32CubeIDE
  (workspace `C:\Users\Brunon\STM32CubeIDE\workspace_2.2.0`) wskazuje na niego.
  Stara kopia w `workspace_2.2.0\TempTaleCodex` jest nieużywana.
- Na urządzeniu jest obraz po punktach 2, 3 i zmianach z punktu 4 (siatka
  pomiarów, menu LCD, stopnie F), wgrany z terminala:
  `Release/TempLogPro.bin` SHA-256
  `a2173f0a1b4524b69af3c555f8ce2a641b06a72d1e254e324b2e8d6248169c17`.
  Wcześniej: `b7a63851...` (stopnie F, progi ze strzałkami),
  `4f55f536...` (menu LCD), `5b3b518d...` (siatka),
  `958edb05...` (sam punkt 3).
- Wcześniejsze obrazy: po punkcie 2 bez rozbicia `main.c` (bin `4e10f2f1...`,
  nie ma kopii, odtwarzalny z `backup/before_main_split_20261009/`), mapa LCD
  z metadanymi v2 w `backup/deployed_20261009_lcd_map/` (ELF `5cd476a1...`),
  obraz Codexa sprzed zmian w `backup/before_lcd_map_fix_20261009/`.
- Build i wgrywanie z terminala (narzędzia z instalacji CubeIDE 2.2.0):
  - `powershell -ExecutionPolicy Bypass -File tools\build_release.ps1 [-Clean]`
    buduje Release przez wygenerowane makefile, robi `.bin`/`.hex` i wypisuje
    SHA-256. Listę plików `Src/*.c` w `Release/Src/subdir.mk` i
    `Release/objects.list` skrypt odtwarza sam, więc nowy lub usunięty plik
    w `Src/` nie wymaga IDE (podfoldery `Src/st_usb/...` nadal z plików IDE).
  - `powershell -ExecutionPolicy Bypass -File tools\flash_release.ps1` wgrywa
    `.bin` pod `0x08000000` (Under reset, weryfikacja, reset; bez mass erase).
    `-ListOnly` pokazuje podłączone ST-Linki.
  - Reset bez wgrywania: `STM32_Programmer_CLI -c port=SWD mode=UR reset=HWrst -rst`
    (tryb HOTPLUG na tej płytce nie działa).
- Obraz różni się bajtowo między buildami tylko przez `__DATE__`/`__TIME__`
  w `templogpro_rtc.c`.
- Folder nie jest repozytorium git, a git nie jest zainstalowany na tym
  komputerze.

## Zmiany z 2026-10-09

1. Mapa LCD poprawiona wg `temptale-ultra_LCD.pdf`. W `Src/main.c` cztery
   adresy RAM[6] były błędne: MIN (jest bit 23), MAX (bit 22), środkowa kropka
   (bit 15), TIME (bit 26). Użytkownik potwierdził na szkle MIN/MAX/TIME.
   Priorytet 2 z `PROJECT_HANDOVER.md` jest zamknięty.
2. Karty progów alarmu (5, 6): strzałka w dół/w górę + MIN/MAX + stopnie C,
   bez TEMP. Czeka na potwierdzenie użytkownika.
3. Karta interwału (9): zegar z dwukropkiem, TIME + klepsydra + PROBE;
   `M:SS` + MIN(S) poniżej 20 min, `H:MM` + HR(S) poniżej 20 h, dalej pełne
   godziny. Nowa funkcja `templogpro_hw_lcd_show_clock(major, minor)`.
   Czeka na potwierdzenie użytkownika.
4. `templogpro_config.c` był zmieniony po ostatnim buildzie Codexa; nowy obraz
   to zawiera (krótszy komentarz w `CONFIG.TXT`).
5. Testy: `tests/lcd_menu_vectors.py` zaktualizowany, nowy
   `tests/lcd_segment_map_vectors.py` (adresy segmentów w obrazie vs mapa).
   `docs/lcd_menu_icons.md` zaktualizowany.

## Mapa LCD (słowo RAM, bit)

| Bit | RAM[0] | RAM[2] | RAM[4] | RAM[6] |
| --- | --- | --- | --- | --- |
| 1 | % | RH | HR(S) | MIN(S) |
| 2 | degrees | degrees F | degrees C | - |
| 5 | 1A | 1B | 1C | 1D |
| 6 | 10E | 10F | 10G | right dot |
| 12 | 10A | 10B | 10C | 10D |
| 13 | Check Mark | \ | / | AVG |
| 14 | big arrow | TEMP | : | - |
| 15 | 100E | 100F | 100G | mid dot |
| 22 | up arrow | down arrow | Stop Sign | MAX |
| 23 | 1E | 1F | 1G | MIN |
| 24 | 100A | 100B | 100C | 100D |
| 25 | 1000BC | left dot | DAY(S) | - |
| 26 | heart | negative | Battery | TIME |
| 27 | sun | Probe | hourglass | - |

Układ pola cyfr: `1 . 8 : 8 . 8` (wiodąca jedynka, lewa kropka, setki,
dwukropek i środkowa kropka, dziesiątki, prawa kropka, jedności).

## Punkt 2: format zapisu v3 (zrobione 2026-10-09)

Opis: `docs/storage_format_v3.md`. Naprawione: blokada loggera po resecie
w trakcie zapisu (`Err` z września), zapis metadanych przy każdej próbce
(teraz 14 zamiast 777 na 760 próbek), jedna kopia konfiguracji (teraz dwie:
A `0x7F000`, B `0x00000`). Format rekordów bez zmian, migracja z v2
automatyczna.

Weryfikacja: `tests/storage_vectors.py` (1633 punkty utraty zasilania na
prawdziwym kodzie C w unicorn; stary kod oblewa ten test). Na sprzęcie:
migracja zachowała 2437 rekordów i konfigurację gen 17; 5 resetów pod
logowaniem bez błędu; zapis konfiguracji trafił do kopii B (gen 18) i został
odczytany po resecie. Pozostałe testy Python przechodzą.
`tests/usb_modes_panel.cjs` wymaga Playwright, którego tu nie ma; nie
uruchomiony (panel HTML nie był zmieniany).

Skutki uboczne do pamiętania:
- Starsze firmware nie odczyta metadanych v3 (log będzie dla niego pusty).
  Przed cofnięciem firmware zrób zrzut Winbond.
- Pominięty slot liczy się do `records` i w `DATA.CSV` ma kod `0xFFFF` oraz
  datę 1970-01-01; panel powinien go ukryć (punkt 5).
- Każdy restart dopisuje zdarzenie `R005` (strażnik czasu RTC). Tak było
  wcześniej; zegar urządzenia pokazuje 2026-08-25, czyli nie był ustawiony.

## Punkt 3: podział `main.c` (zrobione 2026-10-09)

`main.c` (3093 linie) ma teraz tylko start modułów i pętlę główną. Sterowniki:

| Plik | Zawartość |
| --- | --- |
| `Src/board_regs.h` | surowa mapa rejestrów STM32L152; nie łączyć z nagłówkami CMSIS/HAL (te same nazwy) |
| `Src/board.h`, `Src/board.c` | `board_delay`, `board_gpio_af`, diody, `templogpro_hw_led_*`, obsługa HardFault/MemManage/BusFault/UsageFault (kod migania jak wcześniej, `g_board_fault` dla SWD) |
| `Src/drv_lcd.c` | mapa segmentów `SEG_*`, inicjalizacja LCD, rysowanie, sonda segmentów, `templogpro_hw_lcd_*` |
| `Src/drv_sensor_i2c.c` | I2C programowe PB6/PB7 i czujnik 0x48 (dawny `templogpro_sensor_bus.inc` wchłonięty) |
| `Src/drv_w25x40.c` | SPI1 + Winbond, osłona IRQ20, `templogpro_hw_flash_*` |
| `Src/drv_adc.c` | VDD przez VREFINT i skan pinów analogowych |

Usunięte martwe tryby `FIRMWARE_MODE` 0, 4, 6 i 7 (czytnik temperatury,
`templogpro_usb_st_msc_mode`, własny stos USB EP0, test JEDEC), licznik
ustawień `templogpro_settings_counter_read` oraz stare I2C bez limitów czasu.
Firmware zmniejszył się tylko o 216 B, bo linker i tak je pomijał.
Naprawione przy okazji: `w25x40_wait_busy` liczy czas przez `HAL_GetTick()`
(milisekundy także przy SysTick 100 Hz na baterii), z dodatkowym limitem
liczby odczytów statusu.

Sonda diagnostyczna `diagnostics/module_probe` dołącza teraz pliki
sterowników zamiast `main.c` i czyta czujnik własną magistralą z limitami.
Wszystkie 6 wariantów `tools/build_module_probe.ps1` się buduje i przechodzi
audyt symboli; `tools/build_usb_power_path_diag.ps1` też. Ich poprzednie
obrazy są w `backup/before_main_split_20261009/`.

Weryfikacja: wszystkie testy Python przechodzą (w tym `module_probe_*` na
nowym obrazie sondy). Na sprzęcie po wgraniu: USB, `PING`, konfiguracja
gen 18, logger LOGGING, przyrost próbek z czujnika (25,4 °C), VDD 3301 mV.
LCD nie był oglądany po zmianie; kod rysowania jest przeniesiony bez zmian
i sprawdzony testem `lcd_segment_map_vectors.py` na obrazie.

## Punkt 4: rozpoczęty

- Harmonogram pomiarów trzyma siatkę wyznaczoną przy START
  (`app_next_measurement_deadline` w `templogpro_app.c`); opóźnione wybudzenia
  nie przesuwają kolejnych pomiarów, pominięte terminy nie są nadrabiane.
  Test `tests/measurement_grid_vectors.py` (obraz Release, losowe opóźnienia
  0-4 s przez godzinę): stary kod 337 pomiarów i przesunięcie do 9 s, nowy 360
  pomiarów i przesunięcie najwyżej równe opóźnieniu. Wgrane; na urządzeniu
  rekordy co równe 5 s. Obraz: bin `5b3b518d...`.
- Migający X wyjaśniony: to zatrzaśnięty alarm temperatury, nie `Err`.
  `app_alarm_or_error_active()` zapala X przy stanie ERROR albo przy
  jakimkolwiek alarmie w historii bieżącego logu (do CLEAR). Na urządzeniu
  dolny próg 24,0 °C, a log ma minimum 23,19 °C; stan `app=RUN`.
- Menu LCD wg decyzji użytkownika: kolejność TEMP, stan, AVG, MAX, próg górny
  (strzałka w górę + MAX), MIN, próg dolny (strzałka w dół + MIN), czas pracy,
  rekordy, interwał, bateria, CLEAR. Zmierzone MIN/MAX mają TEMP + MIN/MAX,
  progi PROBE + TEMP + MIN/MAX (bez strzałek, decyzja użytkownika), karta
  stanu (run/StP) ma PROBE. Numery kart i bity `lcd_menu_mask` bez zmian.
  Strzałki góra/dół zostają tylko jako wskazanie kierunku alarmu na ekranie
  bezczynności.
- Karta interwału: same ikony PROBE + TIME i liczba sekund, bez jednostki,
  bez klepsydry. Interwał ograniczony do 1-600 s (firmware i panel); starsza
  konfiguracja z większą wartością jest przy odczycie przycinana do 600.
  Funkcję zegara `templogpro_hw_lcd_show_clock` usunięto (nieużywana).
- Testy zaktualizowane (`lcd_menu_vectors.py`: kolejność z tablicy w obrazie;
  `lcd_segment_map_vectors.py`: bity cyfr dla 5/60/600/999;
  `usb_modes_vectors.py`: limit 600 i przycinanie). Wgrane: bin `4f55f536...`.
- Kolejność checkboxów kart w panelu HTML nadal stara (punkt 5).
- Stopnie F (decyzja użytkownika: ustawiane w panelu HTML, dotyczą każdej
  karty LCD z temperaturą). Konfiguracja v5 (48 B): pole `temperature_unit`
  (offset 42, 0 = C, 1 = F), w tekście `temperature_unit=C|F` (także 0/1).
  v1-v4 czytane jako C. Starsze firmware nie odczyta v5 i wróci do ustawień
  domyślnych. Dane w Winbond, CSV i progi zostają w °C; przelicza tylko LCD
  (`app_show_temperature`, ikona `TEMPLOGPRO_LCD_ICON_DEGREE_F` = segmenty
  ° + F). Pole temperatury pokazuje teraz do 199,9 z wiodącą jedynką (np.
  140,0 °F); wcześniej obcinało do 99,9.
- Panel: pole „Jednostka na LCD” w ustawieniach, wysyłane i odczytywane przez
  Web Serial; przełącznik °C/°F raportu startuje od jednostki urządzenia, gdy
  przeglądarka nie ma zapisanego wyboru. Sprawdzone w Edge headless (pole,
  payload z `temperature_unit=F`, limit interwału 600).
- Na urządzeniu: konfiguracja v4 odczytana (gen 18), przełączenie na F i z
  powrotem na C przez CDC zapisane (gen 19, 20). Wgrane: bin `b7a63851...`.
  Wyglądu °F na szkle nie oglądano.

## Punkt 5: panel HTML (w toku, pierwsza część wgrana)

- Kopia stanu sprzed tej pracy: `backup/stable_20261009_before_html/`
  (obraz `a2173f0a...` + `sources.zip`).
- Źródło panelu to `panel/templogpro.html`. `tools/gen_panel.py` robi z niego
  `Src/templogpro_panel_html.inc` (HTML_PREFIX/HTML_SUFFIX) i podgląd
  `tmp/panel_preview.html` z przykładowymi danymi; `build_release.ps1`
  uruchamia go sam. Zasady szablonu: łamania linii i wcięcia są usuwane
  (spacje pisać jawnie, JS ze średnikami), linie `##` to komentarze,
  `{{NL}}` = znak nowej linii, `{{DEVICE_DATA}}` = miejsce CSV i JSON.
  Przeniesienie było bajtowo identyczne (obraz różnił się 4 bajtami czasu
  kompilacji).
- Zmiany: teksty w jednym bloku `tx` (PL/EN, jeden klucz na linię) z polskimi
  znakami; ustawienia w grupach (Pomiar i alarmy / Wyświetlacz i diody /
  Zasilanie), pasek połączenia i zapisu, ekrany menu i lista „Ekran po
  starcie” w kolejności z urządzenia; surowe pola (odpowiedź urządzenia,
  CONFIG.TXT, stan zasilania) w zwijanych „Szczegółach technicznych”;
  zakładka Informacje bez pól technicznych (HTML/PDF/USB/ostatni kod), za to
  z interwałem, progami i jednostką; wpisy `E001` (pominięte sloty) ukryte
  w tabelach i liczone w Informacjach; poprawki szerokości telefonu; wersja
  firmware 0.9.0 (także README/POWER/STATUS).
- Test `tests/panel_edge_vectors.cjs` (Edge headless, bez Playwright,
  26 sprawdzeń przy 1440 i 480 px). `tests/usb_modes_panel.cjs` nadal wymaga
  Playwright; czyta teraz panel z `.inc`.
- Wgrane: bin `1a963912e268235c3d52a80191ba92df957ec8e5460a6cabc08f9a428480abe7`.
  Prawdziwy `TEMPLOGPRO.HTML` z urządzenia sprawdzony w Edge.
- 2026-10-10: nowe zakładki Raport i Dane (kopia obrazu sprzed zmian:
  `backup/before_report_data_20261010/`, bin `1a963912...`).
  - Raport: pasek werdyktu (w zakresie / przekroczenie, „trwa”), kafelki
    ostatni pomiar, min/max z godziną, średnia, czas poza zakresem (% czasu
    objętego pomiarami), liczba przekroczeń; wykres z równymi podziałkami,
    zielonym pasem między progami, bez gęstej siatki; szybkie zakresy
    Całość/1 h/24 h/7 dni (ukryte, gdy dłuższe niż log); obok „Dane pracy”
    i podsumowanie przekroczeń.
  - Przekroczenie (epizod) = kolejne próbki poza jednym progiem; koniec na
    pierwszej próbce w zakresie albo przed przerwą > 3 interwały.
  - Przerwa w zapisie > max(20 interwałów, 10 min) lub cofnięcie zegara jest na
    wykresie przerywaną linią zamiast pustego czasu (zegar urządzenia nie był
    ustawiony, log skacze z 25.08 na 09.10).
  - Zakres na krawędzi pierwszej/ostatniej próbki jest otwarty, więc
    zdarzenia przed pierwszą i po ostatniej próbce (np. R002) są widoczne.
  - Dane: tabela przekroczeń, zakresy pracy, zdarzenia (z S001), pomiary po
    200 na stronę z filtrem Wszystkie/Poza zakresem/Zdarzenia i zakresem z
    wykresu, zdarzenia bez temperatury („—”), „Pobierz CSV”, legenda zwinięta.
  - PDF: Skrócony = strona 1; Raport = + strona danych; Pełny = + wszystkie
    wpisy od najstarszego w 3 kolumnach (42 284 wpisy ≈ 158 stron, 3 MB;
    w tabeli byłoby ~800 stron i ~100 MB; wcześniej drukowało się tylko 500
    ostatnich). Kolory tabel przeliczane przy zmianie motywu (druk jasny).
  - `tests/panel_edge_vectors.cjs` rozszerzony (34 sprawdzenia).
  - Wgrane: bin `853755507b7d95fdfbff9cad71229a87c17dafee1e9b3390079a7782d045974b`;
    `PING`, logger LOGGING, nowy `TEMPLOGPRO.HTML` na dysku E: sprawdzony.

- 2026-10-10 (druga runda, uwagi użytkownika ze zrzutów w `Zdjęcia do analizy/`):
  pasek werdyktu pokazuje tylko maks. i min. (czasy zostają w kafelkach
  i w „Przekroczenia”); jeden przełącznik zakresu Całość/1 h/24 h/7 dni/
  Własny (Własny rozwija pola Od–Do, zmiana działa od razu, przeciągnięcie po
  wykresie też ustawia Własny); Ustawienia: Połącz/Odczytaj/Zapisz przy
  tytule, karta Loggera (Start/Stop/Odśwież/Wyczyść + duża wolna pamięć
  z paskiem: zielony, żółty < 25 %, czerwony < 10 %) nad ustawieniami,
  osobne grupy Wyświetlacz i diody / Zasilanie / Ekrany w menu LCD; nowy
  wygląd całej strony (wyśrodkowana szerokość 1400 px, spokojne karty, logo,
  zakładki z podkreśleniem, bez gradientów i poświat). Wgrane: bin
  `2df03d448f80a3e519949fa7a856c4aaf9885d4cee9f993c9079de56ae06e0e9`.

- 2026-10-10 (trzecia runda): strona na pełną szerokość (bez max-width),
  wyraźniejsza siatka wykresu z liniami pośrednimi, przerwa w zapisie
  (wyłączenie, zmiana zegara) jako szara strefa „BRAK DANYCH” z godziną przed
  i po (decyzja użytkownika: jeden napis zamiast rozróżniania offline /
  zmiana czasu). Wgrane: bin
  `c87be4d4af9e8b3d1566e4e6d4274b090fb49f88bd20913c8fa529bcae5e7892`.

- 2026-10-10: jeden heartbeat dla serca LCD i zielonej diody
  (`app_measure_pulse_active`: 600 ms na USB, 120 ms na baterii). Serce
  także przy zatrzaśniętym alarmie i na ekranie „USB”; zielona dioda miga przy
  pomiarze także przy alarmie (czerwona dalej miga razem z X). LCD jest
  przerysowywany od razu przy pomiarze i przy końcu impulsu, więc serce gaśnie
  razem z diodą. Wgrane: bin
  `91772ce0eadbf4fa0a3f0f6aefa6f9bde87cd993952b0df52bc7cddce3e35b92`.
  Użytkownik: 120 ms na baterii za krótko, zmienione na 250 ms
  (`APP_MEASURE_LED_BATTERY_PULSE_MS`); bin
  `a12e236f9549ec03b3c73d1b8383144195c0c11c722104b7d86d781f092db1c4`.

- 2026-10-10: wersja firmware 0.10.0 w jednym miejscu:
  `Inc/templogpro_version.h` (`TEMPLOGPRO_FW_VERSION`). Używają jej README.TXT,
  STATUS, POWER.TXT, a panel dostaje ją przez `{{FW_VERSION}}` w
  `tools/gen_panel.py`. Przy kolejnym wydaniu zmieniać tylko ten plik. Wgrane:
  bin `c4aab159a66855e8deffa7424e560421bda96c31af91ecbb6781842586a9c406`.

- 2026-10-10: przycisk jednostki pokazuje kierunek („°C → °F” / „°F → °C”);
  w Ustawieniach Pomiar i alarmy / Wyświetlacz i diody / Zasilanie w trzech
  równych kolumnach (pola jedno pod drugim, poniżej 1100 px jedna kolumna).
  Wgrane: bin `03b5039418aa0292fd6d6f4c36be4c2656013bde4e8b872a844380b4d945ffa4`.

- 2026-10-10 (analiza `raport do weryfikacji.pdf` użytkownika): własny
  nagłówek/stopka stron przez `@page` margin boxes (wstrzykiwane w
  `printPageSetup`, styl `#pageStyle` po głównym CSS; zastępują stopkę
  przeglądarki z `file:///E:`), „Strona X z Y”; tytuł dokumentu na czas
  druku = nazwa PDF `TempLogPro_<data>_<HHMM>-<HHMM>`; „Dane pracy” bez
  powtórzonych czasów; pełny raport bez wymuszonej strony przed pomiarami;
  lista pomiarów z nagłówkiem dnia, bez daty w wierszu, kod tylko poza M000,
  4 kolumny; zdarzenia w druku chronologicznie; legenda tylko z kodami
  obecnymi w logu; pola Uwagi / Sprawdził / Data i podpis pod stroną 1
  (tylko druk); nowe logo (termometr + heartbeat, SVG w pliku) i ta sama
  ikonka karty jako `data:` SVG (offline). Wgrane: bin
  `f88431fe2b88b3ccb841e9beabe26b541335f81632af7d742617fc74562ad73e`.

- 2026-10-10: poprawki druku wg użytkownika: legenda kodów na stronie 2 pod
  zdarzeniami (także w raporcie standardowym), kafelki wyśrodkowane, bez
  paska po lewej w werdykcie, tabele z pełną ramką (ramka na `.tablewrap`
  zamiast obramowań komórek), ciemniejsze linie (`--border:#9ea7b3` w druku),
  na stronie 1 bez napisu w lewym górnym rogu (`@page:first`). Wgrane: bin
  `2cc9192d7edb12f08c3418a7cdd7c0392ed5b26d5d99135afa40765e5a49476b`.

- 2026-10-10: flaga przy przycisku EN brytyjska (`.flag-gb`, SVG w CSS),
  status RUN tuż obok zakładek. Wgrane: bin
  `d53588d60272d0bffc473d7d8016a309ca650a5bd037d1508fe3a9135be36926`.

- 2026-10-10: karta „Uwagi i weryfikacja” w Raporcie (pole uwag + „Sprawdził”),
  treść trafia do ramek pod stroną 1 w druku; „Data i podpis” usunięte;
  nazwisko sprawdzającego pamiętane w przeglądarce (`tt_checked_by`), uwagi nie.
  Naprawiony błąd: dosłowne `` `n `` w CSS psuło regułę `.printonly`, więc
  ramki druku były widoczne na ekranie. Wgrane: bin
  `b5b225adf98a1e26a035aea80f3abdcd7bb8be75c6f22a8839e765cf490c2d1c`.

- 2026-10-10: uwagi i „Sprawdził” nie są na stronie (decyzja użytkownika):
  wpisuje się je w oknie `#printDlg`, które otwiera „Generuj PDF”; trafiają
  tylko do ramek w PDF. Ctrl+P drukuje bez okna (ramki puste lub z ostatnich
  wartości). Wgrane: bin
  `9d6e73bc93938af3c9f739933a4cdc7679564664be99cd0efe28e7224197909c`.

- 2026-10-10: rebranding na **TempLogPro** (autor: Brunon Kucharski,
  współpraca: Codex i Claude). Nazwa i autor w `Inc/templogpro_version.h`
  (`TEMPLOGPRO_PRODUCT_NAME`, `TEMPLOGPRO_AUTHOR`), panel dostaje je przez
  `{{PRODUCT}}` / `{{AUTHOR}}` w `gen_panel.py`. Zmienione: logo i favicon
  (SVG użytkownika `templogpro-icon.svg`), tytuł, nagłówek PDF, nazwa pliku
  PDF i CSV, stopka z autorem na stronie, wiersze Autor/Współpraca w
  Informacjach, README.TXT (przepisany, aktualny), nagłówki POWER/CONFIG/
  TIME/ADC/STATUS, deskryptory USB (producent, produkt), etykieta dysku
  `TEMPLOGPRO`, SCSI inquiry („TempLog Pro USB Device”). Bez zmian:
  identyfikatory w kodzie (`templogpro_*`), nazwa pliku `TEMPLOGPRO.HTML`,
  nazwa projektu/obrazu `TempLogPro.bin`. Wgrane: bin
  `e2f1341d28f627d6ac991b478767f535a7b66cb48653e141b062851b224aa84d`.

- 2026-10-10: plik raportu na dysku to `TempLogPro.html` (LFN w dwóch wpisach,
  `write_lfn_name` w `templogpro_usb_msc_storage.c`, alias 8.3 `TEMPLO~1.HTM`).
  Czerwona dioda alarmu: dwa błyski 100 ms (przerwa 150 ms) co 3 s
  (`app_red_led_on` / `app_red_led_busy`; cykl startuje od pierwszego
  przebiegu pętli po okresie, więc działa też przy budzeniu co 1 s na
  baterii). X na LCD bez zmian (cykl 1,1 s). `tests/usb_idle_harness.c`
  dostosowany. Wgrane: bin
  `91634abf404ab9c794360aaec007db10002de3b36202f0c89fa9db6910866a11`.

- 2026-10-10: pełny rebranding kodu na TempLogPro (skrypt zamiany tekstu i nazw
  plików): `temptale_*` → `templogpro_*`, `TempTale*` → `TempLogPro*`,
  `TEMPTALE_*` → `TEMPLOGPRO_*`, pliki `Src/templogpro_*.c`,
  `Inc/templogpro_*.h`, `panel/templogpro.html`,
  `Src/templogpro_panel_html.inc`, obraz `Release/TempLogPro.bin`, projekt
  CubeIDE `TempLogPro` (`.project`, `.cproject`, `TempLogPro Debug.launch`),
  makefile i `objects.list` w `Release/`, testy, narzędzia, diagnostyka,
  dokumentacja. Bez zmian: nazwa sprzętu „TempTale Ultra” i
  `temptale-ultra_LCD.pdf`, folder projektu `ProjektTempTaleCodex`,
  `backup/`, archiwa diagnostyki, `kod.txt`, stare wyjścia IDE w `Debug/`
  i `CodexBuild/`. Stałe magiczne w Winbond są liczbowe, więc dane i
  konfiguracja zostały odczytane bez zmian (3280 wpisów, konfiguracja gen 29).
  Kopia przed zmianą: `backup/before_rebrand_20261010/`. W STM32CubeIDE
  projekt trzeba zaimportować ponownie (zmieniona nazwa w `.project`).
  Wgrane: bin `401545ec00256f313dd894f590c98b3654feb8ce60e88c9e35bf28fbcdd493fe`.

- 2026-10-11: napis logo wg `zdjecia do projektu/Nowoczesne logo TempLogPro z
  termometrem.png` odtworzony w CSS (nie obrazek): „TempLog” kolorem tekstu,
  „Pro” gradientem #19D3FF→#0A3CFF, hasło „Professional temperature data
  logger” z kreskami; opis raportu obok za pionową linią. Wgrane: bin
  `90656e4675484c8b2e6260bc3c6eca437adbfedf588b1c7be379b36a36669af7`.

- 2026-10-11: dokumentacja `DOKUMENTACJA.md` + `README.md`, licencja MIT,
  `firmware/TempLogPro.bin|hex` (bin `90656e46...`), zrzuty panelu na
  przykładowych danych w `docs/assets/`, `tests/requirements.txt` (unicorn).
  `build_release.ps1` poprawia bezwzględną ścieżkę skryptu linkera w
  `Release/makefile` (kopia działa z dowolnego folderu). Publiczne repozytorium:
  https://github.com/Bruncjusz/TempLogPro (gałąź `main`, autor commitów
  z adresem noreply GitHuba). `.gitignore` pomija `backup/`, `tmp/`,
  `Debug/`, `CodexBuild/`, zdjęcia robocze, `kod.txt`, PDF raportu i wyjścia
  kompilacji. Mapa LCD: hackaday.io/project/189442.

- 2026-10-12: oszczędzanie w stanie zatrzymanym: LCD ~75 Hz zamiast ~300 Hz
  (`LCD_FCR_PS(2)`, ten sam stosunek PON do fazy), budzenie RTC wybierane przez
  `templogpro_app_stop_wake_seconds()` (1 s przy pomiarach, 3 s przy alarmie
  z diodą, 60 s przy zatrzymanym loggerze), wybudzanie przez podłączenie USB
  (EXTI12 na D+ z podciągnięciem, uzbrajane tylko gdy linia jest wysoko).
  Alarm na LCD: X świeci stale + trójkąty ↑/↓ kierunku. Strefa czasowa:
  zegar i rekordy w UTC, `Src/templogpro_tz.c` (reguły jak POSIX TZ),
  konfiguracja v6 (96 B, `tz_*`), `DATA.CSV` z czasem lokalnym i kolumną
  `UTC_Offset` (linia 48 B), JSON raportu z `tz*`, panel: grupa „Czas i strefa
  czasowa”, znaczek strefy, znaczniki zmiany czasu (Z001/Z002), SET TIME w UTC.
  Testy: `tests/tz_vectors.py`, scenariusz zmiany czasu w
  `tests/panel_edge_vectors.cjs`. Urządzenie zsynchronizowane: Europe/Warsaw.
  Wgrane: bin `a3c45fdfeb82c96446470f02be727c187d442f6d6d7cab328f5633beeebfaf95`.
  Do zmierzenia przez użytkownika: prąd w stanie zatrzymanym, wygląd LCD przy
  75 Hz. Znane (starsze): po zmianie układu wirtualnego dysku (START/STOP,
  zapis ustawień) Windows może pokazywać nieaktualną zawartość plików do
  ponownego podłączenia USB.

- 2026-10-12: **wersja 1.0.0** (`TEMPLOGPRO_FW_VERSION`), odstęp pod opisem
  strefy w Ustawieniach. Wgrane: bin
  `8a6bd46110432ab531fbd272278bc923c64e31c3db13fdc7782f7cb5ce8aeefa`
  (także `firmware/TempLogPro.bin|hex`). GitHub: tag `v1.0.0` + Release.

- 2026-10-13 (po 1.0.0, wersja jeszcze 1.0.0, niewydane na GitHubie):
  nazwa w Windows z małymi literami — etykieta FAT `TempLogPro` (Windows ją
  tak pokazuje), SCSI inquiry vendor pusty + product `TempLogPro` („TempLogPro
  USB Device”); `GET TIME` (CDC, UTC + lokalny + offset); zegar 12 h na LCD
  (`LCD_VIEW_CLOCK` = 12, karta po temperaturze, `templogpro_hw_lcd_show_clock12`,
  dwukropek miga; LEDS/ICON_SCAN przenumerowane na 13/14), maska menu 13 bitów,
  konfiguracja v7 (układ v6; starsze dostają bit 12); panel: checkbox „Ustaw
  zegar przy zapisie” + źródło (komputer / pole), „Wstaw czas i strefę
  z komputera” tylko wypełnia pola, jeden „Zapisz”, zegar urządzenia na żywo
  z różnicą względem PC. TODO.txt użytkownika omówione; diagnostyki poboru
  prądu pominięte (decyzja użytkownika). Wgrane: bin
  `697d2a50b745774fbabb54cbea41d7abf8203f9e0a75ab7a3a245e1a1f791f57`.

- 2026-10-13 (dalej 1.0.0): słońce na zegarze 12 h po południu; ochrona
  CLR/START na baterii (`app_flash_power_ok`, < 2700 mV → odmowa, migają
  BATTERY + X przez 3 s, `g_lcd_message_icons`); znacznik operacji w
  `RTC_BKP2R` (`templogpro_diag_flash_op_begin/end`), przyczyna restartu
  `templogpro_diag_reset_cause()` w `POWER.TXT` (`LAST_RESET=`), w JSON
  (`"reset"`) i w panelu (Informacje → Ostatni restart); po przerwanej
  operacji bateria miga 5 s po starcie. Testy: stuby VDD/flash-op w
  harnessie, nowe przypadki w `lcd_actions_vectors.py`. Wgrane: bin
  `456b3e939747242231b507722096baa9f80e1fedfe5a03c5987481ba65014094`;
  na urządzeniu `LAST_RESET=SOFTWARE` po wgraniu (poprawnie).

- 2026-10-13: **wersja 1.1.0** (powyższe zmiany z 13.10), nowe zrzuty panelu
  w `docs/assets` (raport ciemny/jasny, Dane, Informacje, Ustawienia) opisane
  w instrukcji. Wgrane: bin
  `99e35b9233aae0ff6bc3db730d564a3d9a8769c915e3b6bf2108e8fff13c71e9`
  (także `firmware/TempLogPro.bin|hex`). GitHub: tag `v1.1.0` + Release.

- 2026-10-15 (po 1.1.0, niewydane): **tryb zegarka** (`clock_mode`, bajt 60 konfiguracji,
  JSON `clockMode`; zatrzymany logger na baterii pokazuje H:MM ze stałym dwukropkiem,
  budzenie RTC na pełną minutę) i **OFF** (`POWER OFF` / `POWER OFF CANCEL` przez CDC,
  `off=ARMED` w LOGGER STATUS; po odłączeniu USB: LCD_CR=0, diody off, STOP bez timera RTC
  - `stop_wake_seconds()==0`; oba przyciski 3 s z odliczaniem 3-2-1 albo USB kończą OFF).
  Panel: pole 'Tryb zegarka', przycisk 'Wyłącz (OFF)' z potwierdzeniem. Test
  `tests/clock_off_vectors.py` (32 sprawdzenia). Wgrane: bin
  `8ded9e44…`; użytkownik potwierdził działanie. Poprawka: zegar bez ikon X/STOP/ptaszka
  (tylko TIME + słońce). Wgrane: bin
  `c34451eb529de60471786dbb7bbaad3c8366a7d151512cb4ebca271ca57def26`.
  Potem: tryb zegarka także w RUN (serce z diodą przy pomiarze), karta LCD `OFF` (widok 13,
  LEDS/ICON_SCAN -> 14/15, maska 14 bitów 0x3FFF, konfiguracja v8 - starsze dostają bit 13;
  oba przyciski, odliczanie o 5..o 0, napis OFF 1,2 s, potem OFF; na USB po odłączeniu).
  Zbudowane: bin `d6d4eb5b89ac6284a7d15cef195b2b97ebb7027c755436a093eb953abeaaf319` -
  wgrane po poprawieniu przewodów SWD; konfiguracja zmigrowana do v8 (lcd_menu_mask=16383,
  clock_mode=1 zachowane), logger RUN z 12881 wpisami bez strat.
  Kolejność: OFF przed CLR w LCD_MENU_SEQUENCE (użytkownik przewija STARTEM, wstecz:
  Temperatura -> CLR -> OFF), panel tak samo. Zbudowane (czeka na wgranie, ST-Link bez połączenia): bin
  `565df2f3d42acbba31066bbe89f13a7bc90364ea4aca859beece5cc7f4827d9f`.
  Następnie karta LCD **tryb zegarka** (widok 14: TIME + On/OFF z `clock_mode`, tylko podgląd;
  w menu zaraz za zegarem; LEDS/ICON_SCAN -> 15/16; maska 15 bitów 0x7FFF; konfiguracja v9 -
  starsze dostają bit 14). Wgrane: bin
  `18f2f33f7c8c856ec34303a1dd5e919acaf6c4f5bdcd20a3f54d8cd99deb3c30` (zawiera też kolejność OFF/CLR).
  Potem: na karcie trybu zegarka przytrzymanie START = zegar on, STOP = off (zapis konfiguracji,
  odmowa na baterii < 2,7 V; bez ruszania loggera). Zbudowane, NIEWGRANE (ST-Link: brak rdzenia -
  urządzenie w STOP bez USB, a NRST chyba bez kontaktu): bin
  `9af2f626034f8d42a4295b3c14b60f1a35f79bb5276e10bad5174266d7471d0a`.
  Potem: ostrzeżenie baterii jako napis `bAt` + migające tylko BATTERY i X (komunikat z własnymi
  ikonami nie dokłada ikon bazowych, więc nie ma ptaszka obok X). Zbudowane, niewgrane
  (ST-Link odłączony): bin `334269686d00160145a20f1f8d4494c4ab26b3e488cf90432959d894f4de9523`.
  Potem: karta LCD **diody LED** (widok 15: SUN+PROBE+On/OFF, w menu za kartą trybu zegarka;
  maska 16 bitów 0xFFFF, konfiguracja v10 - starsze dostają bit 15); karty trybu zegarka i diod:
  przytrzymanie START/STOP z odliczaniem On5..On0 / OF5..OF0 (APP_ACTION_CARD_ON/OFF), anulowanie
  zostaje na karcie (poprawka: anulowanie z kart > 12, także OFF, wracało do temperatury).
  Wgrane: bin `043faf0a7b66850baec3e47a19f41c21541a458dc6e223ce25df7a007f61a0ca`.
  Potem: **tryb eco** (`eco_mode`, bajt 61; JSON `ecoMode`; panel Zasilanie + karta LCD 16:
  BATTERY, na zmianę Eco i On/OFF, START/STOP z odliczaniem). Na baterii po zaśnięciu ekranu:
  LCD_CR=0, bez serca i diod (także alarmu), w RUN budzenie RTC na następny pomiar (maks. 60 s),
  zatrzymany co 60 s; przycisk/USB włącza LCD (`g_lcd_hw_off`). Eco wygrywa z trybem zegarka.
  Maska menu 17 bitów 0x1FFFF - bity 16+ w bajcie 51 (wolny bajt reguły DST), konfiguracja v11.
  `clock_mode` teraz bajt 60 (u8). Zbudowane, NIEWGRANE (ST-Link bez rdzenia): bin
  `553a9eefebd0d6b63a865f98e4a9794d9d03926cbbc18193b2225e32c1c85b45`.
  Potem: tryb zegarka przełączany na karcie zegara (START/STOP z odliczaniem), osobna karta 14
  usunięta z menu (bit 14 maski nieużywany, panel bez checkboxa). Zbudowane, niewgrane: bin
  `95f1deba2e1e876d75526e08faf29df489e1f224a97197d44679edeb0ac3608b`.
  Potem: OFF przeniesiony na kartę baterii (oba przyciski 3 s), karta OFF usunięta z menu
  (bit 13 nieużywany). Zbudowane, niewgrane: bin
  `2f2a57d36e2a651be5dddd5e5e49fb4c7c23de993fa281d4e39ff0bebdd9b4df`.
  (Wszystko powyżej wypchnięte jako commit a4c8e45 - stan roboczy, bez wydania.)
- 2026-10-17: przed dużą aktualizacją: **opóźniony start** (LOGGER START IN/AT, start_at w STATUS,
  RTC_BKP3R, odliczanie M:SS/H:MM/godziny z klepsydrą i migającym STOP, START = teraz, STOP = anuluj),
  **opóźnienie alarmu** (alarm_delay_min, bajt 57; zatrzask w RTC_BKP4R; panel: krótkie wyjścia
  osobno), **ostrzeżenie baterii** (< 2,7 V ikona stale; vbatMin w raporcie), **odświeżanie dysku**
  (2x NOT READY po zmianie plików). Testy: start_alarm_vectors.py (40), panel_delay_vectors.py (11).
  Wgrane: bin `e7a2939db06ba76d76e8eb4595688e7853cb1b0216fcda29e53449b072e7b32d`; na urządzeniu
  sprawdzone przez CDC: WAIT + start_at + anulowanie. Uwaga: podczas testu przypadkiem PAUSE
  działającego loggera (~15 s przerwy, zdarzenia STOP/RESUME w logu), od razu wznowiony.
  Potem: panel premium - poświata w tle, własne strzałki select, przyciski -/+ przy polach liczbowych
  (przytrzymanie powtarza), własne checkboxy i podświetlenie zaznaczonych ekranów LCD, komunikaty
  z ikoną i kolorem wg typu (ok/bad/warn/info), własne okno potwierdzenia zamiast confirm()
  (CLR, OFF). Wgrane: bin `9454d2cf3d0021c708a24b3f1a0b50e020980ece3f6c665481d28be67a9793b0`.
  Potem: własny kalendarz (datetime-local: miesiąc, siatka dni, godzina/minuta -/+ z powtarzaniem,
  Teraz/Ustaw, min/max), przyciski -/+ obok siebie po prawej, druk: Pro jednolitym kolorem (błąd
  Chrome z background-clip:text dawał kreskę) i logo bez cienia. Wgrane: bin
  `e4a8618d0bfc5ddfb2c6602793b680555beef71a3edaf17a72d2b46f0c1a2442`.
  Potem: (1) CLEAR z panelu: urządzenie restartuje się po 0,8 s (templogpro_app_request_restart ->
  NVIC_SystemReset), panel przeładowuje stronę po 8 s - świeży dysk; czyszczenie samo działało, panel
  pokazywał stare dane. (2) SET TIME: blokada cofania zegara porównywała z najpóźniejszym ODCZYTANYM
  czasem (g_monotonic_floor), więc spieszącego zegara nie dało się poprawić nawet po CLR; teraz
  g_data_floor = najnowszy zapisany rekord (set_minimum_timestamp), CLR (logger_format) go zeruje.
  Na urządzeniu zegar był ~1 h 49 min do przodu - ustawiony na czas PC (pamięć pusta). Wgrane: bin
  `f1e55be0c993aa4fd2ca2565e45dc3caa5f321180984c6dde2489f07480d75aa`.
  (Wypchnięte jako commit 67ed5e6 - stan roboczy.)
- 2026-10-17: **kolejność ekranów LCD** z panelu (przeciąganie kafelków, numery pozycji, Przywróć
  domyślną; temperatura zawsze pierwsza). Konfiguracja `lcd_order` (14 numerów ekranów) w wolnych
  bajtach 25-27, 29-31, 33 (po 4 bity, zera = domyślna); battery/lcd_mode/pdf/led czytane jako bajty.
  JSON `lcdOrder`; HTML_CONFIG_BLOCK_SIZE 576, CONFIG_TEXT_SIZE 768. Przyciski przewijają wg kolejności
  (app_menu_card). Testy: start_alarm (54), panel_delay (17). Wgrane.

- 2026-10-17: **dioda baterii**: bateria zasila MCU przez diodę ~0,55 V (pomiar użytkownika: 3,30 V na
  baterii -> 2,75 V na padzie 3,3 V J2; logger gaśnie przy ~2,3 V na baterii). Próg 2,7 V (VDD) był za
  wysoki - świeża CR3450 by go nie przeszła. Teraz TEMPLOGPRO_FLASH_MIN_VDD_MV 2300 (W25X40CL) dla
  ikony i blokady CLR/START, procenty na napięciu ogniwa (VDD + 550 mV): 100 % od 3,15 V, 0 % przy
  2,85 V; vbatMin w raporcie jako napięcie ogniwa. Zbudowane, czeka na wgranie (ST-Link bez rdzenia).

- 2026-10-17: zakładka **Instrukcja** w panelu: 8 kart z obrazkami SVG w stylu ilustracji użytkownika
  (niebieskie urządzenie, napis z LCD, dłoń na właściwym przycisku, plakietka 3 s) + legenda znaczków,
  PL/EN, ok. 10 KB. Flash 221 KB / 256 KB. Poprawka diody wgrana. Uwaga: zegar spieszy ~37 %
  (+80 min w 2 h 11 min) - do zbadania (RTC na LSI kalibrowanym raz względem LSE).

- 2026-10-17: **RTC na kwarcu LSE** (rtc_switch_to_lse w templogpro_rtc_process po LSERDY): zapis kalendarza
  i BKP0-19, RTCRST, ponowne LSEON (RTCRST je kasuje!), czekanie <= 3 s, odtworzenie BKP, kalendarz +czas
  czekania; fallback na LSI. Hak LCD suspend zostaje na stałe, app re-inicjuje LCD po przełączeniu.
  POWER.TXT: RTC_SOURCE. Przyczyna: LSI dryfował ~37 % (+80 min / 2 h 11 min) przy zmianie napięcia.
## Znane błędy, nienaprawione

- Brak.

## Nieprzeczytane jeszcze pliki

`templogpro_rtc.c`, `templogpro_buttons.c`, `st_usb/msc/usbd_msc.c`,
`templogpro_usb_msc_storage.c`, `templogpro_usb_files.c` (panel HTML/JS/CSS).

## Uzgodniona kolejność

1. Zrobione: mapa LCD.
2. Zrobione: nowy format zapisu odporny na reset (rekordy jako źródło prawdy,
   koniec logu wyznaczany skanem przy starcie, metadane tylko przy zmianie
   stanu, pomijanie zajętych slotów, konfiguracja w dwóch kopiach).
3. Zrobione: rozbicie `main.c` na sterowniki, usunięcie martwych trybów
   diagnostycznych.
4. Aplikacja i LCD (w tym stopnie F).
5. Nowy wygląd panelu HTML (m.in. spójne polskie znaki, ukrycie surowych pól
   diagnostycznych).

## Ustalenia z użytkownikiem

- Na LCD bez napisów tekstowych typu `SEC`, `Lo`, `Hi`; tylko segmenty i ikony.
- Dane w Winbond nie są krytyczne; firmware można wgrywać ponownie. Mass erase
  i kasowanie Winbond nadal tylko świadomie.
- Użytkownik chce pracować równolegle na komputerze, więc build i wgrywanie
  mają iść z terminala, nie przez klikanie w IDE.