# Pełny system i386: porównanie przeglądarek

Próba używa rzeczywistego userspace Debiana 12 Bookworm i386, kernela 686,
Xvfb i D-Bus w pełnosystemowym QEMU TCG. Profil: jeden Pentium II Deschutes,
384 MiB RAM, dysk IDE ext4 2 GiB i potwierdzony swap 512 MiB. Gość testowy
nie ma karty sieciowej ani dostępu do sieci zewnętrznej. Włączony jest
loopback, potrzebny lokalnym pomocnikom DPI. Obie przeglądarki otwierają ten sam lokalny
`fixtures/start.html`, na ekranie Xvfb 1000×740.

## Budowanie i uruchomienie

Polecenia wykonuje się na mocniejszym komputerze amd64, w katalogu projektu:

```sh
docker build --pull -f emulation/Dockerfile.full-bookworm -t p2browser-p2-full .
mkdir -p build/full-system-results
docker run --rm --network none --user "$(id -u):$(id -g)" \
  --mount "type=bind,src=$(pwd)/build/full-system-results,dst=/results" \
  p2browser-p2-full
```

Podstawowy wynik trafi do nowego `build/full-system-results/run/`.
Kolejne uruchomienie wymaga nowego katalogu wynikowego: runner odmawia
nadpisania istniejącego. Można podać nową ścieżkę wewnątrz `/results`,
np. dopisując `sh /usr/local/bin/p2-full-run /results/run-02` po nazwie obrazu.
Sam obraz jest duży; trzeba zapewnić kilka GiB miejsca na obraz i kopię dysku.
Obraz bazowy przechowuje dysk w gzip; runner sprawdza archiwum, odtwarza
rzadki plik dysku i weryfikuje SHA-256 surowych danych przed startem.

Pełną serię ośmiu większych obciążeń uruchamia się osobno:

```sh
docker run --rm --network none --user "$(id -u):$(id -g)" \
  -e P2_GUEST_MODE=stress \
  --mount "type=bind,src=$(pwd)/build/full-system-results,dst=/results" \
  p2browser-p2-full sh /usr/local/bin/p2-full-run /results/stress-01
```

Wyniki trafiają do `extracted/p2-results/stress/summary.tsv`.
Metodę opisuje [protokół obciążeń](../docs/stress-method.md).

W środowisku z dodatkowym CA użyj podczas budowania
`--secret id=proxy_ca,src=/ścieżka/do/pakietu-CA.pem`. TLS i weryfikacja
podpisów repozytoriów pozostają włączone. Pakiety i ich zależności pobieramy
przy budowaniu; gotowa próba uruchamia się bez sieci.

Zmiany skryptów można sprawdzać bez przebudowy dużego obrazu. Runner przed
startem kopiuje aktualne skrypty do nowego dysku przez debugfs, sprawdza
ich zawartość przez ponowny odczyt i zapisuje SHA-256. Przykład użyty
przy walidacji, po wcześniejszym zbudowaniu obrazu:

```sh
docker run --rm --network none --user "$(id -u):$(id -g)" \
  -e P2_HARNESS_DIR=/harness \
  --mount "type=bind,src=$(pwd)/build/full-system-results,dst=/results" \
  --mount "type=bind,src=$(pwd)/emulation,dst=/harness,readonly" \
  p2browser-p2-full sh /harness/run-full-check.sh /results/run-03
```

Ta ścieżka musi być nowa. Modyfikowany jest wyłącznie odtwarzalny dysk
próby; bazowy obraz i biblioteki pozostają takie same. Zmiana binarium lub
zależności wymaga osobnej kompilacji i przygotowania odpowiedniego obrazu.

Opcjonalny `P2_TESTS_DIR=/tests` i montowanie `tests/` do `/tests,readonly`
pozwala analogicznie podmienić wyłącznie `test_probe.py`; runner weryfikuje
odczyt i zapisuje SHA-256. W pełnym gościu testy GUI mają mnożnik czasu 5
(`P2_GUI_TEST_TIME_SCALE`), zachowując te same asercje co na amd64.

W trybie stress można podać `P2_STRESS_SECONDS=120` i np.
`P2_STRESS_CASES=tabs-baseline,tabs-pressure`, aby ponowić tylko te przypadki.
Runner sprawdza nazwy i liczbę wyników; każda próba wymaga nowej ścieżki.

