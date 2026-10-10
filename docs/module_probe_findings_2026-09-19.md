# TempLogPro: odczyty diagnostyczne 2026-09-19

## Zakres i stan

Wgrano osobny obraz `diagnostics/module_probe/build/TempLogPro_module_probe.hex`,
z weryfikacja programatorem. Main 0.8.12-power pozostaje w kopii i w Release.
Diagnostyka nie zapisuje pomiarow i nie udostepnia raportow. LCD: `dIA`.
Ponizsze obserwacje pochodza z diagnostyki przy USB i podlaczonym ST-Link,
nie z pracy bateryjnej glownego firmware.

Surowe wyniki: `diagnostics/module_probe/results_20260919_233136/registers.json`.

## Rozpoznane elementy

| Element | Odczyt / polaczenie | Wniosek i ograniczenie |
| --- | --- | --- |
| MCU | CPUID 412FC230, DEV_ID 427, 256 KiB Flash | Cortex-M3 / STM32L1 xC, zgodny z targetem projektu STM32L152RC. Sam DEV_ID nie rozroznia wszystkich wariantow. |
| Czujnik | I2C 0x48, PB6 SCL / PB7 SDA | ACK i powtarzalne odpowiedzi z rejestrow 0..3. |
| Winbond | EF3013, SPI1, PA4 CS / PA5 SCK / PA6 MISO / PA7 MOSI | Rodzina W25X40, 4 Mbit = 512 KiB. Rewizji i obudowy nie ustalono z tego ID. |
| LCD | CR 000000CD, FCR 003C0C50, READY=1 | Sterownik LCD MCU pracuje na dotychczasowych ustawieniach. Brak identyfikatora modelu szkla. |
| RTC | RCC_CSR 14420003, PRER 007D0144 | Wybrane LSI; po przywroceniu LSION licznik dziala. Nie zmieniano kalendarza. |
| VDD | VREFINT: 3301 mV | Zasilanie MCU w tej sesji USB/SWD, nie napiecie ani prad baterii. |
| LED | PC0 zielona, PB8 czerwona | Mapowanie znane z main; w diagnostyce obie wylaczone. |
| Przyciski | PA0 START, PA1 STOP, aktywne 0 | Mapowanie z main; nie wykonano fizycznego testu naciskania w tej sesji. |
| SWD / USB | PA13/PA14; PA11/PA12 | Uzywane przez programator / dzialajacy interfejs USB. |

Rejestry MODER/AFR pokazuja konfiguracje ustawiona przez program, nie schemat PCB.
Dotychczasowy sterownik LCD ustawia szeroki zestaw pinow AF11. To nie dowodzi,
ze kazdy z nich jest fizycznie polaczony ze szklem. Nie zidentyfikowano toru
USB/bateria, elementow separujacych zasilanie ani polaczenia wyjscia ALERT.

## Czujnik: wynik odczytu

| Rejestr | Odczyt | Interpretacja przy zgodnosci TMP102/TMP112 |
| --- | --- | --- |
| 00 | 19F0, wczesniej 1A00 | 25.9375 / 26.0000 stopnia C, krok 0.0625 |
| 01, 1 bajt | 60 | Starszy bajt konfiguracji |
| 01, 2 bajty | 60A0, powtarzalny | SD=0, CR=10, EM=0, R1:R0=11 |
| 02 | 4B00 | Sprzetowy prog dolny 75 stopni C |
| 03 | 5000 | Sprzetowy prog gorny 80 stopni C |

To mocno wskazuje na **TMP102/TMP112 lub kompatybilny uklad**, ale nie pozwala
uczciwie podac jednej nazwy. Oba uklady maja te same odczytane wartosci domyslne.
Zgodnie z ich dokumentacja konfiguracja odpowiada 12 bitom i pracy ciaglej 4 Hz,
nie jednemu pomiarowi dopiero przy odczycie loggera. Progi 75/80 sa progami
sprzetowego ALERT, niezaleznymi od limitow alarmow zapisanych w CONFIG.TXT.
Nie zmieniano zadnego z tych rejestrow.

