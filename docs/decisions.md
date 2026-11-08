# Decyzje techniczne wydania 0.1.1

Stan: 8 listopada 2026 r. Cel produktu opisuje [requirements.md](requirements.md).
Wydanie przygotowano ponownie na podstawie rozmowy po zmianie środowiska;
nie deklarujemy identycznego odtworzenia kodu lub pakietów 0.1.0.

## Natywny prototyp przed wariantem P2

Bieżąca paczka służy próbom na Debianie 13.7 amd64 użytkownika. Budujemy
natywnie dla amd64 z bibliotekami Trixie i automatycznie wyznaczanymi minimalnymi
zależnościami ABI. GUI i pakiet diagnostyczny muszą pochodzić z tego samego
wydania. Nie włączamy i386 i nie sprowadzamy zależności z Debiana 12.

Cel P2 pozostaje niezmieniony, ale wymaga osobnego wariantu i386 i pomiarów.
Udane uruchomienie amd64 albo nowoczesnej VM z 384 MB nie jest dowodem
zgodności z Pentium II. Także test CLI `--version` pod QEMU user nie obejmuje
całego renderowania i wszystkich procesów potomnych.

Najpierw sprawdzamy podstawowe uruchomienie, lokalny dokument, diagnostykę
i pakowanie, następnie rzeczywisty koszt silnika. Dopiero wyniki uzasadnią
wybór silnika docelowego oraz rozwój kolejnych funkcji. Wyniki faktycznie
wykonanych testów należy oddzielić od planu i instrukcji testowania.

## Silnik i zależności

Prototyp używa C, GTK3 oraz WebKitGTK przez API 4.1.
Numer API nie jest numerem wersji silnika. WebKitGTK zapewnia gotowy komponent
desktopowy i szeroki zakres WWW, ale nadal nie został zatwierdzony jako
ostateczny silnik dla P2.

