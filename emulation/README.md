# Próba profilu Pentium II w QEMU

Pełny test użytkowego systemu i porównanie Dillo/WebKitGTK opisuje
[FULL-SYSTEM.md](FULL-SYSTEM.md). Poniżej pozostaje mała próba samego
kernela, CPU i RAM.

To mały test uruchomienia kernela 32-bitowego, BusyBoxa i inwentaryzacji.
Gość ma procesor `pentium2`, jeden CPU i 384 MiB RAM. Wykorzystuje TCG,
nie ma sieci, dysku ani pulpitu. Nie uruchamia jeszcze przeglądarki.

Obraz budowania jest amd64. Pakiety kernela `linux-image-*-686:i386` i
`busybox-static:i386` pochodzą z repozytoriów Debiana 12 Bookworm. Kernel
jest tylko rozpakowywany; nie instalujemy go na hoście ani w kontenerze.
Architektura i386 zostaje dodana wyłącznie wewnątrz obrazu kontenera.

Z katalogu głównego projektu:

```sh
docker build -f emulation/Dockerfile.bookworm -t p2browser-p2-boot .
docker run --rm --network none p2browser-p2-boot
```

W środowisku z inspekcją TLS i dodatkowym certyfikatem CA buduj z sekretem
`--secret id=proxy_ca,src=/ścieżka/do/pakietu-CA.pem`. Weryfikacja TLS
pozostaje włączona. Pobieranie pakietów jest potrzebne przy budowaniu;
sam test gościa i uruchomienie gotowego obrazu działają bez sieci.

Udana próba kończy się kodem 0 i markerem `P2BOOTPASS`. Test sprawdza jeden
procesor z rodziny 6/modelu 5 (Pentium II Deschutes), brak SSE/SSE2/trybu 64-bitowego, zakres
dostępnej pamięci odpowiadający konfiguracji 384 MiB i uruchomienie
`tools/collect-report.sh`. Raport zawiera wersje rozpakowanych pakietów.
Pobranie innych aktualizacji Bookworm może zmienić wynikowy obraz;
do dokładnego odtworzenia trzeba zachować użyty obraz i wersje pakietów.

To minimalny initramfs, a nie zainstalowany Debian z Xorg. Brak poleceń
`dpkg-query` i `lspci` w gościu jest oczekiwany i nie oznacza braku pakietów
na przyszłym komputerze. Dostępny RAM w raporcie jest mniejszy od nominalnych
384 MiB, ponieważ kernel rezerwuje część pamięci. Swap nie jest skonfigurowany.

## Granice wyniku

- TCG sprawdza m.in. ścieżki instrukcji CPU; nie odtwarza szybkości 400 MHz.
- Nie emulujemy tu ATI Rage 3D Pro, dysków IDE/CF/SD ani zachowania pulpitu.
- Nie mierzymy zużycia zasobów WebKitGTK lub całej przeglądarki.
- Wynik nie potwierdza jeszcze zgodności bibliotek, TLS, JS, renderowania
  ani sandboxu na Pentium II.
- Potrzebny jest późniejszy test pełnego systemu, przeglądarki i fizycznego P2.

Debian 13 i386 wymaga SSE2 i nie zapewnia zwykłego systemu instalacyjnego
dla tej architektury. Z tego powodu obecne paczki Trixie amd64 nie są bazą
tego testu. Zobacz [uwagi wydania Debiana 13](https://www.debian.org/releases/trixie/release-notes/issues.en.html#i386-reduced-support).
