# Walidacja 0.2.0~alpha1 — 11 listopada 2026

## Debian 13 amd64

Program skompilowano jako natywny ELF64 x86-64 w środowisku Trixie,
z GTK 3 i systemowym WebKitGTK 2.54.0. Nie użyto flag instrukcji
specyficznych dla komputera budującego.

Pełny zestaw **52 testów** przeszedł:

| Grupa | Liczba |
| --- | ---: |
| Diagnostyka i monitor drzewa procesów | 14 |
| Polityka pamięci, niepełne dane i ponowne użycie PID | 11 |
| Pakowanie, zależności ABI i archiwum źródeł | 9 |
| CLI i GUI | 7 |
| Generator syntetycznych obciążeń | 7 |
| Nadzorca procesów i pomiar CPU | 4 |

Testy GUI wykonano jako zwykły użytkownik, pod Xvfb/D-Bus, bez sieci,
z włączonym sandboxem. Po zmianie parametrów czasu testów powtórzono
siedem testów CLI/GUI przy domyślnym mnożniku 1: wszystkie przeszły.

Obie paczki 0.2.0~alpha1 zainstalowano przez APT w świeżym Debianie 13
amd64, bez źródeł projektu i bibliotek deweloperskich. `dpkg --audit`
nie wykazał błędów; lista architektur obcych pozostała pusta.
Z zainstalowanego `/usr/bin/p2browser-probe` przeszły test formularza JS
i test kart/odciążania/tekstu. WebKit zgłosił 2.54.0, 64 bit.
Końcowe uzupełnienia raportów zmieniają dokumentację paczek, bez zmiany
sprawdzonego binarium lub zależności ABI.

## Pełny gość Pentium II

QEMU TCG, jeden CPU Pentium II Deschutes bez SSE/SSE2/LM, 384 MiB RAM,
swap 512 MiB, kernel 6.1.0-54-686 i rzeczywisty userspace Bookworm i386.
WebKitGTK 2.50.6, GTK 3.24.38, Xvfb 1000×740 i sandbox włączony.
Własny kod zbudowano przez `-march=pentium2 -mno-sse -mno-sse2 -mfpmath=387`.

Przeszły lokalne testy HTML/CSS i formularza JS oraz **wszystkie siedem
testów CLI/GUI**. Test pamięci w obu trybach JS sprawdził pięć kart,
odmowę szóstej, ochronę jednej karty, odciążenie trzech innych,
przeładowanie karty i zastąpienie WebView tekstem. Dziennik kernela
końcowej próby nie zawierał OOM kill, segfault ani nielegalnej instrukcji.

Pierwsza próba regresji używała natywnych czasów 4 i 30 s: test prywatnego
błędu JS i test pięciu kart zakończyły się przed dokończeniem ładowania.
Powtórzenie z `P2_GUI_TEST_TIME_SCALE=5` i niezmienionymi asercjami
przeszło w 134,9 s dla całego zestawu. Nie usuwano warunków zaliczenia.
Weryfikowaną kopię skryptu testów przekazano do nowego dysku przez debugfs;
runner sprawdził odczyt i zachował SHA-256.

Większe obciążenia i ich nieudane/ponowione próby opisuje
[osobny raport](stress-results-alpha1.md). Ten test nie używa jeszcze
nowej lokalnej kompilacji WebKitGTK 2.54.2.

## Próba współczesnego silnika

JavaScriptCore 2.54.2 skompilował się dla i386 z C Loop bez SSE/SSE2
i przeszedł `smoke.js` pod modelem CPU Pentium II. Konfiguracja GTK
przeszła, lecz budowanie WebCore zatrzymał jawny limit 300 s.
Pełny port GTK nie jest gotowy; [dokładny raport](engine-current-feasibility.md)
oddziela te wyniki od działania kompletnej przeglądarki.

## Granice

Nie badano fizycznego P2, ATI Rage Pro, rzeczywistych serwisów WWW,
logowania, transferu do drugiego komputera, multimediów ani prawdziwego
wyczerpania RAM i całego swapu. TCG nie odwzorowuje 400 MHz. Budżet pamięci
pozostaje miękki; odciążanie traci niezapisany stan, a tryb tekstowy jest
przybliżeniem. Pełną listę brakujących funkcji zawiera README.

WebKitGTK Bookworm jest starszą bazą testu offline z zakończonym wsparciem;
nie stanowi docelowego utrzymywanego portu. Paczka do pobrania pozostaje
natywna dla Debiana 13 amd64 i korzysta z bibliotek Trixie.