W aktualnym wariancie Debiana 13 korzystamy z jego aktualizowanych repozytoriów.
Historia wcześniejszych prób Bookworm ma osobny kontekst: WebKitGTK z Debiana
12 zakończył wsparcie bezpieczeństwa, co potwierdza
[Debian #1135152](https://bugs.debian.org/1135152). Nie rozszerzamy tego statusu
na bieżące biblioteki Trixie i nie dołączamy starego WebKitGTK do nowej paczki.

Dla P2 samo wyłączenie JIT nie rozwiązuje wymagań procesora wszystkich
bibliotek. Historyczne CLoop i poprawki bez SSE2 są punktem startu do prób,
a nie gwarancją stabilności całego stosu. Flagi kompilacji własnego kodu nie
zmieniają instrukcji w gotowych zależnościach. Aktualny port P2 i starszych
systemów, a także jego utrzymanie, pozostają osobnym ryzykiem projektu.

Dillo działało według użytkownika na P2 z Debianem 12. Bez znanej wersji lub
pomiarów jest punktem odniesienia, nie gotową realizacją wymagań JS. NetSurf
również pozostaje alternatywą o ograniczonej zgodności z aplikacjami WWW.

## Rzeczywista granica funkcji

0.1.1 to pojedynczy widok i podstawowa nawigacja. JS domyślnie jest wyłączony,
a jego przełącznik udostępnia możliwości silnika. Nie jest to jeszcze
uzgodniony oszczędny JS ani system wyjątków dla witryn. Nie udajemy wdrożenia
kart, limitów RAM, quota danych, trwałej historii, offline lub pracy zdalnej.

Profil danych jest tymczasowy. Oszczędny cache w tej wersji pomaga pomiarowi,
ale nie zastępuje docelowego cache przyspieszającego ponowne ładowanie.
Wyłączamy WebGL i WebAudio, wymagamy gestu dla mediów i żądamy braku
akceleracji sprzętowej. Żądanie polityki grafiki nie zastępuje sprawdzenia
konkretnego runtime i sterownika.

Standardowe rozdzielenie procesów i sandbox pozostają włączone. GUI uruchamiamy
jako zwykły użytkownik pulpitu. Instalacja paczek jako root nie uzasadnia
uruchamiania przeglądarki jako root ani wyłączania sandboxu.

## Pamięć i diagnostyka

Wbudowane próbki dotyczą procesu interfejsu. Monitor zewnętrzny sumuje dane
interfejsu i potomnych procesów silnika. Suma RSS wielokrotnie liczy
współdzielone strony; trzeba ją tak nazywać, bez utożsamiania z unikalnym RAM.
Brak dostępu do informacji oznacza wartość nieznaną. Próbkowanie powinno mieć
ograniczoną częstotliwość, aby samo nie zdominowało zasobów P2.

Docelowe mechanizmy są rozdzielone: budżet RAM procesów, wspólna quota danych
roboczych witryn i cache. Linux zarządza swapowaniem. Program nie gwarantuje
ładowania kolejnych danych wyłącznie do swapu i nie zmienia samowolnie swapu
lub konfiguracji systemu. Tryb tekstowy wymaga reakcji przed całkowitym OOM;
nie zawsze uda się zachować stan.

Raporty są lokalne. Dziennik aplikacji ogranicza objętość i zbiera techniczne
zdarzenia bez URL, cookies, haseł, treści formularzy i surowych błędów strony.
Wyjście stderr bibliotek jest odrębne i może zawierać dodatkowe informacje.
Inwentaryzacja nie zbiera nazw komputerów, loginów, numerów seryjnych, sieci ani
pełnego środowiska procesów. Brak automatycznej wysyłki raportów.

Docelowa diagnostyka JS musi rozróżniać dowód konfliktu od podejrzenia.
Komunikat konsoli nie oznacza automatycznie awarii strony, a cisza nie dowodzi
poprawności. Użytkownik decyduje o wyjątku pełnego JS.

## Dane i sesje docelowe

Wspólny limit obejmie dane robocze wszystkich witryn, z podglądem udziału
poszczególnych serwisów. Dane wyłącznie karty i współdzielone dane witryny
mają różne momenty usuwania; zamknięcie jednej karty nie wylogowuje innych
kart tej samej witryny. To przyszła funkcja, a nie skutek samego profilu
tymczasowego obecnego prototypu.

Historia, zakładki, cache fontów, cache ładowania i świadome kopie offline
pozostają odrębne. Offline zachowuje treść i potrzebne zasoby, również gdy
treść wymagała zalogowania, ale nie służy utrwalaniu kluczy sesji i działającej
aplikacji. Dobór języków określa fonty i preferencje, przy zachowaniu poprawnego
dekodowania Unicode.

## Docelowa praca zdalna

Kierunek: JS, DOM i layout na serwerze; polecenia rysowania oraz potrzebne
zasoby na P2. Serwer utrzymuje działającą stronę i reaguje na wejście klienta.
Nie zakładamy niezależności dowolnego fragmentu JS od stanu strony.

Publiczne API WebKitGTK nie zapewnia gotowego protokołu takiej transmisji.
Wymaga on osobnego eksperymentu z renderowaniem, fontami, obrazami,
współrzędnymi, przewijaniem, synchronizacją oraz kosztami pamięci i sieci.

Ręczne włączenie zdalnego trybu ustala preferencję. Po awarii użytkownik
wybiera czekanie lub lokalne przeładowanie zastępcze. Po odzyskaniu łączności
następuje powrót zdalny, o ile użytkownik jawnie nie wybrał lokalnego trybu.
Próba zachowania logowania nie jest gwarancją przeniesienia całej sesji.

Transport ma tryb automatyczny (LAN może być nieszyfrowany, poza LAN szyfrowany),
zawsze szyfrowany i jawnie nieszyfrowany. To wybór użytkownika, niezależny od
HTTPS do witryn. Szczegóły identyfikacji LAN i serwera ustalimy przy module;
prywatny adres IP sam nie dowodzi tożsamości serwera.

## Licencja i rozpowszechnianie

Nowy kod i dokumentacja są na MIT w pliku [LICENSE](../LICENSE). Zależności
i przyszłe modyfikacje cudzych silników mają własne licencje. Wykaz komponentów
opisuje [THIRD_PARTY.md](../THIRD_PARTY.md); nie bundlujemy bibliotek systemowych.
Archiwum źródeł zawiera dokumentację projektu, bez raportów i binariów.
