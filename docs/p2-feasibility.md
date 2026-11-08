# Etap P2: zgodność procesora i pomiary pamięci

Stan prac: 8 listopada 2026 r., po wydaniu 0.1.1.
Użytkownik potwierdził, że ostatni zrzut działającego programu pochodzi
z maszyny wirtualnej. Fizyczny Pentium II jest obecnie niedostępny.

Wykonane próby oraz pomiary opisuje [p2-validation.md](p2-validation.md).

## Co można sprawdzić bez komputera użytkownika

Pełnosystemowe QEMU TCG z jednym procesorem `pentium2` i 384 MiB RAM pozwala
sprawdzać instrukcje CPU, uruchomienie systemu i później działanie aplikacji
w ograniczonej pamięci gościa. KVM oraz samo ograniczenie pamięci nowoczesnej
VM nie zapewniają takiego modelu instrukcji procesora.

Model QEMU nie odtwarza szybkości Pentium II 400 MHz, karty ATI Rage Pro,
wydajności magistrali ani dysków IDE/CF/SD. Czas wykonania w emulatorze jest
wynikiem dla konkretnego hosta i QEMU, nie prognozą czasu na fizycznym P2.
Również `icount` nie zapewnia dokładności cyklowej.

Pierwszy test opisany w [emulation/README.md](../emulation/README.md) jest
minimalnym testem kernela, profilu CPU, RAM i inwentaryzacji w initramfs.
Nie uruchamia pulpitu ani przeglądarki. Jego zaliczenie nie potwierdza jeszcze
działania WebKitGTK przy 384 MiB RAM.

## Istotna granica Debiana 13

Oficjalne pakiety i386 Debiana 13 wymagają SSE2. Dystrybucja nie dostarcza
już oficjalnego instalatora i kernela i386; te pakiety służą przede wszystkim
uruchamianiu starszych aplikacji na systemach amd64. Pentium II nie ma SSE
ani SSE2, więc zamiana architektury obecnej paczki z amd64 na i386 nie wystarczy.

Do pierwszego eksperymentu CPU wybieramy kernel i podstawowe narzędzia i386
z Debiana 12 Bookworm. Jego bazowe wymagania i686 nie obejmują SSE2.
Wersja amd64 dla Debiana 13 pozostaje osobnym wariantem testowym. Zgodność
przeglądarki z Debianem 9 i kolejnymi wydaniami wymaga odrębnego sprawdzenia
ABI, bibliotek i utrzymania, a nie samej deklaracji w nazwie pakietu.

## Ocena WebKitGTK

- Bieżący prototyp korzysta z GTK3 i WebKitGTK API 4.1. Wyłączenie JS w GUI
  nie zmienia instrukcji maszynowych używanych przez biblioteki.
- Reguły budowania WebKitGTK 2.50.6 dla Bookworm i386 włączają interpreter
  CLoop, wyłączają JIT i zawierają poprawkę usuwającą globalne wymuszenie
  SSE2. To kandydat do lokalnego eksperymentu zgodności.
- W Trixie usunięto tę poprawkę i włączono JIT dla i386. Same flagi kompilacji
  naszego pliku C nie zmienią gotowych bibliotek dystrybucji.
- Zgłoszenie Debian #1126591 dokumentuje rzeczywisty błąd SIGILL na procesorze
  bez SSE2. Dlatego trzeba zapisywać dokładne wersje wszystkich bibliotek
  i sprawdzać renderowanie, a nie tylko uruchomienie `--version`.
- WebKitGTK w Bookworm zakończył wsparcie bezpieczeństwa. Stary pakiet może
  posłużyć do testu na lokalnej stronie, ale nie realizuje wymagania stale
  aktualizowanej przeglądarki. Aktualny port bez SSE2 i jego utrzymanie byłyby
  dodatkową pracą. Nie zostały jeszcze wykonane.

Na tym etapie WebKitGTK nie jest zatwierdzonym silnikiem docelowym dla P2.
Dillo będzie punktem odniesienia dla pamięci i CPU; jego brak JavaScript
oraz ograniczenia HTML/CSS wykluczają traktowanie samego Dillo jako realizacji
wszystkich wymagań nowoczesnych aplikacji WWW.

## Następna bramka: lokalny dokument i całe drzewo procesów

1. Uruchomić osobno Dillo i kandydata P2Browser w tym samym gościu i386,
   z identycznym dokumentem lokalnym, rozdzielczością i ustalonym swapem.
2. Zachować sandbox przeglądarki i rozróżniać SIGILL, brak pamięci, problem
   sandboxu, błąd ładowania i timeout. Sam timeout nie dowodzi braku pamięci.
3. Zebrać wersje bibliotek, liczbę procesów, PSS, RSS, VmSwap i dostępny RAM
   systemu. Zmierzyć stan początkowy oraz ładowanie z JS i obrazami włączonymi
   i wyłączonymi. Dopiero po tym sprawdzać witryny w sieci.
4. Powtórzyć pomiary na fizycznym Pentium II, gdy będzie dostępny.

Rozwojowy `tools/monitor.sh` rozszerza format próbek o `sum_pss_kib` oraz
`pss_unreadable_processes`. PSS dzieli pamięć współdzieloną pomiędzy procesy,
więc jej suma jest lepszym przybliżeniem kosztu RAM niż suma RSS. Próbki
nie są atomowe. Brak PSS choć jednego wybranego żywego procesu daje `NA`
z licznikiem brakujących danych. Preferowany jest `smaps_rollup`, z odczytem
`smaps` na starszych kernelach. Odczyt `smaps` ma dodatkowy koszt; przy
pomiarach docelowego sprzętu trzeba uwzględnić narzut monitorowania.
PSS nie obejmuje całej pamięci kernela ani całego kosztu systemu graficznego.

Paczki 0.1.1 w `dist/` pozostają oryginalnymi plikami wydania. Nowe narzędzia
etapu P2 są w źródłach na `main`; nie zostały dopisane do starych archiwów.

## Źródła ustaleń

- [Debian 13: ograniczone wsparcie i386 i wymaganie SSE2](https://www.debian.org/releases/trixie/release-notes/issues.en.html#i386-reduced-support)
- [Debian: wymagania instrukcji poszczególnych architektur](https://wiki.debian.org/ArchitectureSpecificsMemo)
- [Bookworm WebKitGTK: reguły budowania 2.50.6](https://sources.debian.org/src/webkit2gtk/2.50.6-1~deb12u2/debian/rules/)
- [Bookworm: poprawka bez wymuszania SSE2](https://sources.debian.org/src/webkit2gtk/2.50.6-1~deb12u2/debian/patches/dont-detect-sse2.patch/)
- [Trixie WebKitGTK: changelog z decyzją o SSE2](https://sources.debian.org/src/webkit2gtk/2.52.6-1~deb13u1/debian/changelog/)
- [Debian #1126591: SIGILL bez SSE2](https://bugs.debian.org/1126591)
- [Debian #1135152: koniec wsparcia bezpieczeństwa WebKit w Bookworm](https://bugs.debian.org/1135152)
- [Dillo: zakres HTML/CSS i brak JavaScript](https://dillo-browser.org/user_help.html)
- [QEMU: ograniczenia dokładności icount](https://www.qemu.org/docs/master/devel/tcg-icount.html)
