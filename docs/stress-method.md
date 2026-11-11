# Powtarzalne próby większych obciążeń

To protokół testów, nie deklaracja pomyślnego uruchomienia wszystkich
przypadków. Faktyczne wyniki należy zapisać po wykonaniu prób w gościu.

## Stałe dane wejściowe

Generator `fixtures/stress/generate.py NOWY_KATALOG` tworzy dane bez sieci,
zewnętrznych fontów i losowości. Manifest zawiera parametry, rozmiary plików,
SHA-256 oraz wersje Pythona i zlib. HTML i piksele są stałe; bitowa kompresja
PNG może zależeć od wersji zlib, dlatego przy porównywaniu prób zachowujemy
manifest. Generator odmawia nadpisywania istniejącego katalogu.

| Przypadek | Obciążenie i ustawienia |
| --- | --- |
| `dom-no-js` | Około 2 MiB HTML, 12000 akapitów i teksty z polskimi znakami; JS i obrazy wyłączone |
| `images-off` | Cztery lokalne PNG, automatyczne obrazy wyłączone, JS wyłączony |
| `images-on` | Ten sam dokument i PNG, automatyczne obrazy włączone, JS wyłączony |
| `js-off` | Ten sam dokument co `js-on`, lecz skrypt nie jest wykonywany |
| `js-on` | 5000 elementów utworzonych synchronicznie przez JS, zachowany bufor 48 MiB i 300000 iteracji obliczeń |
| `mixed-on` | Dwa największe PNG, 2000 elementów JS, bufor 32 MiB, te same ograniczone obliczenia |
| `tabs-baseline` | Duży dokument na pierwszym planie i dwie ukryte karty `sync-js`; brak softlimitu |
| `tabs-pressure` | Ten sam zestaw kart, softlimit 96 MiB i jawna zgoda CLI na odrzucanie WebView w tle |

PNG mają wymiary 256², 1024², 2048² i 4096². Pełne powierzchnie RGBA
zajmowałyby łącznie 84,25 MiB; to rozmiar nominalny przed optymalizacjami
dekodera, a nie przewidywany pomiar PSS. Obrazy są widoczne na początku
dokumentu, aby nie polegać wyłącznie na obrazach daleko poza ekranem.

Bufory JS są dotykane co 4096 B i pozostają osiągalne przez obiekt `window`.
Nie są jedynie nieużywaną rezerwacją przestrzeni adresowej. Pętle mają stałe
granice; nie ma timerów, pracy ciągłej ani celowego nieograniczonego OOM.
JS ustawia tytuł `P2STRESS:ready:NAZWA` dopiero po całym synchronicznym
zadaniu. Wariant `js-off` oczekuje tytułu `P2STRESS:disabled:sync-js`.

## Uruchamianie

W przygotowanym pełnym gościu i386, jako zwykły użytkownik `p2test`:

```sh
P2_STRESS_RESULTS=/var/lib/p2-results/stress \
  sh /opt/p2browser/emulation/stress-benchmark.sh
```

Katalog nadrzędny musi być zapisywalny dla użytkownika. Rootowy init gościa
przygotowuje `/var/lib/p2-results`; skrypt tworzy prywatny podkatalog.
Każda następna próba wymaga nowego katalogu wynikowego. Skrypt nie zmienia
konfiguracji hosta, swapu ani limitu RAM; te warunki ustala runner QEMU.
Wyjście na zewnętrzną sieć musi pozostać wyłączone; loopback służy lokalnemu
IPC. Wymagane są Xvfb, D-Bus, Python 3, ImageMagick `import`, przygotowany
`case-supervisor` oraz monitor projektu. Żadne z tych dodatkowych narzędzi
nie staje się przez to zależnością codziennego uruchamiania przeglądarki.

Najważniejsze zmienne środowiskowe:

| Zmienna | Domyślna wartość i znaczenie |
| --- | --- |
| `P2_PROJECT_DIR` | `/opt/p2browser`; katalog źródeł i binarium |
| `P2_STRESS_RESULTS` | `/var/lib/p2-stress-results`; nowy katalog wynikowy |
| `P2_STRESS_SECONDS` | `45`; od 15 do 180 s na przypadek |
| `P2_STRESS_CASES` | Wszystkie osiem nazw z tabeli; można podać unikalną listę oddzieloną przecinkami, bez spacji |
| `P2_STRESS_MEMORY_LIMIT_MIB` | `96`; softlimit wyłącznie dla `tabs-pressure` |
| `P2_STRESS_DISPLAY` | `:98`; osobny lokalny ekran Xvfb 1000×740 |
| `P2_STRESS_CAPTURE_DELAY_SECONDS` | `5`; opóźnienie zrzutu po gotowości, od 1 do 30 s |

