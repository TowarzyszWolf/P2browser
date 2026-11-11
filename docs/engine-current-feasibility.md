# Współczesny WebKitGTK bez SSE/SSE2 — próba 11 listopada 2026

**JavaScriptCore 2.54.2 działa w teście na modelu CPU Pentium II. Pełny
port przeglądarki GTK nie został jeszcze skompilowany ani uruchomiony.**

To oddzielny eksperyment od paczki Debiana 13 amd64 i od pomiarów
Bookworm i386 w pełnym gościu 384 MiB. Wyniku samego rdzenia JS nie
przenosimy na zgodność DOM, CSS, sieci, sandboxu ani całych witryn.

## Źródło i środowisko

Użyto stabilnego [WebKitGTK 2.54.2](https://webkitgtk.org/2026/10/23/webkitgtk2.54.2-released.html),
archiwum `https://webkitgtk.org/releases/webkitgtk-2.54.2.tar.xz`.
Sprawdzona suma SHA-256:

```text
a76345b391bef6211fb5ab6cfab2e86b285d18cda17cbb9ade595f1252b86065
```

Budowanie odbywało się na mocniejszym amd64, w kontenerze Bookworm,
przez GCC/G++ 12.2 z multilib i bibliotekami i386. CMake, Ninja i generatory
działały na hoście. Zastosowano `-m32 -march=pentium2 -mno-sse -mno-sse2
-mfpmath=387`, optymalizację `-O1`, `PORT=JSCOnly`, `ENABLE_C_LOOP=ON`,
wyłączony JIT/DFG/FTL i WebAssembly oraz systemowy alokator.

Konfiguracja cross-build nie uruchamia testu SSE2 na komputerze budującym;
nie uznajemy przejścia CMake za dowód działania CPU. Dowodem jest osobne
wykonanie skompilowanego ELF32 przez `qemu-i386 -cpu pentium2`.

## Dwie lokalne poprawki kompatybilności

Pierwsza próba bez poprawek natrafiła na nieobsługiwany na GCC i386 typ
`__uint128_t` w libpas. Biblioteka ta jest już wyłączona dla ADDRESS32,
lecz jej jednostki nadal trafiały do budowania. Skrypt projektu:

1. Dla 32 bitów i `USE_SYSTEM_MALLOC` usuwa nieużywane źródła libpas
   z listy kompilacji bmalloc.
2. W WTF ogranicza include `pas_process.h` i sprawdzenie
   `pas_process_is_shutting_down()` do Windows. Na Uniksie upstreamowa
   funkcja w tym miejscu zwraca stałe false.

Skrypt sprawdza oczekiwane fragmenty kodu. To lokalne poprawki
eksperymentalne, bez deklaracji akceptacji upstream lub kompletnego audytu.
Nie zastępują testów całego silnika.

## Faktyczny wynik JavaScriptCore

Ninja zakończyła wszystkie 1994 kroki celu `jsc`. Program jest ELF32 i386,
dynamicznie linkowany. Wykonanie `smoke.js` na modelu P2 zakończyło się
komunikatem:

```text
P2-JSC-CLOOP-PASS
```

Test obejmuje tablice i arytmetykę, JSON, wyrażenie regularne,
formatowanie ICU dla pl-PL, ArrayBuffer/DataView z liczbą zmiennoprzecinkową
oraz BigInt. Nie zawiera DOM ani interfejsu GTK. QEMU user nie ogranicza
tu RAM do 384 MiB i nie symuluje wydajności fizycznych 400 MHz.

## Stan portu GTK

Przygotowano osobny sysroot zależności Bookworm i386. CMake dla GTK 3,
X11, interpretera JS bez JIT i wyłączonych multimediów/WebGL poprawnie
wygenerował pliki budowania. Pierwsze braki pakietów GStreamer/gettext
rozwiązano wyłączeniem nieużywanych mediów i dodaniem narzędzia gettext.
Nie są one dowodem niezgodności Pentium II.

Próba celu WebCore miała jawny limit 300 sekund i dwa zadania kompilacji.
Doszła do kroku 2240 z 6738 (w tym generatory i zależności), po czym
`timeout` przerwał budowanie kodem 124. Nie powstał pełny WebCore ani
libwebkit2gtk; nie uruchomiono renderera na P2. Nie wykazano na tym etapie
ani sukcesu całego portu, ani nieusuwalnej bariery CPU.

Kolejny techniczny etap to dokończenie budowania WebCore i portu GTK,
sprawdzenie zależności graficznych bez SSE2, a następnie test całego stosu
z sandboxem w pełnym gościu 384 MiB. Dopiero później można oceniać koszt
RAM i zgodność stron. Rozbudowany silnik może przekroczyć użyteczny budżet
nawet wtedy, gdy zestaw instrukcji CPU nie jest już przeszkodą.

## Odtwarzanie i licencje

Polecenia i przypięte parametry zawiera
[experimental/engine-current/README.md](../experimental/engine-current/README.md).
Źródła, biblioteki i duże logi są lokalne w ignorowanym
`build/engine-current/`, poza paczką przeglądarki i repozytorium.
Skrypty mają MIT; kod WebKitGTK i jego zależności zachowuje licencje upstream.
