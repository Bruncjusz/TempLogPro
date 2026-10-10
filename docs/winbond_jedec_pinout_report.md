# TempLogPro - raport testu JEDEC Winbond

Data testu: 2026-07-31

## Cel

Sprawdzono nieinwazyjnie, ktory zestaw pinow odpowiada za komunikacje z pamiecia Winbond W25X40.

Test nie wykonywal komend kasowania, zapisu ani Write Enable. Uzyto wylacznie komendy JEDEC ID `0x9F`.

## Testowane warianty

### Wariant A - aktualny sterownik projektu

- CS: PA4
- SCK: PA5
- MISO: PA6
- MOSI: PA7
- Tryb: sprzetowe SPI1

Wynik JEDEC ID:

- `EF 30 13`

Interpretacja:

- `EF` - Winbond
- `30 13` - W25X40
- wynik zgodny z oczekiwanym ID `0x00EF3013`

### Wariant B - piny z handoff / podejrzenie PCB

- CS: PB15
- SCK: PC6
- MISO: PC7
- MOSI: PC8
- Tryb testu: tymczasowy GPIO bit-bang tylko do odczytu JEDEC

Wynik JEDEC ID:

- `FF FF FF`

Interpretacja:

- brak odpowiedzi ukladu na tym zestawie pinow w obecnej konfiguracji sprzetu

## Dane diagnostyczne z RTC backup registers

Adres bazowy odczytu: `0x40002858`

Odczyt:

```text
0x40002858 : 4A454443 00000007 00EF3013 00FFFFFF
0x40002868 : 00EF3013
```

Znaczenie:

- `4A454443` - magic `JEDEC`
- `00000007` - wykonano oba testy; wariant A zgodny; wariant B wykonany, ale niezgodny
- `00EF3013` - ID z wariantu A
- `00FFFFFF` - ID z wariantu B
- `00EF3013` - oczekiwane ID

## Wniosek

Pamiec Winbond W25X40 w tym egzemplarzu odpowiada na pinach PA4/PA5/PA6/PA7 przez SPI1.

Nie nalezy przenosic dzialajacego sterownika Winbond na PB15/PC6/PC7/PC8 bez dodatkowego dowodu sprzetowego.

## Stan po tescie

Po diagnostyce przywrocono domyslny tryb aplikacji:

```c
#define FIRMWARE_MODE 5
```

Projekt zostal ponownie zbudowany i wgrany jako normalna aplikacja.

