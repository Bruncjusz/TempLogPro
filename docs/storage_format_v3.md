# Format zapisu Winbond v3 (2026-10-09)

Cel: reset albo utrata zasilania w dowolnym momencie zapisu nie może zablokować
loggera ani zgubić potwierdzonych rekordów. Kod: `Src/templogpro_logger.c`,
`Src/templogpro_config.c`, układ adresów: `Inc/templogpro_storage_layout.h`.

## Układ pamięci (bez zmian adresów rekordów)

| Adres | Zawartość |
| --- | --- |
| `0x00000` | konfiguracja, kopia B (dawniej nieużywany licznik ustawień) |
| `0x01000` | metadane loggera, sektor A (64 sloty po 64 B) |
| `0x02000` | metadane loggera, sektor B |
| `0x03000`-`0x7EFFF` | rekordy: 124 sektory × 341 slotów × 12 B = 42 284 |
| `0x7F000` | konfiguracja, kopia A |

## Rekordy

Format 12 B bez zmian: `timestamp u32`, `temperatura x256 i16`, `flagi u16`,
`sequence u16 = indeks + 1`, `CRC16-CCITT`. Rekordy z firmware 0.8.x są czytane
bez konwersji.

Rekordy są źródłem prawdy. Zdarzenia START, PAUSE i RESUME zmieniają stan
loggera przez wspólną funkcję `metadata_apply_event()`, używaną zarówno przy
zapisie, jak i przy odtwarzaniu po starcie.

## Metadane = punkt kontrolny

Metadane v3 zapisuje się tylko:

- po START / STOP / RESUME (po zapisie rekordu zdarzenia),
- przy zmianie interwału,
- po skasowaniu nowego sektora rekordów (flaga `SECTOR_READY`),
- po skanie przy starcie, jeżeli znalazł nowe rekordy,
- przy zapełnieniu pamięci.

Dla 760 próbek to 14 zapisów metadanych zamiast 777 w v2.

Metadane mają format identyczny z v2 plus pole `checkpoint_flags` (offset 56).
Odczyt v1 i v2 jest zachowany.

## Start

1. Najnowszy poprawny slot metadanych z sektorów A/B (wg `generation`).
2. Jeżeli sektor bieżącej pozycji jest „gotowy” (pozycja nie jest na początku
   sektora albo flaga `SECTOR_READY`), cały sektor jest czytany i koniec logu
   to ostatni zaprogramowany slot. Każdy zaprogramowany slot jest liczony;
   poprawne rekordy (CRC i zgodny `sequence`) są odtwarzane jako zdarzenia.
3. Sektor jest kasowany tylko przy wejściu loggera do niego, a `SECTOR_READY`
   trafia do metadanych przed pierwszym rekordem. Dlatego w gotowym sektorze
   nie ma starych rekordów z poprzedniego logu, nawet po CLEAR bez kasowania
   całej pamięci.

## Zapis rekordu

- Slot jest najpierw czytany. Jeżeli nie jest pusty (przerwany zapis), zostaje
  wyzerowany i pominięty.
- Jeżeli zapis lub weryfikacja zawiedzie, a odczyt działa, slot jest zerowany i
  pomijany. Pusta dziura w środku sektora nie kończy skanu (patrz wyżej).
- Najwyżej 4 sloty na jedną próbę; dopiero potem błąd `FLASH_WRITE`.
- Pominięte sloty liczą się do `records`. W `DATA.CSV` mają kod `0xFFFF` i
  datę 1970-01-01. Do ukrycia w panelu (punkt 5 kolejności).

## Konfiguracja

Dwie kopie: A (`0x7F000`) i B (`0x00000`), format v4 bez zmian. Zapis zawsze
kasuje i nadpisuje kopię, która nie zawiera bieżącej konfiguracji. Wygrywa
poprawna kopia z wyższym `generation`. Przerwany zapis zostawia poprzednią
konfigurację, a nie ustawienia domyślne.

## Zgodność wstecz

- Starsze firmware nie czyta metadanych v3. Po cofnięciu firmware zobaczy pusty
  log (rekordy zostają w pamięci) i może zacząć pisać od nowa. Przed cofnięciem
  zrób zrzut Winbond.
- Starsze firmware czyta tylko kopię A konfiguracji, która może być o jedną
  generację starsza.

## Testy

`tests/storage_vectors.py` kompiluje prawdziwy kod loggera i konfiguracji na
Cortex-M3, uruchamia go w unicorn z modelem NOR (kasowanie = 0xFF, programowanie
tylko zeruje bity, weryfikacja jak w `w25x40_page_program`) i:

- przerywa zasilanie w połowie każdej operacji zapisu/kasowania (815 punktów
  na świeżej pamięci, 818 po CLEAR nad starym logiem), po czym sprawdza
  wszystkie potwierdzone rekordy, spójność logu, poprawną konfigurację,
  identyczny wynik drugiego startu i dalsze logowanie;
- odtwarza stan `Err` z września (metadane v2 + w połowie zapisany slot);
- symuluje slot, którego nie da się zaprogramować;
- limituje liczbę zapisów metadanych.

Stary `templogpro_logger.c` nie przechodzi tego testu: blokuje się po przerwie
zasilania w 8. operacji, zapisuje 777 metadanych na 760 próbek i zatrzymuje
się na złym slocie. `--quick` sprawdza co siódmy punkt.

Na sprzęcie (2026-10-09): migracja z v2 zachowała 2437 rekordów i konfigurację,
5 resetów sprzętowych w trakcie logowania nie przerwało zapisu, a zapis
konfiguracji poszedł do kopii B i był czytany po resecie.
