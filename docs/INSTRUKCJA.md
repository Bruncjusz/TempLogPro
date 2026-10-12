# TempLogPro — instrukcja obsługi

Firmware 1.0.0 · [README](../README.md) · [Dokumentacja techniczna](../DOKUMENTACJA.md) · [Sprzęt i podłączenie ST-Linka](HARDWARE.md)

TempLogPro to rejestrator temperatury: mierzy temperaturę w stałych odstępach,
zapisuje pomiary w pamięci, ostrzega o przekroczeniu ustawionego zakresu,
a po podłączeniu do komputera tworzy raport z wykresem i PDF.

---

## 1. Wyświetlacz i przyciski

Urządzenie ma segmentowy wyświetlacz i dwa przyciski: **START** i **STOP**.

| Co robisz | Co się dzieje |
| --- | --- |
| krótko **START** | poprzedni ekran |
| krótko **STOP** | następny ekran |
| przytrzymaj **START** 3 s | rozpoczęcie albo wznowienie zapisu |
| przytrzymaj **STOP** 3 s | pauza zapisu |
| na ekranie **CLR** przytrzymaj **oba** przyciski 3 s | wyczyszczenie pamięci pomiarów |

Po przytrzymaniu przez 3 s na wyświetlaczu pojawia się odliczanie od 5 do 0
(`r 5…r 0` przy starcie, `S 5…S 0` przy pauzie, `C 5…C 0` przy czyszczeniu).
Trzymaj przycisk do końca odliczania. **Puszczenie przycisku w dowolnym
momencie odliczania anuluje akcję** — nic się nie zmienia.

Po 10 sekundach bez naciskania wyświetlacz wraca do ekranu temperatury. Na
baterii cyfry gasną, żeby oszczędzać energię; zostają ikony stanu. Naciśnij
dowolny przycisk, aby je przywrócić.

### Ekrany

Kolejność na urządzeniu (niektóre można ukryć w ustawieniach):

1. **Temperatura** — ostatni pomiar (po podłączeniu USB napis `USB`)
2. **Stan** — `rdY` gotowy, `run` zapis trwa, `StP` pauza
3. **Średnia** (ikona AVG)
4. **Maksimum** (MAX)
5. **Próg górny** (PROBE + MAX)
6. **Minimum** (MIN)
7. **Próg dolny** (PROBE + MIN)
8. **Czas pracy** (klepsydra + jednostka: sekundy, minuty, godziny, dni)
9. **Liczba zapisów** (PROBE)
10. **Interwał pomiaru** w sekundach (PROBE + TIME)
11. **Bateria** w procentach (szacunkowo; przy USB napis `USB`)
12. **CLR** — czyszczenie pamięci

### Znaczki i diody

| Sygnał | Znaczenie |
| --- | --- |
| ✔ (ptaszek) | wszystko w porządku, brak alarmu |
| ♥ (serce) + zielona dioda | właśnie zapisano pomiar (mignięcie przy każdym pomiarze) |
| ✖ (krzyżyk) świecący | w bieżącym zapisie było przekroczenie zakresu |
| czerwona dioda, dwa błyski co 3 s | alarm |
| strzałka ↑ / ↓ | kierunek przekroczenia (za ciepło / za zimno) |
| znak STOP | zapis jest zatrzymany |

Alarm (krzyżyk, strzałka, czerwona dioda) zostaje widoczny do wyczyszczenia
pamięci — także wtedy, gdy temperatura wróciła już do zakresu. Dzięki temu
widać, że przekroczenie się zdarzyło.

---

## 2. Pierwsze uruchomienie

1. Podłącz rejestrator do komputera kablem USB (wbudowana wtyczka).
2. Pojawi się dysk **TEMPLOGPRO**. Otwórz plik **`TempLogPro.html`** w
   przeglądarce **Chrome** lub **Edge**.
3. Przejdź do zakładki **Ustawienia** i kliknij **Połącz z urządzeniem**
   (wybierz port TempLogPro / „USB Serial Device”).
4. Ustaw interwał pomiaru, progi alarmu, jednostkę i pozostałe opcje.
5. W grupie **Czas i strefa czasowa** kliknij **Synchronizuj z komputerem**
   (ustawia zegar i strefę), potem **Zapisz w urządzeniu**.
6. W karcie **Logger** kliknij **Start** albo odłącz urządzenie i przytrzymaj
   **START** przez 3 s.

Zegar urządzenia warto ustawiać z komputera przy każdym nowym zapisie, bo
wewnętrzny zegar może się z czasem spieszyć lub spóźniać.

---

## 3. Ustawienia

| Ustawienie | Opis |
| --- | --- |
| Interwał pomiaru | co ile sekund zapisywać pomiar (1–600 s) |
| Dolny / górny próg alarmu | dozwolony zakres temperatury (°C) |
| Jednostka temperatury | °C lub °F na wyświetlaczu (raport ma osobny przełącznik) |
| Ekran po starcie | który ekran pokazać po włączeniu |
| Diody LED | włączone / wyłączone (wyłączenie oszczędza baterię) |
| Strefa czasowa | strefa, w której pokazywany jest czas (np. Europe/Warsaw) |
| Zmiana czasu | automatycznie wg strefy, ręcznie (własne dni przejścia na czas letni i zimowy) albo bez zmiany |
| Data i godzina | ręczne ustawienie zegara w czasie lokalnym wybranej strefy (przycisk **Ustaw zegar**) |
| Oszczędzanie energii na baterii | zalecane włączone |
| Praca na zasilaniu USB | *Pomiary i logowanie* albo *Tylko raporty i ustawienia* (wtedy przy USB zapis jest wstrzymany) |
| Ekrany w menu LCD | które ekrany są dostępne przyciskami |

