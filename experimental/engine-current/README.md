# Próba WebKitGTK 2.54.2 dla Pentium II

Eksperyment kompilacji, niezależny od systemowego silnika w paczce amd64.
Wynik i granice próby opisuje [raport](../../docs/engine-current-feasibility.md).
Pobierane źródła i binaria pozostają w ignorowanym `build/engine-current/`.
Potrzebne są mocniejszy komputer amd64, Docker oraz wiele GiB wolnego dysku.

```sh
sh experimental/engine-current/prepare-source.sh
docker build -f experimental/engine-current/Dockerfile \
  -t p2browser-engine-current:bookworm .
docker run --rm --network none \
  --mount "type=bind,src=$(pwd),dst=/work" --workdir /work \
  p2browser-engine-current:bookworm \
  sh experimental/engine-current/run-probe.sh
```

Pierwsza kompilacja JavaScriptCore może trwać kilkadziesiąt minut. Skrypt
ogranicza samą kompilację do 60 minut i dwóch zadań, następnie uruchamia
`smoke.js` przez `qemu-i386 -cpu pentium2`. QEMU user sprawdza tu zestaw
instrukcji CPU; nie ogranicza RAM do 384 MiB.

Próba konfiguracji portu GTK i ograniczonego czasowo budowania WebCore:

```sh
docker build -f experimental/engine-current/Dockerfile.gtk \
  -t p2browser-engine-current:gtk-bookworm .
docker run --rm --network none \
  --mount "type=bind,src=$(pwd),dst=/work" --workdir /work \
  p2browser-engine-current:gtk-bookworm \
  sh experimental/engine-current/run-gtk-probe.sh
```

Drugi skrypt jest sondą z limitem 300 s budowania. Kod wyjścia 124 oznacza
upłynięcie tego limitu, nie dowód niezgodności CPU. Nie produkuje pełnej
przeglądarki ani gotowego pakietu dla Pentium II.

`apply-compat-patches.py` wprowadza dwie wąskie poprawki wyłączenia
nieużywanego libpas na i386 z systemowym alokatorem. Weryfikuje oczekiwane
fragmenty kodu i odmawia niepasującego źródła. Licencje upstream pozostają
w pobranym archiwum. Nowe skrypty projektu mają licencję MIT.