Wynik wykonanej próby: [raport porównania](../docs/p2-browser-comparison.md).

## Sposób budowania i zakres próby

PRoot 5.5.0 i statyczny QEMU user służą wyłącznie przygotowaniu userspace.
Wymuszona emulacja działa także na hoście bez natywnej obsługi ELF32,
bez rejestracji binfmt i bez uprzywilejowanego kontenera. Pobierany PRoot
ma przypiętą wersję i sprawdzaną sumę SHA-256. APT hosta sprawdza podpisy
i pobiera zależności i386; konfiguracja pakietów przebiega w emulowanym
userspace. Wykonywanie prób przeglądarek odbywa się później w pełnym gościu
TCG, niezależnie od PRoot.

Własny program C jest kompilowany z `-march=pentium2 -mno-sse -mno-sse2
-mfpmath=387`. Wymagań gotowych bibliotek nie zmieniamy tymi flagami.
Dokładne wersje są zachowywane w metadanych obrazu i raportach gościa.
Ten eksperyment nie instaluje bibliotek i386 na komputerze użytkownika.

Gość uruchamia aplikacje jako `p2test`, z włączonym sandboxem WebKitGTK.
Sprawdza lokalny dokument, wygląd na zrzutach, JS/formularz i zachowanie
w zadanym RAM/swapie. Nie sprawdza współczesnych serwisów internetowych,
kodeków, logowania ani wydajności fizycznego Pentium II 400 MHz.
WebKitGTK Bookworm służy tu eksperymentowi offline; jego zakończone wsparcie
bezpieczeństwa pozostaje przeszkodą dla docelowego utrzymywanego produktu.

## Wyniki i interpretacja

Runner zachowuje `serial.log`, kod wyjścia QEMU, kopię dysku oraz zawartość
`extracted/p2-results/`. Gość zapisuje pięć przypadków w `summary.tsv`:

1. Dillo: próba pamięci i zrzut ekranu wymagający oceny wzrokowej.
2. WebKitGTK: próba pamięci bez JS.
3. WebKitGTK: próba pamięci z JS silnika.
4. WebKitGTK: osobny test renderowania bez JS.
5. WebKitGTK: osobny test formularza, JS i CSS Flexbox.

Następnie uruchamiane są istniejące testy `test_probe.py` z włączonymi
przełącznikami CLI i GUI. Ich wynik i kod wyjścia są zapisane oddzielnie
w `probe-regression-tests.log` i `probe-regression-exit.txt`.

Mały nadzorca C używa `PR_SET_CHILD_SUBREAPER`, aby odłączone pomocnicze
procesy Dillo/DPI i silnika pozostały w mierzonym drzewie. Jego własna pamięć
jest uwzględniona w każdej próbie. Xvfb, D-Bus, monitor i narzędzie zrzutu
ekranu znajdują się poza tym drzewem. Nadzorca sprząta osierocone procesy
po zakończeniu głównego programu; testy na hoście sprawdzają to podwójnym
forkiem i odłączeniem sesji.

Suma PSS proporcjonalnie dzieli strony współdzielone. Suma RSS zawyża koszt
unikalnego RAM. `NA` oznacza brak pełnego odczytu, nie zerowe zużycie.
Największa próbka nie jest gwarancją uchwycenia szczytu pomiędzy próbkami.
Czas TCG jest czasem tego emulatora i hosta, nie symulacją 400 MHz.

Zakończenie runnera kodem 0 oznacza zebrane pięć przypadków, a nie sukces
każdego silnika. Poszczególne statusy, raporty self-test i obrazy trzeba
sprawdzić osobno. `SIGILL`, `SIGSEGV`, timeout i SIGKILL są rozdzielone;
SIGKILL wymaga sprawdzenia kernela, zanim uznamy go za OOM. Kontrolowane
zatrzymanie Dillo po czasie jest opisane jako `TIMED_STOP`.

Gotowe dyski, obrazy kontenerów i surowe raporty pozostają lokalne w `build/`.
Repozytorium zawiera kod i instrukcje eksperymentu, nie cały system Debiana.
