# P2Browser 0.1.1 — lokalny prototyp pomiarowy

## Pobierz wersję dla Debiana 13 amd64

- [Komplet ZIP: obie paczki, źródła i instrukcja](https://github.com/TowarzyszWolf/P2browser/raw/refs/heads/main/dist/p2browser-0.1.1-debian13-amd64.zip)
- [Program — pakiet amd64](https://github.com/TowarzyszWolf/P2browser/raw/refs/heads/main/dist/p2browser-probe_0.1.1_amd64-trixie.deb)
- [Wymagany pakiet diagnostyczny](https://github.com/TowarzyszWolf/P2browser/raw/refs/heads/main/dist/p2browser-diagnostics_0.1.1_all.deb)
- [Archiwum źródeł wydania 0.1.1](https://github.com/TowarzyszWolf/P2browser/raw/refs/heads/main/dist/p2browser-source_0.1.1.tar.gz)

Po rozpakowaniu kompletu instaluj **obie paczki w jednym poleceniu** zgodnie
z plikiem INSTALACJA.txt. GUI uruchamiaj jako zwykły użytkownik sesji graficznej.

## Bieżące prace w źródłach na main

Po wydaniu 0.1.1 dodaliśmy [test profilu P2 w QEMU](emulation/README.md)
oraz pomiar PSS w monitorze pamięci. Analizę zgodności CPU i dalsze kryteria
wyboru silnika opisuje [etap P2](docs/p2-feasibility.md).
Pierwsze próby opisuje [raport etapu P2](docs/p2-validation.md).
W pełnym gościu i386 z profilem Pentium II i 384 MiB RAM uruchomiliśmy już
Dillo oraz prototyp WebKitGTK; lokalny formularz JS i Flexbox działają
w WebKitGTK. Wyniki pamięci, obrazy i granice próby zawiera
[porównanie przeglądarek](docs/p2-browser-comparison.md).
Zrzut przesłany przez użytkownika przedstawiał osobną próbę na VM amd64.

Gotowe paczki i archiwa w `dist/` zachowują zawartość wydania 0.1.1.
Nowe narzędzia są dostępne w źródłach na `main`, niezależnie od tych paczek.

Wydanie 0.1.1 zostało **przygotowane ponownie na podstawie kodu i ustaleń
z rozmowy** po zmianie środowiska. Nie jest deklaracją identycznego bajtowo
odtworzenia dawnych plików lub pakietów 0.1.0.

Ta wersja służy sprawdzeniu działania WebKitGTK i zbieraniu pomiarów. Ma jeden
widok strony; nie realizuje jeszcze wszystkich uzgodnionych funkcji przeglądarki.
Nowy kod oraz dokumentacja projektu są objęte [MIT](LICENSE). Biblioteki mają
własne licencje opisane w [THIRD_PARTY.md](THIRD_PARTY.md).

**Bieżące pakiety GUI są natywne dla Debiana 13 Trixie amd64**, zgodnie
z systemem testowym użytkownika, Debianem 13.7 amd64. Nie wymagają włączania
architektury i386 ani dodawania repozytoriów Debiana 12.

Docelowy słaby komputer pozostaje osobnym etapem: Pentium II 400 MHz,
384 MB SDRAM i ATI 3D Rage Pro AGP 2X. Nie ma jeszcze potwierdzonego pomiaru
tej wersji na fizycznym P2 lub w pełnym systemie z 384 MB RAM. Sukces natywnej
kompilacji amd64 nie potwierdza zgodności z Pentium II.

Ustalenia produktu i decyzje techniczne opisują
[docs/requirements.md](docs/requirements.md) oraz
[docs/decisions.md](docs/decisions.md).

Wyniki prób dla tej kompilacji: [docs/validation.md](docs/validation.md).
Skrócona instrukcja instalacji znajduje się też w [INSTALACJA.txt](INSTALACJA.txt).

## Co zawiera prototyp

- Jeden widok strony, adres, wstecz, dalej, odświeżanie i zatrzymanie.
- JS domyślnie wyłączony. Przełącznik włącza JS oferowany przez silnik;
  odświeżenie stosuje ustawienie od początku ładowania.
- Automatyczne obrazy domyślnie wyłączone, z przełącznikiem ich ładowania.
- Tymczasowy profil danych stron oraz oszczędny model cache
  `DOCUMENT_VIEWER`, z wyłączonym page cache.
- Włączony sandbox WebKitGTK.
- Wyłączone WebGL i WebAudio; odtwarzanie mediów wymaga gestu użytkownika.
- Żądanie polityki `HARDWARE_ACCELERATION_POLICY_NEVER`. Skuteczność tej
  polityki zależy także od wersji silnika i stosu graficznego.
- Lokalne raportowanie zdarzeń i pamięci procesu interfejsu oraz oddzielne
  skrypty inwentaryzacji systemu i monitorowania drzewa procesów.
- Lokalną stronę testową bez zasobów z internetu i tryb jej automatycznego testu.

Nie ma jeszcze pięciu kart, oszczędnego JS, wyjątków dla witryn, limitu danych
roboczych, sterowania RAM/swapem, docelowych reguł kończenia sesji, historii,
zakładek, zapisu offline, pełnego menedżera pobierania ani modułu zdalnego.
Docelowe ograniczenia animacji, Canvas 2D i fontów także nie są kompletne.
Pobieranie plików w prototypie jest anulowane. Profil tymczasowy nie zastępuje
docelowego trwałego cache i polityki danych witryn.

## Instalacja na Debianie 13 amd64

Potrzebne są **obie paczki z tego samego wydania**:

- `p2browser-diagnostics_0.1.1_all.deb`;
- `p2browser-probe_0.1.1_amd64-trixie.deb`.

W katalogu zawierającym te pliki uruchom jako administrator:

```sh
apt install ./p2browser-diagnostics_0.1.1_all.deb ./p2browser-probe_0.1.1_amd64-trixie.deb
```

Jeśli terminal już działa jako root, polecenie nie potrzebuje `sudo`.
W terminalu zwykłego użytkownika z uprawnieniami sudo użyj `sudo apt install`
z tymi samymi dwiema ścieżkami. APT rozwiązuje zależności z repozytoriów systemu.
Nie używaj `--force-depends`, nie włączaj i386 i nie mieszaj repozytoriów
Bookworm z Trixie, aby zainstalować to wydanie.

Pakiet GUI zawiera minimalne wymagania ABI wynikające z faktycznego linkowania.
Nie zakładamy, że sama nazwa `libwebkit2gtk-4.1-0` wystarcza niezależnie od jej
wersji. Architektura i wydanie w nazwie pakietu opisują jego kompilację.

**GUI uruchamiaj jako zwykły użytkownik sesji graficznej, bez sudo:**

```sh
p2browser-probe
p2browser-probe --version
```

Jeżeli instalowałeś paczki z terminala roota, uruchom aplikację z osobnego
terminala zwykłego użytkownika pulpitu. Nie wyłączaj sandboxu w celu obejścia
problemów uruchomienia.

## Argumenty programu

| Argument | Działanie |
| --- | --- |
| `--version` | Wersja programu i WebKitGTK oraz rozmiar wskaźnika, bez GUI |
| `--javascript` | JS silnika włączony od początku uruchomienia |
| `--report-dir DIR` | Nowy katalog raportu; istniejący katalog jest odrzucany |
| `--data-dir DIR` | Katalog z lokalnym `start.html` |
| `--self-test` | Test wyłącznie lokalnej strony; nie przyjmuje URL |
| `--quit-after SECONDS` | Ograniczenie czasu uruchomienia |
| `URL` | Opcjonalny pojedynczy adres strony |

Bez adresu program otwiera lokalną stronę testową. Bez `--report-dir` tworzy
nowy prywatny katalog tymczasowy. Wypisuje ścieżkę raportu i PID interfejsu.
Katalog nadrzędny jawnie wskazanego raportu musi istnieć.

Po zbudowaniu ze źródeł można uruchomić bez instalowania:

```sh
./build/p2browser-probe --data-dir ./fixtures --report-dir ./build/probe-run-01
```

## Raporty i pomiary

`events.tsv` zapisuje czas od uruchomienia, rodzaj zdarzenia i ograniczone dane
techniczne; jego rozmiar jest ograniczony do około 1 MiB. Dziennik aplikacji
nie zapisuje URL, ciasteczek, haseł, treści formularzy ani surowych komunikatów
błędów pochodzących ze strony. Podstawowe zdarzenia JS obejmują linię/kolumnę
błędu lub nieobsłużone odrzucenie Promise. Nie jest to jeszcze pełny raport
przyczyn niezgodności ani niezawodny detektor awarii funkcji strony.

Biblioteki i procesy silnika mogą niezależnie wypisywać komunikaty na stderr.
Takiego wyjścia nie należy automatycznie uznawać za zanonimizowany dziennik
aplikacji. Raporty pozostają lokalne, bez automatycznej wysyłki.

Wpisy pamięci w aplikacji dotyczą **samego procesu interfejsu**, nie całej
przeglądarki. Osobny monitor obejmuje także potomne procesy silnika:

```sh
p2browser-collect-report ./system-report-01
p2browser-probe --report-dir ./probe-run-01 --quit-after 40 &
p2_pid=$!
p2browser-monitor "$p2_pid" ./process-tree-01.tsv 30
wait "$p2_pid"
```

Używaj nowych katalogów i plików: narzędzia nie nadpisują istniejących raportów.
Bez instalacji odpowiednikami są `sh tools/collect-report.sh` oraz
`sh tools/monitor.sh`.

Monitor podaje m.in. sumę RSS, sumę `VmSwap`, liczbę procesów i wolny swap
systemu. **Suma RSS wielokrotnie liczy współdzielone strony pamięci; nie jest
unikalnym zużyciem RAM.** Dane są próbkami zmieniającego się drzewa procesów.
Brakujące dane mają być oznaczone jako nieznane, a nie jako zużycie zerowe.
Skrypty nie nakładają limitów i nie zmieniają konfiguracji swapu.

Rozwojowy `sh tools/monitor.sh PID NOWY_PLIK.tsv SEKUNDY` na `main` dopisuje
kolumny `sum_pss_kib` i `pss_unreadable_processes`. PSS proporcjonalnie dzieli
współdzielone strony, zamiast liczyć je w całości dla każdego procesu.
Jeśli brakuje PSS któregokolwiek wybranego żywego procesu, suma wynosi `NA`.
Na nowszych kernelach czytany jest `smaps_rollup`, na starszych `smaps`.
Odczyt starszego formatu może zwiększać koszt monitorowania. Pierwsze siedem
kolumn pozostaje zgodne z monitorem z wydania 0.1.1; dwie nowe są na końcu.

Inwentaryzacja wybiera dane CPU, pamięci, GPU i wersji pakietów. Nie zbiera
nazwy komputera, loginu, numerów seryjnych, konfiguracji sieci, URL ani linii
poleceń procesów. Przed przekazaniem raport można obejrzeć.

## Budowanie i testy

Do bieżącego natywnego budowania na Debianie 13 używaj bibliotek jego własnych
repozytoriów. Potrzebne są kompilator C, Make, `pkg-config`, pliki deweloperskie
GTK3 i WebKitGTK oraz narzędzia dpkg. To wydanie wymaga API
`webkit2gtk-4.1`; numer API nie jest numerem wydania silnika.

Python 3 służy testom na komputerze budowania, a Xvfb, xauth i D-Bus lokalnym
testom GUI bez ekranu. Python nie jest zależnością uruchomieniową programu
ani skryptów diagnostycznych.

```sh
make
make test
make smoke-test
make deb
make source
```

`make deb` tworzy oba pakiety w `dist/`, a `make source` archiwum źródeł wraz
z dokumentacją, bez raportów i binariów. Test GUI wymaga dostępnych mechanizmów
sandboxu jądra; problemów z nimi nie obchodzimy wyłączaniem sandboxu.

Przykład osobnego lokalnego testu z JS w zwykłej sesji graficznej:

```sh
./build/p2browser-probe --data-dir ./fixtures --report-dir ./build/self-test-js-01 --self-test --javascript --quit-after 30
```

Test zapisuje `self-test.json` i, gdy silnik udostępni zrzut, `snapshot.png`.
Dotyczy dokumentu w `fixtures/`: prostego HTML/CSS i formularza JS przy
włączonym JS. Nie jest testem dziesięciu serwisów odniesienia, wydajności P2
ani wszystkich funkcji przeglądarki. Wyniki rzeczywiście wykonanych testów
należy podawać osobno od samej instrukcji ich uruchomienia.

Opcjonalne środowisko kontenerowe Debiana 13 amd64:

```sh
docker build --platform linux/amd64 -f packaging/Dockerfile.trixie -t p2browser-build:trixie-amd64 .
docker run --rm --platform linux/amd64 --user "$(id -u):$(id -g)" -v "$PWD:/work" p2browser-build:trixie-amd64 make deb
docker run --rm --platform linux/amd64 --user "$(id -u):$(id -g)" -v "$PWD:/work" p2browser-build:trixie-amd64 make source
```

Obraz instaluje zależności wewnątrz środowiska budowania. Zachowaj wymagane
proxy i zaufane certyfikaty własnego środowiska; nie wyłączaj weryfikacji TLS.
Przeglądarkę i kompilację źródeł uruchamiaj jako zwykły użytkownik.

## Stary sprzęt i wcześniejsze kompilacje

Pakiet amd64 Trixie nie jest pakietem dla Pentium II i nie powinien zastępować
testów tego CPU. Ewentualny przyszły wariant i386 wymaga osobnego budowania,
przeglądu instrukcji bibliotek i pełnych prób. Flagi naszego kodu bez SSE/SSE2
nie dowodzą zgodności całego stosu GTK/WebKitGTK/JavaScriptCore.

Uruchomienie samego `--version` pod QEMU user z modelem P2 sprawdza jedynie
część ładowania i wykonania. Nie obejmuje automatycznie pełnego silnika,
renderowania i wszystkich procesów potomnych. Pełna emulacja systemu oraz
pomiary fizycznego sprzętu pozostają potrzebne.

Wcześniejsze próby rozważały WebKitGTK z Debiana 12 Bookworm. Ten stary pakiet
zakończył wsparcie bezpieczeństwa, co opisuje
[Debian #1135152](https://bugs.debian.org/1135152). Dotyczy to tamtej bazy;
nie oznacza zakończenia wsparcia bieżących bibliotek Debiana 13. Obecny build
korzysta z bibliotek Trixie, które należy utrzymywać aktualne przez systemowy
menedżer pakietów.
