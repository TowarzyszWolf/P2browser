# Karty i oszczędzanie pamięci — 0.2.0~alpha1

To pierwszy działający etap sterowania pamięcią. Kod aplikacji jest w C,
interfejs korzysta z GTK 3 i systemowego WebKitGTK 4.1.

## Dostępne sterowanie

- `+ Karta` otwiera kartę; `×` ją zamyka. Domyślnie można mieć pięć kart.
- `Pamięć` zawiera wspólny suwak i pole liczbowe budżetu PSS w MiB.
  Wartość 0 wyłącza budżet. Można też zdjąć limit liczby kart.
- Pasek pokazuje PSS całego znalezionego drzewa procesów i sumę VmSwap.
  Pomiar odbywa się co około dwie sekundy. Brak pełnych danych jest
  sygnalizowany, a nie traktowany jak zerowe zużycie.
- Przekroczenie budżetu wyłącza automatyczne pobieranie kolejnych obrazów
  i czyści cache silnika. Page cache, WebGL, WebAudio i płynne przewijanie
  są już wyłączone od startu. Załadowane obrazy nie znikają automatycznie.
- `Odciąż tło` po potwierdzeniu usuwa WebView niechronionych kart w tle.
  Adres zostaje, a powrót do karty ponownie ładuje stronę. Opcja w oknie
  `Pamięć` pozwala robić to automatycznie po przekroczeniu budżetu.
- W oknie `Pamięć` można ochronić wybraną kartę przed odciążaniem.
- `Tekst` zastępuje WebView podglądem tekstu do 128 KiB. Przy JS włączonym
  źródłem jest tekst DOM; przy wyłączonym uproszczony odczyt HTML, do 1 MiB
  wejścia parsera. `Odśwież` odtwarza stronę pod zachowanym adresem.
- Przy jednoczesnym bardzo małym zapasie RAM i swapu pojawia się komunikat
  proponujący widok tekstowy. Użytkownik może kontynuować zwykłą pracę.

Ustawienia obowiązują podczas bieżącego uruchomienia; zapis preferencji
na dysku nie jest jeszcze zaimplementowany. Karty w tle są ukryte i wyciszone;
harmonogram ich pracy nadal kontroluje WebKit, chyba że zostały odciążone.

## Granice tego etapu

Budżet jest **miękki**. PSS obejmuje proporcjonalny udział współdzielonych
stron procesów aplikacji, bez całego Xorg i kernela. To nie twardy limit
kernela ani limit całej przestrzeni roboczej na dysku. Linux decyduje,
które strony pamięci przenosi do swapu. Program nie gwarantuje kierowania
wszystkich nowych alokacji do swapu po przekroczeniu zadanej liczby.

Odciążanie nie jest bezstratnym zamrożeniem. Niszczy niezapisane formularze,
stan aplikacji, historię danej karty i potencjalnie sessionStorage.
Dlatego domyślnie pozostaje wyłączone i interfejs informuje o skutkach.
Wspólny tymczasowy kontekst witryn pozostaje do zamknięcia przeglądarki;
usuwanie sesji dopiero po zamknięciu ostatniej karty danej witryny wymaga
osobnej implementacji. Zachowanie sesji po przeładowaniu zależy od strony.

Tekst jest przybliżeniem treści, bez interakcji, układu i formularzy.
Odczyt głównego zasobu HTML przez API WebKit może wcześniej zaalokować
pełny zasób; ograniczenie parsera nie jest gwarancją małego szczytu RAM.
Przy prawdziwym OOM silnik lub system może zakończyć proces, zanim uda
się przygotować podgląd. Nie jest to zapis strony offline.

## Przykłady i sprawdzanie

```sh
p2browser-probe --memory-limit-mib 160
p2browser-probe --javascript --images --memory-limit-mib 160
p2browser-probe --memory-self-test --quit-after 120
```

`--reclaim-background` oznacza zgodę na utratę stanu kontrolowanych kart
przy przekroczeniu budżetu. Pozostałe argumenty: `--background-tab URL`
(można powtarzać) i `--unlimited-tabs`.

Test `--memory-self-test` używa wyłącznie lokalnego dokumentu. Sprawdza
odrzucenie szóstej karty, ochronę jednej karty w tle, odciążenie trzech
pozostałych, przeładowanie jednej z nich i przejście do tekstu. Testy GUI
uruchamiają ten przebieg z JS wyłączonym i włączonym, z aktywnym sandboxem.
Testy polityki pamięci dodatkowo sprawdzają brakujące dane, ponowne
wykorzystanie PID i rozpoznawanie presji pamięci na kontrolowanym `/proc`.