Zrodla pierwotne:
- [TI TMP102, sekcje 6.4 i 6.5](https://www.ti.com/lit/ds/symlink/tmp102.pdf)
- [TI TMP112, sekcje 7.4 i 7.5](https://www.ti.com/lit/ds/symlink/tmp112.pdf)
- [Winbond W25X40CL, dokumentacja rodziny](https://www.winbond.com/hq/support/documentation/?__locale=en&pno=W25X40CL)

## Energia: co wynika, a czego nie zmierzono

- Czujnik prawdopodobnie konwertuje takze miedzy zapisami loggera. To kandydat
  do kontrolowanego testu shutdown/one-shot, nie potwierdzona przyczyna 60-70 uA.
- Dla wskazanej rodziny oczekiwana skala oszczednosci na samym czujniku to
  mikroampery, nie miliampery. Odczyty nie wyjasniaja 16 mA z baterii przy USB.
- Winbond zwrocil status 00: BUSY=0, WEL=0. Po odczycie wysylany jest B9,
  a zegar SPI1 jest wylaczony (APB2ENR bit12=0). `power_down=1` oznacza brak
  bledu transmisji polecenia, nie bezposredni pomiar pradu ukladu.
- Po pomiarze VDD zegar ADC i HSI sa wylaczone. Rejestry przy wylaczonym zegarze
  moga czytac sie jako zero, dlatego istotne sa tez bity bramkowania RCC.
- CPU przy USB: 32 MHz, PLL/HSE aktywne; LCD i SysTick aktywne. Diagnostyka
  uzywa plytkiego WFI, nie profilu STOP glownego loggera.
- Seria odczytow I2C zajmuje okolo 15-18 ms na transakcje przy USB.
  To czas komunikacji/programu, nie dowod czasu konwersji czujnika.
- Nie wykonywano pomiaru amperomierzem, wiec brak wyniku nowego poboru.

## RTC podczas programowania

Pierwszy odczyt po NRST mial wybrane LSI, ale oscylator byl wylaczony i LCD
nie wystartowal. Diagnostyka teraz wznawia tylko LSION, bez resetu domeny,
zmiany RTCSEL, preskalerow i kalendarza. Odczyt kalendarza potem wrocil do
2026-08-25 20:50:25, a licznik sekund dziala. To nie jest aktualna data PC;
diagnostyka celowo jej nie przestawia. W czasie programowania / pierwszej wersji
testowej RTC mogl stac. Przed nowym testem loggera trzeba zsynchronizowac czas.

## Kopie i weryfikacja

Kopia main: `backup/main_before_module_diag_20260919_231527/`.

| Plik / sprawdzenie | SHA256 / wynik |
| --- | --- |
| Main BIN, kopia i Release | DF4C05F0F1832B16CECBC1F1FDEF0A85D08C5DB427856A89AD87B971A0F56784 |
| Diagnostyka BIN | 3F51A143F10D8E683DACF6A3787681669DBA031AB3091A3D9FFCF74371E07E5F |
| EEPROM 8192 B, przed i po | 9F1DCBC35C350D6027F98BE0F5C8B43B42CA52B7604459C0C42BE3AA88913D47 |
| Winbond 524288 B, dwa odczyty | 0DC95557D52DD99DE95122EC768E89E71CCF9A5A2F11E3C0571331A655DE4BDE |
| ARM test gotowej binarki | 64 sprawdzenia PASS |
| Kontrola symboli | Brak loggera/config/zapisu i kasowania flash/original console handler |
| Rozmiar diagnostyki | text 24048, data 592, bss 6264 B |

Nie bylo surowej kopii Winbonda bezposrednio przed pierwszym wgraniem. Sa kopie
plikow USB sprzed testu oraz dwa identyczne pelne odczyty juz w diagnostyce.
Brak modyfikacji pomiarow wynika tez z wykluczenia kodu zapisu z binarki, nie
z porownania surowych obrazow sprzed i po calym eksperymencie.

## Nastepny kontrolowany etap

1. Osobny, odwracalny test czujnika: zapamietac konfiguracje, sprawdzic shutdown
   i gotowosc pojedynczej konwersji, przywrocic dokladnie pierwotna konfiguracje.
   Bez skanowania nieudokumentowanych rejestrow i bez general-call reset.
2. Porownac prad zasilania samej baterii przed/po, bez USB i ST-Link. Bez tego
   nie przypisywac oszczednosci do konkretnego elementu.
3. Dopiero po potwierdzeniu zgodnosci protokolu przeniesc mala zmiane do main,
   zachowujac harmonogram, alarmy i reakcje przyciskow.
4. Wrocic do main przez `tools/restore_main_after_probe.ps1`, sprawdzic czas,
   status loggera i pierwszy zapis. Narzedzie restore jest gotowe, lecz nie
   zostalo uruchomione: na urzadzeniu pozostaje diagnostyka.