Każdy proces otrzymuje osobne katalogi konfiguracji i cache. Katalog sesji
`XDG_RUNTIME_DIR` jest wspólny i musi mieć poprawnego właściciela oraz
uprawnienia przygotowane przez init. Dane stron w prototypie nadal zależą
od jego tymczasowego profilu.

Poza standardowym `--quit-after` działa zewnętrzny limit czasu dłuższy o
60 s i ostateczna kontrola drivera. Normalny przebieg ośmiu prób trwa około
6–8 minut, lecz dla serii timeoutów globalny runner musi dopuścić ponad
15 minut albo uruchamiać mniejsze podzbiory. Czas emulatora nie odwzorowuje
400 MHz fizycznego Pentium II.

## Co uznajemy za potwierdzone

Kod wyjścia 0 nie wystarcza. Przypadek musi mieć:

1. Zakończenie ładowania aktywnej strony bez błędu ani zakończenia procesu strony.
2. Zdarzenie `fixture_ready` z dopasowaniem oczekiwanego tytułu po ładowaniu.
3. Potwierdzony włączony sandbox.
4. Co najmniej jedną pełną próbkę PSS drzewa procesów.
5. W `tabs-pressure` także dodatni `memory_reclaim: background_tabs=N`.

Zrzut ekranu jest wykonywany po znaczniku gotowości i pozostaje do oceny
wzrokowej. Sama poprawna sygnatura PNG nie dowodzi, że wszystkie obrazy
zostały narysowane. Znacznik dla statycznej strony z obrazami potwierdza
ładowanie dokumentu, nie niezależny test wszystkich dekoderów. Dla JS-off
oczekiwany jest brak wygenerowanych elementów, a nie taki sam wygląd jak
po wykonaniu skryptu.

Pierwsza seria alpha1 używała opóźnienia 1 s. Biały zrzut jednego przypadku
z obrazami pokazał, że tytuł/LOAD_FINISHED może wyprzedzać widoczny paint;
dlatego domyślne opóźnienie zwiększono do 5 s. Nadal wymaga to oceny obrazu,
a nie utożsamienia zdarzenia gotowości z poprawnym narysowaniem strony.

Softlimit nie jest limitem kernela ani gwarancją ładowania nowych danych
wyłącznie do swapu. Karta aktywna nie jest automatycznie niszczona przez
ten test. `--reclaim-background` jest jawną zgodą tylko dla kontrolowanych
kart testowych; odrzucenie WebView traci niezapisany stan formularzy.

## Raportowane dane

`summary.tsv` zawiera gotowość, status, największe zaobserwowane PSS/RSS,
VmSwap, liczbę procesów i liczbę próbek z brakującym PSS. PSS obejmuje
poddrzewo przeglądarki i mały nadzorca utrzymujący odłączone pomocniki.
Python driver, Xvfb, monitor i procesy D-Bus pozostają poza tym poddrzewem,
choć korzystają z RAM tego samego gościa. Suma RSS liczy współdzielone
strony wielokrotnie. Maksimum próbek nie jest gwarancją uchwycenia szczytu.

Plik `cpu.tsv` nadzorcy powstaje z `getrusage(RUSAGE_CHILDREN)` po zebraniu
potomków. Do podsumowania trafiają czas użytkownika, systemu i suma w µs
oraz ms. Obejmuje procesy przeglądarki i zebrane osierocone pomocniki;
nie obejmuje własnego CPU nadzorcy, monitora ani drivera. To czas CPU
gościa/emulatora, nie pomiar fizycznego P2 ani liczenie jego cykli.
Brak albo niepoprawny plik CPU daje `NA`, nigdy domyślne zero.

`SIGILL`, `SIGTRAP`, `SIGSEGV`, `SIGABRT`, timeout i SIGKILL są rozdzielone.
SIGKILL wymaga sprawdzenia dziennika kernela, zanim zostanie nazwany OOM.
Skrypt stresowy nie może czytać rootowego `dmesg`; init gościa zachowuje
dziennik kernela do późniejszego zestawienia. Próbki PSS/RSS i czas CPU
obejmują także uruchamianie, a odczyt `ready_elapsed_ms` odnosi się do
zegara zdarzeń aplikacji.

Surowe logi i obrazy pozostają lokalne. Dane wejściowe są syntetyczne i
nie zawierają kont, haseł ani sesji użytkownika. Wyniki udanych i nieudanych
prób trzeba oddzielać — stan przed awarią nie jest kosztem działającej strony.
