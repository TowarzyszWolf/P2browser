# Weryfikacja wydania 0.1.1 — Debian 13 amd64

Data: 8 listopada 2026. Wydanie odtworzono z kodu zachowanego w rozmowie,
po utracie poprzedniego środowiska. Nie deklarujemy identyczności plików 0.1.0.

## Wyniki

| Sprawdzenie | Wynik i zakres |
| --- | --- |
| Kompilacja natywna | ELF64, x86-64, bez flag zależnych od instrukcji maszyny budującej |
| Zależności | Natywne dpkg-shlibdeps, symbole ABI, właściwe pakiety GLib/GTK t64 |
| Diagnostyka | 10 testów zaliczonych: prywatność, uprawnienia, błędy zapisu i drzewo procesów |
| Pakowanie | 9 testów zaliczonych: architektura, symbole, brak zgadywanych zależności, XZ, źródła |
| CLI | 4 testy zaliczone: wersja bez ekranu, argumenty, istniejący raport i brak ekranu |
| GUI lokalne | Testy JS wyłączonego/włączonego, HTML/CSS, formularza i PNG zaliczone |
| Dziennik błędów JS | Test pomijania treści prywatnego wyjątku i adresu zaliczony |
| Instalacja | Oba lokalne DEB zainstalowane przez APT w świeżym Debianie 13 amd64 |
| Architektury obce | Lista pozostała pusta; nie dodawano i386 ani repozytoriów Bookworm |
| Stan pakietów | dpkg --audit bez problemów, obie paczki w wersji 0.1.1 |
| GUI po instalacji | Lokalny formularz działa z zainstalowanej ścieżki, bez źródeł i bibliotek rozwojowych |

Po instalacji program zgłosił: p2browser-probe 0.1.1,
WebKitGTK 2.54.0, 64-bit. Użyto GTK 3.24.49 i bibliotek Debiana 13.

GUI testowano jako zwykły użytkownik, pod Xvfb i D-Bus, z włączonym sandboxem
WebKit. Kontener uruchamiający GUI nie miał sieci. Dodatkowe opcje seccomp oraz
systempaths jednorazowego kontenera umożliwiły tworzenie wewnętrznego sandboxu;
nie wyłączano sandboxu aplikacji i nie zmieniano komputera użytkownika.

Nowy pakiet wymaga m.in. libc6 >= 2.34, GLib t64 >= 2.58.0,
GTK t64 >= 3.16.2 i WebKitGTK >= 2.39.90, zgodnie z metadanymi symboli.
Nie skopiowano pełnych wersji bibliotek z poprzedniego builda i386.
Spełnienie tych minimów nie jest obietnicą obsługi innych wydań systemu.

## Granice

To test podstawowego lokalnego dokumentu i instalacji, nie całej platformy WWW.
Nie potwierdza działania popularnych serwisów ani pełnej zgodności aplikacji.
Próby wykonano w amd64 na mocniejszym komputerze, bez ograniczenia RAM do 384 MB.
Nie jest to kompilacja dla Pentium II. Ten sprzęt pozostaje osobnym celem projektu.

Surowe dzienniki i zrzut lokalnego dokumentu znajdują się w katalogu build
środowiska roboczego i nie są dołączane do źródeł. Pakiety zawierają wyłącznie
nasz kod i dokumentację; biblioteki systemowe pobiera APT z repozytoriów Debiana.
