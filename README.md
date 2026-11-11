# P2Browser 0.2.0~alpha1 — karty i oszczędzanie pamięci

Prototyp przeglądarki w C, GTK 3 i WebKitGTK. Docelowy komputer to Pentium II
400 MHz, 384 MB SDRAM i ATI 3D Rage Pro. Gotowa paczka poniżej jest przeznaczona
na **Debiana 13 amd64**, do rozwoju i prób na VM. Nie jest paczką dla P2.

## Pobierz dla Debiana 13 amd64

- [Komplet ZIP: dwa pakiety, źródła i instrukcja](https://github.com/TowarzyszWolf/P2browser/raw/refs/heads/main/dist/p2browser-0.2.0-alpha1-debian13-amd64.zip)
- [Program amd64](https://github.com/TowarzyszWolf/P2browser/raw/refs/heads/main/dist/p2browser-probe_0.2.0~alpha1_amd64-trixie.deb)
- [Wymagany pakiet diagnostyczny](https://github.com/TowarzyszWolf/P2browser/raw/refs/heads/main/dist/p2browser-diagnostics_0.2.0~alpha1_all.deb)
- [Źródła wydania](https://github.com/TowarzyszWolf/P2browser/raw/refs/heads/main/dist/p2browser-source_0.2.0~alpha1.tar.gz)

Po rozpakowaniu, w terminalu administratora:

```sh
apt update
apt install ./p2browser-diagnostics_0.2.0~alpha1_all.deb ./p2browser-probe_0.2.0~alpha1_amd64-trixie.deb
```

APT pobiera zależności z własnych repozytoriów Trixie. Nie trzeba dodawać
i386 ani repozytoriów Bookworm. Oba lokalne pakiety podaj w jednym poleceniu.
GUI uruchamiaj jako zwykły użytkownik pulpitu, bez sudo:

```sh
p2browser-probe
```

Instrukcja jest także w [INSTALACJA.txt](INSTALACJA.txt). Archiwa 0.1.1
pozostają w `dist/` bez zmiany zawartości. Nowy kod i dokumentacja są na
[MIT](LICENSE); biblioteki zachowują [własne licencje](THIRD_PARTY.md).

## Co działa w tym etapie

- Pięć kart, z możliwością zdjęcia limitu; adres, wstecz, dalej, odśwież i stop.
- Wspólny miękki budżet PSS: suwak i pole liczbowe w oknie `Pamięć`.
- Pomiar pamięci drzewa procesów, komunikat o przekroczeniu budżetu,
  wyłączenie nowych obrazów i czyszczenie cache.
- Ręczne odciążanie kart w tle oraz opcjonalne automatyczne odciążanie
  przy presji pamięci. Chronioną kartę można wyłączyć z odciążania.
- Ponowne otwarcie odciążonej karty przez przeładowanie, z jawną informacją
  o utracie niezapisanego stanu.
- Uproszczony podgląd tekstowy i propozycja jego użycia przy krytycznie
  małym zapasie RAM i swapu.
- JS i automatyczne obrazy domyślnie wyłączone, z przełącznikami.
- Włączony sandbox, wyłączone WebGL, WebAudio i page cache; multimedia
  wymagają gestu użytkownika. Żądana akceleracja sprzętowa: `NEVER`.
- Tymczasowy wspólny profil witryn, lokalne raporty techniczne i monitor
  pamięci. Log zdarzeń nie zapisuje adresów, formularzy ani treści wyjątków.

[Opis sterowania pamięcią](docs/memory-controls.md) wyjaśnia szczegóły.
Budżet **nie jest twardym limitem RAM** i nie wymusza alokacji do swapu;
zarządza nim Linux. Odciążanie usuwa WebView i niezapisany stan, więc wymaga
zgody. Nie jest bezstratnym zamrożeniem. Widok tekstowy ma ograniczony rozmiar
i nie gwarantuje uniknięcia OOM.

Nie ma jeszcze ograniczonej implementacji JS, wyjątków per witryna,
docelowego zamykania sesji po ostatniej karcie witryny, trwałych ustawień,
historii, zakładek, zapisu offline, menedżera pobierania ani modułu zdalnego.
Pobieranie plików jest anulowane. Tymczasowy profil nie zastępuje trwałego
cache ani limitu danych roboczych. Ukryte karty pozostają zarządzane przez
WebKit, o ile użytkownik nie włączy odciążania.

## Wyniki i dalsza praca nad silnikiem

- [Walidacja bieżącego etapu](docs/validation-alpha1.md).
- [Metoda większych obciążeń](docs/stress-method.md) i [wyniki](docs/stress-results-alpha1.md).
- [Próba współczesnego silnika bez SSE/SSE2](docs/engine-current-feasibility.md).
- [Wcześniejsze porównanie Dillo i WebKitGTK w pełnym gościu P2](docs/p2-browser-comparison.md).
- [Budowanie i uruchamianie pełnej emulacji](emulation/FULL-SYSTEM.md).

JavaScriptCore 2.54.2 został skompilowany dla i386 bez SSE/SSE2, z interpreterem
C Loop i wyłączonym JIT. Przeszedł lokalny test na modelu CPU Pentium II.
To wynik rdzenia JS, a nie potwierdzenie pełnego portu GTK ani współczesnych
serwisów. Paczka amd64 używa systemowego silnika Debiana 13.

Testy pełnego gościa używają osobnego stosu i386 Bookworm wyłącznie offline.
Jego zakończone wsparcie WebKitGTK nie pozwala uznać go za docelową bazę
utrzymywanej przeglądarki internetowej. Pomiary fizycznego P2 i testy witryn
nadal są potrzebne. Czas QEMU TCG nie odwzorowuje procesora 400 MHz.

Uzgodnienia produktu: [wymagania](docs/requirements.md) i [decyzje](docs/decisions.md).
Historyczna [walidacja 0.1.1](docs/validation.md) dotyczy tamtego wydania.

## Argumenty i diagnostyka

| Argument | Działanie |
| --- | --- |
| `--version` | Wersja programu, WebKitGTK i rozmiar wskaźnika, bez GUI |
| `--javascript`, `--images` | Włącz odpowiednią funkcję od startu |
| `--memory-limit-mib N` | Wspólny miękki budżet PSS, 0 = bez budżetu |
| `--reclaim-background` | Zgoda na utratę stanu kart tła przy przekroczeniu budżetu |
| `--unlimited-tabs` | Zdejmij limit pięciu kart |
| `--background-tab URL` | Otwórz dodatkową kartę w tle; można powtarzać |
| `--report-dir DIR` | Nowy katalog raportu; istniejący zostaje odrzucony |
| `--data-dir DIR` | Katalog lokalnego `start.html` |
| `--self-test` | Lokalny test HTML/CSS i formularza JS |
| `--memory-self-test` | Lokalny test kart, odciążania, wznowienia i tekstu |
| `--quit-after SECONDS` | Czas uruchomienia, do 86400 s |
| `URL` | Jedna strona pierwszoplanowa |

Przykład w zwykłej sesji graficznej:

```sh
p2browser-probe --javascript --memory-limit-mib 160
p2browser-probe --memory-self-test --quit-after 120
p2browser-collect-report ./nowy-raport-systemu
p2browser-monitor PID ./nowy-pomiar.tsv 60
```

Katalog raportu aplikacji ma uprawnienia 0700, pliki powstają z umask 0077.
Raport systemu i monitor to osobne narzędzia. Sprawdź raporty przed ich
publicznym udostępnieniem. Błąd JS nie oznacza automatycznie winy silnika;
tryb diagnozowania ograniczonego JS nie został jeszcze zaimplementowany.

## Budowanie na mocniejszym komputerze

Na Debianie 13 amd64 potrzebne są `build-essential`, `pkg-config`,
`libgtk-3-dev`, `libwebkit2gtk-4.1-dev` i `dpkg-dev`. Python 3 służy testom,
nie jest zależnością uruchomieniową przeglądarki.

```sh
make
make test
make deb
make source
```

Testy GUI wymagają Xvfb, xauth, D-Bus i działającego sandboxu:

```sh
P2_RUN_PROBE_TESTS=1 P2_RUN_GUI_TESTS=1 make test
```

Przy budowaniu w kontenerze:

```sh
docker build --platform linux/amd64 -f packaging/Dockerfile.trixie -t p2browser-build:trixie-amd64 .
docker run --rm --user "$(id -u):$(id -g)" -v "$PWD:/work" p2browser-build:trixie-amd64 make deb
```

Wymagane API to `webkit2gtk-4.1`; numer API nie jest wersją silnika.
Flagi kompilacji własnego kodu dla P2 nie zmieniają wymagań bibliotek.
Eksperymentalną kompilację współczesnego silnika opisuje
[experimental/engine-current](experimental/engine-current/README.md).
