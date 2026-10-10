# TempLogPro - optymalizacja baterii 2026-08-20

## Zmiany

- Zachowano MSI okolo 2,1 MHz i `WFI` w profilu bateryjnym.
- Pelne menu LCD nadal jest odswiezane co 250 ms.
- Wygaszony ekran jest odswiezany co 600 ms, a pomiar wymusza natychmiastowy
  impuls serca.
- Odswiezenie tla aplikacji zmieniono z 1 s na 5 s na baterii. Na USB nadal
  wynosi 1 s.
- Zielony impuls LED po pomiarze skrocono na baterii z 600 ms do 120 ms.
  Na USB pozostaje 600 ms. Alarm czerwony pozostaje 100 ms co 1,1 s.
- Sprawdzenie obecnosci hosta USB bez aktywnego polaczenia zmieniono z 2 s na
  5 s. Aktywny stos USB nie zostal spowolniony.
- Winbond W25X40CL jest usypiany komenda `B9h` po kazdej operacji i budzony
  komenda `ABh` przed kolejnym dostepem. Zachowano wymagane opoznienie `tRES1`.
- Docelowy obraz zbudowano jako `Release -Os`.

## Fakty zweryfikowane

- Testy `button_fsm_vectors.py` i `logger_runtime_vectors.py`: OK.
- Debug: 0 bledow, 0 ostrzezen.
- Release `-Os`: 0 bledow, 0 ostrzezen.
- Release: text 119908 B, data 520 B, bss 14264 B.
- Obraz Release zostal zapisany do STM32 i zweryfikowany przez
  STM32CubeProgrammer 2.23.0.
- Po resecie diagnostyka przez SWD: RTC OK, czujnik OK, Flash OK, brak bledu.
- Logger zostal zainicjalizowany i zachowal 7958 rekordow.
- Odbudowa statystyk zakonczyla sie.
- Flaga sterownika Winbonda stabilnie wskazuje Power-down pomiedzy operacjami.
- SHA-256 `Release/TempLogPro.elf`:
  `4133F2282BFD4F15F9CD18A1CFDABB05D3415BFABED9506154AAB7E38990A339`.

## Niezweryfikowane zalozenia

- Nie wykonano pomiaru pradu o rozdzielczosci mikroamperowej.
- Kabel danych USB nie byl widoczny w Windows podczas tego testu, dlatego po
  zmianie okresu wykrywania nie potwierdzono czasu enumeracji MSC/CDC.
- Stan przyciskow i wyglad LCD po tej konkretnej kompilacji wymagaja krotkiego
  testu uzytkownika na urzadzeniu.
- Nie wlaczono jeszcze STOP ani tickless idle. Nadal wystepuje 100 wybudzen
  SysTick na sekunde.

## Nastepny etap

Nastepna wersja powinna dodac osobny, testowalny profil `STOP` z wybudzaniem
przez RTC oraz PA0/PA1. Musi zachowac natychmiastowa reakcje przyciskow,
dzialanie LCD i pewne wykrycie ponownego podlaczenia USB. Ten etap powinien byc
wdrazany oddzielnie, z mozliwoscia szybkiego powrotu do zwyklego `WFI`.

Etap zostal wykonany pozniej tego samego dnia. Aktualny opis znajduje sie w
`docs/battery_stop_mode_2026-08-20.md`.
