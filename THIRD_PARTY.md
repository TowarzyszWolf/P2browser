# Komponenty i narzędzia zewnętrzne

Nowy kod oraz dokumentacja P2Browser są objęte [MIT](LICENSE). MIT nie zmienia
warunków licencyjnych bibliotek, narzędzi i ewentualnych przyszłych modyfikacji
silnika. Wydanie 0.1.1 jest rekonstrukcją projektu na podstawie rozmowy,
bez deklaracji identyczności z dawnymi artefaktami 0.1.0.

Repozytorium nie zawiera skopiowanych źródeł ani dołączonych binariów bibliotek
GTK, WebKitGTK, JavaScriptCore i ich zależności. Paczka programu dynamicznie
korzysta z bibliotek dystrybucji i deklaruje wymagane zależności ABI. Archiwum
źródeł P2Browser nie jest archiwum źródeł tych bibliotek.

## Biblioteki uruchomieniowe

| Komponent | Zastosowanie | Licencje w skrócie |
| --- | --- | --- |
| GTK3 i GDK | Okno, kontrolki, integracja pulpitu | LGPL; szczegóły według konkretnego pakietu |
| GLib, GObject, GIO | Typy, zdarzenia, opcje i operacje systemowe | LGPL; szczegóły według pakietu |
| WebKitGTK / WebCore | Wyświetlanie i obsługa stron | LGPL/BSD oraz licencje poszczególnych części |
| JavaScriptCore | Wykonywanie JS i interfejs wartości JS | LGPL/BSD oraz licencje poszczególnych części |
| Cairo | Powierzchnia zrzutu i zapis PNG | LGPL/MPL dla głównej biblioteki, ze szczegółami właściwego pakietu |

GTK i WebKitGTK używają dalszych bibliotek grafiki, fontów, sieci oraz
multimediów. Lista, wersje, autorzy i licencje zależą od wybranej kompilacji
dystrybucji. Tabela nie jest kompletną listą plików lub komponentów całego
systemu. Wydanie natywne Trixie używa zależności własnych repozytoriów
Debiana 13, bez dołączania bibliotek Debiana 12.

Szczegółowe informacje Debiana znajdują się w
`/usr/share/doc/<nazwa-pakietu>/copyright`, np. dla zainstalowanych odpowiedników
`libgtk-3-0t64`, `libglib2.0-0t64`, `libwebkit2gtk-4.1-0`,
`libjavascriptcoregtk-4.1-0` i `libcairo2` oraz ich dalszych zależności.
Przy innych API i wydaniach nazwy pakietów mogą być inne.

Wymagania konkretnej paczki pokazuje `dpkg-deb -f <pakiet.deb> Depends`.
Inwentaryzacja projektu zapisuje wersje wybranych zainstalowanych bibliotek.
Pełne metadane i pakiety źródłowe zależności pozostają po stronie dystrybucji.
Jeżeli przyszłe wydanie zacznie je dołączać, musi dołączyć wykaz faktycznie
rozpowszechnianych wersji i spełnić właściwe obowiązki licencyjne.

## Narzędzia budowania i testów

Narzędzia środowiska budowania, niedołączane do paczki przeglądarki:

- GCC i linker — kompilacja oraz linkowanie;
- GNU Make i `pkg-config` — budowanie i odczyt parametrów bibliotek;
- dpkg (`dpkg-deb`, `dpkg-shlibdeps`), GNU binutils, tar i gzip — paczki,
  informacje o binarium i archiwum źródeł;
- POSIX sh, awk, coreutils, grep i sed — diagnostyka i pakowanie;
- Python 3 oraz biblioteka standardowa — testy na hoście budowania;
- Xvfb, xauth i D-Bus — opcjonalne testy GUI bez ekranu;
- QEMU — opcjonalna emulacja, z ograniczeniami opisanymi w README;
- Docker i obraz Debian Trixie — opcjonalne środowisko budowania.

Wszystkie zachowują własne licencje, dostarczane przez odpowiednie pakiety
lub producentów. Używanie obrazu Debiana do budowania nie oznacza dołączenia
całej dystrybucji do paczki programu.

## Zasoby projektu

`fixtures/start.html` jest własną stroną testową na MIT. Nie pobiera
zewnętrznych fontów, obrazów, skryptów lub innych zasobów. Nie dołączamy kopii
witryn odniesienia ani ich materiałów. Kod Dillo i NetSurf nie został włączony
do tego wydania; nazwy występują jako punkty porównawcze i kandydaci rozważani
w dokumentacji. Cytowanie dokumentacji technicznej nie zmienia jej autorstwa.
