# TempLogPro - test bateryjny 24 h

Data analizy: 2026-08-17

## Konfiguracja testu

- logger: RUN
- tryb baterii: ON
- interwal: 10 s
- zrodlo danych: `F:\DATA.CSV`
- kopia testu: `backup/battery_run_24h_2026-08-17`

## Wyniki zweryfikowane

- 7914 wszystkich rekordow
- 7913 pomiarow `M000`
- 1 zdarzenie startu `R001`
- pierwszy pomiar: 2026-07-31 19:16:37
- ostatni pomiar: 2026-08-01 17:15:16
- czas pomiedzy pierwszym i ostatnim pomiarem: 79119 s
- 7911 odstepow wynosilo dokladnie 10 s, jeden odstep wynosil 9 s
- brak bledow czujnika i bledow rekordow
- temperatura minimalna: 25.875 C
- temperatura maksymalna: 30.750 C
- temperatura srednia: 26.3802 C

## Drobne oscylacje temperatury

Widoczne na wykresie przejscia gora/dol maja krok 0.0625 C. Jest to jeden
najmlodszy bit wyniku czujnika LM75-compatible, a nie przypadkowy duzy blad.
6945 kolejnych par pomiarow mialo identyczna temperature. Pozostale drobne
zmiany byly prawie w calosci przejsciami o jeden kod w gore albo w dol.

Surowe rekordy pozostaja bez filtrowania. Ewentualne wygladzenie nalezy
realizowac tylko w prezentacji raportu albo jako jawnie konfigurowalny filtr,
aby nie tracic danych pomiarowych.

## Przyczyna dlugiego startu

Przy uruchomieniu `stats_rebuild()` odczytywal dotad kazdy rekord osobna
transakcja SPI. Dla 7913 rekordow i bateryjnego zegara MSI okolo 2.097 MHz
powodowalo to start trwajacy okolo 25 s. Skan jest wykonywany przed
uruchomieniem USB, dlatego podlaczenie kabla nie przyspieszalo tej fazy.

W przygotowanym buildzie odczyt statystyk jest wykonywany blokami po maksymalnie
32 rekordy, bez przekraczania granicy sektora. LCD pokazuje `rdy` przed skanem.

## Trwalosc metadanych Winbonda

W poprzedniej implementacji zapis kazdego pomiaru kasowal jeden z dwoch
sektorow metadanych. Przy interwale 10 s oznaczalo to tysiace kasowan na dobe.
Nowa implementacja zapisuje kolejne 64-bajtowe kopie metadanych w wolnych
slotach sektora. Drugi sektor jest kasowany dopiero po wypelnieniu 64 slotow.
Format metadanych pozostaje w wersji 2 i zachowuje zgodnosc z dotychczasowymi
danymi.

## Niewyjasnione

- Uzytkownik zmierzyl okolo 2.1 V bezposrednio na baterii przy podlaczonym USB
  i okolo 3 V po odlaczeniu USB.
- Firmware nie steruje obecnie potwierdzonym sygnalem wyboru zasilania ani
  izolacji baterii.
- Przed wgraniem kolejnego buildu trzeba zmierzyc osobno napiecie baterii,
  VDD plyty i VBUS USB wzgledem masy oraz potwierdzic tor zasilania PCB.

## Kontrola danych i raportu 2026-08-20

- Po dalszych testach pamiec zawierala 8027 rekordow, w tym 8023 pomiary.
- W kolejnosci zapisu wykryto dwa cofniecia RTC:
  - rekord 07915: cofniecie o 79103 s,
  - rekord 07953: cofniecie o 386 s.
- Surowe temperatury przed i po tych miejscach nie tworza oscylacji widocznych
  na starym wykresie. Efekt piloksztaltny powodowalo sortowanie raportu po
  znaczniku czasu, ktore nakladalo rekordy z powtarzajacymi sie godzinami.
- Raport HTML zachowuje rzeczywista date i czas w CSV, tabelach i dymkach, ale
  pozycjonuje punkty wedlug numeru sekwencji na monotonicznej osi pomocniczej.
- Linia temperatury jest przerywana przy cofaniu RTC. Krotkie odcinki po
  zmianie czasu nie sa laczone z poprzednim przebiegiem.
- Po aktualizacji potwierdzono USB MSC `TEMPLOGPRO`, CDC `COM3`, 8027 zachowanych
  rekordow oraz stan loggera `STOPPED`.