Ustawienia są zapisywane w urządzeniu dopiero po kliknięciu **Zapisz w
urządzeniu**. Przycisk **Odczytaj ustawienia** pobiera aktualne wartości.

---

## 4. Raport

Plik `TempLogPro.html` zawsze pokazuje aktualne dane z urządzenia. Działa bez
internetu. Można go też skopiować na komputer — będzie zawierał dane z chwili
skopiowania.

- **Raport** — od razu widać, czy temperatura była w zakresie (zielony pasek)
  czy nie (czerwony, z maksimum i minimum). Pod nim kafelki, wykres i dane
  pracy. Zakres wykresu zmieniasz przyciskami **Całość / 1 h / 24 h / 7 dni /
  Własny** albo przeciągając myszą po wykresie.
- **Dane** — lista przekroczeń (kiedy, jak długo, ile stopni), zakresy pracy,
  zdarzenia i wszystkie pomiary (z filtrem i stronami). **Pobierz CSV** zapisuje
  dane do Excela.
- **Informacje** — dane urządzenia i pamięci.
- Znaczek strefy (np. **UTC+02:00 Europe/Warsaw · czas letni**) pokazuje, w jakiej
  strefie są godziny w raporcie. Fioletowa linia **CZAS LETNI / CZAS ZIMOWY**
  na wykresie oznacza zmianę czasu.
- Szare pole **BRAK DANYCH** na wykresie oznacza przerwę w zapisie, np. gdy
  urządzenie było wyłączone albo zmieniono zegar.

### PDF

1. Wybierz rodzaj: **Skrócony** (1 strona), **Raport** (2 strony) albo
   **Pełny** (ze wszystkimi pomiarami).
2. Kliknij **Generuj PDF**.
3. Wpisz ewentualne **Uwagi** i **Sprawdził** (imię i nazwisko) — pojawią się
   tylko w PDF. Puste pola zostaną do wypełnienia ręcznie.
4. W oknie drukowania wybierz **Zapisz jako PDF**.

PDF obejmuje zakres widoczny na wykresie.

---

## 5. Pamięć

Pamięć mieści **42 284 wpisy** (np. przy pomiarze co 5 minut — ponad 140 dni).
Wolne miejsce widać w zakładce **Ustawienia** (karta Logger) i **Informacje**.
Gdy pamięć się zapełni, zapis się zatrzymuje.

Czyszczenie: przycisk **Wyczyść pamięć** w panelu (z potwierdzeniem) albo
ekran **CLR** i oba przyciski przez 3 s. Przed czyszczeniem zapisz raport lub
`DATA.CSV`, jeśli dane są potrzebne.

---

## 6. Zasilanie

- Rejestrator pracuje na baterii **CR3450** (3 V); w spoczynku pobiera ok.
  16 µA, więc bateria wystarcza na długo.
- Po podłączeniu USB urządzenie zasila się z komputera i może dalej mierzyć
  (zależnie od ustawienia „Praca na zasilaniu USB”).
- Ekran **Bateria** pokazuje przybliżony stan na podstawie napięcia.

> Znany problem: przy podłączonym USB bateria jest obciążana i jej napięcie
> spada. Do czasu wyjaśnienia nie zostawiaj urządzenia długo podłączonego do
> USB z włożoną baterią.

---

## 7. Rozwiązywanie problemów

| Problem | Co zrobić |
| --- | --- |
| Nie widać dysku po podłączeniu | odłącz i podłącz ponownie; sprawdź inny port USB |
| „Połącz z urządzeniem” nie działa | użyj Chrome lub Edge; zamknij inne programy korzystające z portu COM; odśwież stronę |
| Start z panelu odrzucony (`E007`) | w Ustawieniach ustaw *Praca na zasilaniu USB: Pomiary i logowanie* albo odłącz USB i wystartuj przyciskiem |
| Na wyświetlaczu `Err` | odłącz i podłącz zasilanie; jeśli wraca — zgłoś problem, nie czyść pamięci przed zrobieniem kopii |
| Zła data w raporcie | Ustawienia → Czas i strefa czasowa → **Synchronizuj z komputerem** |
| Zmiana czasu w złym dniu | Ustawienia → Zmiana czasu: **Ręcznie** i ustaw dni przejścia |
| Krzyżyk miga, choć temperatura jest dobra | to pamiątka po wcześniejszym przekroczeniu; zniknie po wyczyszczeniu pamięci |
| Windows pokazuje starą nazwę pliku | odłącz i podłącz USB |

---

TempLogPro · autor: Brunon Kucharski · opracowane we współpracy z Codex (OpenAI) i Claude (Anthropic)
