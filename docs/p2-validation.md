# Wyniki pierwszego etapu P2

Data: 8 listopada 2026 r. Testy wykonano po wydaniu 0.1.1.
Są dwa odrębne eksperymenty: uruchomienie minimalnego gościa i386 w QEMU
oraz pomiar obecnego prototypu amd64 na mocniejszym hoście.

## Profil CPU i RAM w pełnosystemowym QEMU

Zbudowano obraz według [instrukcji emulacji](../emulation/README.md).
Uruchomienie z `--network none --user 1000:1000` zakończyło się kodem 0
i markerem `P2BOOTPASS`.

| Element | Zaobserwowany wynik |
| --- | --- |
| QEMU | 7.2.22, pakiet `1:7.2+dfsg-7+deb12u18+b3` |
| Akcelerator | TCG |
| CPU | Jeden Pentium II Deschutes, rodzina 6, model 5 |
| Instrukcje | MMX obecne; SSE, SSE2 i tryb 64-bitowy nieobecne |
| RAM skonfigurowany w QEMU | 384 MiB |
| RAM widoczny jako MemTotal | 373016 KiB; część nominalnej pamięci rezerwuje kernel |
| Kernel gościa | `6.1.0-54-686`, pakiet `6.1.190-1`, i386 |
| BusyBox gościa | `1:1.35.0-4+deb12u1+b1`, i386, statyczny |
| Inwentaryzacja | `tools/collect-report.sh` zakończył się pomyślnie |
| Dysk, sieć, swap, pulpit, przeglądarka | Nieobecne w tej minimalnej próbie |

Kontrole negatywne użyły tego samego gościa i zmieniły wyłącznie argument
QEMU w kopii `run-check.sh`:

- `-cpu pentium3`: odrzucone jako nieprawidłowy profil CPU; kod wrappera 1.
- `-m 256`: odrzucone jako nieprawidłowa pamięć; kod wrappera 1.

Test rozróżnia zadany profil od niewłaściwego procesora lub RAM.
Nie jest to test GUI ani pomiar zasobów przeglądarki na P2.
Pole `cpu MHz` w raporcie gościa wynika z zegara emulacji/hosta i nie oznacza
odtworzenia 400 MHz. Obraz zbudowano z bieżących repozytoriów Bookworm;
zachowane wersje pakietów określają zakres tej konkretnej próby.

## Pamięć prototypu amd64 na lokalnej stronie

Użyto istniejącego binarium P2Browser 0.1.1 z WebKitGTK 2.54.0 i GTK 3.24.49
oraz nowego monitora ze źródeł. Program działał jako zwykły użytkownik,
pod Xvfb i D-Bus, z włączonym sandboxem, bez sieci i bez limitu RAM.
Załadowano wyłącznie `fixtures/start.html`. Automatyczne obrazy pozostały
domyślnie wyłączone. Druga próba dodawała jedynie `--javascript`.

Program kończył się po 18 sekundach (`--quit-after 18`), monitor zbierał
próbki przez 14 sekund co około 2 sekundy. Każda próba dała osiem próbek,
w chwilach 0, 2, 4, 6, 8, 10, 12 i 14 s. Poniższa tabela dotyczy próbek
od 2 do 14 s, po zakończeniu ładowania lokalnego dokumentu. W obu raportach
zapisano `load finished` oraz włączony sandbox.

| Tryb | Procesy w drzewie | Zakres sumy PSS | Największa próbka sumy RSS | VmSwap |
| --- | ---: | ---: | ---: | ---: |
| JS wyłączony | 8 | 240743–240769 KiB (około 235 MiB) | 406948 KiB | 0 KiB |
| JS silnika włączony | 8 | 248903–248930 KiB (około 243 MiB) | 414444 KiB | 0 KiB |

PSS było dostępne dla wszystkich wybranych procesów we wszystkich próbkach.
RSS liczy w całości współdzielone strony każdego procesu, dlatego nie należy
porównywać jego sumy wprost z fizycznym RAM komputera. PSS proporcjonalnie
dzieli takie strony; wynik nadal jest przybliżeniem z nieatomowych próbek.
Nie obejmuje osobno uruchomionych Xvfb i D-Bus, innych usług systemowych,
kernela ani całego Xorg/pulpitu.

To pojedyncze krótkie próby prostego dokumentu, bez obciążenia pamięci systemu.
Nie określają maksimum przy ładowaniu, kosztu pięciu kart ani nowoczesnych
serwisów. Nie mierzono tutaj CPU i nie symulowano szybkości 400 MHz.
Wyniki amd64 nie są pomiarem wariantu i386 ani pracą przeglądarki w gościu
QEMU z 384 MiB RAM. Pokazują jednak istotny koszt bazowy obecnego silnika.

## Sprawdzenie monitora i pakowania

`python3 -m unittest discover -s tests -p 'test_*.py' -v`:

- 14 testów diagnostyki zaliczonych, w tym PSS z `smaps_rollup`, fallback
  `smaps`, zero PSS, brakujące i błędne dane, tożsamość PID oraz prywatność.
- 9 testów pakowania zaliczonych.
- 6 testów CLI/GUI pominiętych przez ich jawne przełączniki uruchamiania.
  Ich wcześniejsze wyniki dla 0.1.1 opisuje [validation.md](validation.md).

Kod przeglądarki nie był zmieniany w tym etapie. Bieżące próby lokalnego
ładowania z JS i bez JS zakończyły się kodem 0. Surowe raporty, obrazy
kontenerów i logi pozostają w lokalnym `build/` lub magazynie Dockera;
nie dołączono ich do repozytorium ani paczek wydania 0.1.1.

## Wniosek i kolejna próba

Możemy prowadzić testy profilu CPU bez fizycznego P2, a pomiar pamięci
uwzględnia teraz współdzielenie bibliotek. Zgodność i wydajność docelowej
przeglądarki pozostają otwarte. Kolejny eksperyment wymaga pełnego gościa
z Xorg i porównania Dillo z kandydatem WebKitGTK i386 na tej samej stronie.
Wybór utrzymywanego silnika bez SSE2 musi uwzględnić ograniczenia opisane
w [ocenie wykonalności](p2-feasibility.md).

Pełny eksperyment wykonano w kolejnym etapie; wyniki i obrazy zawiera
[porównanie Dillo i WebKitGTK](p2-browser-comparison.md).
