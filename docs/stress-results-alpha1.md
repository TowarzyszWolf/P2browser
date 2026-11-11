# Wyniki większych obciążeń — 0.2.0~alpha1

Data: 11 listopada 2026. Pełny QEMU TCG: jeden Pentium II Deschutes,
384 MiB RAM (373108 KiB widoczne dla kernela), aktywny swap 524284 KiB,
Bookworm i386, kernel 6.1.0-54-686, WebKitGTK 2.50.6, GTK 3.24.38.
Zwykły użytkownik p2test, sandbox włączony, Xvfb 1000×740, brak sieci
zewnętrznej. To eksperyment offline na starszym stosie, nie gotowy port
utrzymywanego silnika dla P2.

## Pierwsza seria: 45 sekund na przypadek

| Przypadek | Wynik zdarzeń | PSS max MiB | VmSwap max MiB | CPU gościa s | Gotowość s |
| --- | --- | ---: | ---: | ---: | ---: |
| `dom-no-js` | READY | 210.0 | 0.0 | 9.56 | 30.7 |
| `images-off` | READY | 156.6 | 0.0 | 7.82 | 10.1 |
| `images-on` | READY | 270.8 | 0.0 | 11.93 | 9.1 |
| `js-off` | READY | 149.4 | 0.0 | 11.96 | 10.6 |
| `js-on` | READY | 217.9 | 0.0 | 18.71 | 20.4 |
| `mixed-on` | READY | 301.3 | 0.0 | 13.73 | 12.8 |
| `tabs-baseline` | LOAD_NOT_CONFIRMED | 312.7 | 31.4 | 9.57 | — |
| `tabs-pressure` | PASS_RECLAIM | 204.6 | 0.0 | 11.39 | 36.1 |

Surowa [tabela TSV](results/alpha1-stress-45.tsv) i [profil](results/alpha1-stress-45-profile.json).
Metodę, zakres CPU i ograniczenia PSS opisuje [protokół](stress-method.md).
Wszystkie próbki miały pełny PSS. Nie stwierdzono SIGILL, SIGSEGV, zakończenia
procesu strony ani wpisu OOM kill w dzienniku kernela tej serii.

`READY` oznacza zakończenie ładowania i dopasowanie tytułu, nie automatyczną
ocenę obrazu. Oceniono zrzuty DOM, JS, mieszany i odciążania: treść jest
widoczna; w mieszanym również oba duże PNG i wynik JS. `images-off` pokazuje
puste pola obrazów zgodnie z ustawieniem. Zrzut `images-on` wykonany sekundę
po gotowości był biały, więc ta pierwsza próba nie potwierdza wizualnie
poprawnego narysowania czterech PNG. Tę wątpliwość rozstrzyga opisane poniżej powtórzenie.

Wariant trzech kart bez odciążania nie zakończył ładowania aktywnej strony
przed watchdogiem. Jego PSS opisuje stan w trakcie pracy; nie wolno go
przedstawiać jako kosztu w pełni załadowanego zestawu. Dwa zdarzenia
`tab_load finished` potwierdzają ładowanie kart w tle. Pojawiło się około
31,4 MiB sumy VmSwap. To powód do ponowienia z dłuższym czasem, a nie
dowód niezgodności strony.

Wariant z budżetem 96 MiB odrzucił dwie karty w tle i osiągnął gotowość
aktywnego dokumentu. Największa próbka PSS wyniosła około 204,6 MiB, czyli
nadal przekraczała budżet. To potwierdzenie działania polityki odciążania,
nie dotrzymania twardego limitu 96 MiB. Nie porównujemy go bez zastrzeżeń
z nieukończonym wariantem bazowym.

Pojedyncza seria i próbkowane maksima nie ustalają stabilnych średnich.
CPU pochodzi z getrusage gościa; czas TCG nie odwzorowuje 400 MHz.
Nie testowano prawdziwego OOM, publicznych serwisów, logowania, multimediów
ani fizycznego Pentium II.

## Powtórzenie kart: 120 sekund na przypadek

| Przypadek | Wynik | PSS max MiB | VmSwap max MiB | CPU gościa s | Gotowość s |
| --- | --- | ---: | ---: | ---: | ---: |
| `tabs-baseline` | READY | 320.9 | 18.4 | 16.23 | 70.6 |
| `tabs-pressure` | PASS_RECLAIM | 217.6 | 0.0 | 13.99 | 32.6 |

[Dane TSV](results/alpha1-tabs-120.tsv) i [profil](results/alpha1-tabs-120-profile.json).
Oba warianty osiągnęły gotowość aktywnego dokumentu; zrzuty pokazują treść.
W bazowym obie karty w tle zakończyły ładowanie przed aktywną. W oszczędnym
dwie karty zostały odciążone około 7,9 s po starcie, a ich zawartość wymaga
ponownego ładowania przy powrocie. Nie wykonują więc całej pracy wariantu
bazowego: to zamierzony koszt funkcjonalny odciążania.

Największe zaobserwowane PSS spadło z 320,9 do 217,6 MiB, o około 32%.
Bazowy osiągnął gotowość po 70,6 s, oszczędny po 32,6 s. Są to obserwacje
jednej pary prób TCG, a nie gwarancja takiego przyspieszenia na sprzęcie.
Budżet 96 MiB nadal nie został dotrzymany. Nie było braków PSS, sygnału
awarii silnika ani OOM kill.

## Powtórzenie obrazów: 90 sekund, zrzut 5 sekund po gotowości

`images-on` ponownie osiągnął gotowość, z włączonym sandboxem i bez awarii.
Zrzut pokazuje wszystkie cztery kolorowe PNG (256², 1024², 2048², 4096²)
oraz ich podpisy. W tej próbie potwierdzono więc również widoczne narysowanie
obrazów; pierwszego białego zrzutu nie uznano za sukces wizualny.

PSS max: 268,9 MiB; VmSwap max: 0; CPU gościa: 16,95 s; gotowość:
12,3 s. Zachowano [dane TSV](results/alpha1-images-90.tsv) i
[profil](results/alpha1-images-90-profile.json). Brak niepełnych próbek PSS
oraz wpisów OOM kill, segfault i nielegalnych instrukcji.

Ostatecznie każdy z ośmiu rodzajów obciążenia ma potwierdzoną gotowość
w co najmniej jednej próbie. Pierwszy niedokończony przebieg trzech kart
i biały zrzut pozostają opisane, a nie zastąpione wynikiem udanym.
