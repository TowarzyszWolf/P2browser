# Wymagania P2Browser

Stan uzgodnień: 8 listopada 2026 r. Dokument odtwarza wymagania z rozmowy.
Opisuje cel produktu, nie deklarację funkcji już dostępnych w prototypie.
Kod wydania 0.1.1 przygotowano ponownie po zmianie środowiska; nie deklarujemy
identyczności bajtowej z dawnym wydaniem 0.1.0.

## Sprzęt i środowiska odniesienia

| Element | Ustalona konfiguracja docelowa P2 |
| --- | --- |
| Komputer | IBM PC300PL; na zrzucie model 6862340 |
| CPU | Pentium II Deschutes, Slot 1, 400 MHz nominalnie, około 398 MHz na zrzucie; bez SSE/SSE2 |
| RAM | 384 MB SDRAM; system na zrzucie raportuje około 368–369 MiB |
| GPU | ATI 3D Rage Pro AGP 2X |
| Ekran | 1280 × 1024 w pokazanej konfiguracji |
| Nośnik | SSD przez IDE–SATA; wymagane również działanie z HDD oraz kartami CF i SD |
| Swap | Około 467 MiB na zrzucie; nie jest to ustalony wymóg każdej instalacji |

Zrzut przedstawia **Debiana 10 Buster i686**, kernel
`4.19.0-17-686-pae`, Openbox i terminal XFCE. Zużycie pamięci wynosiło około
70–72 MiB; to obserwacja pojedynczego uruchomienia. Pojemność i aktualne wolne
miejsce dysku pozostają nieustalone.

Użytkownik osobno potwierdził działanie Dillo na P2 z **Debianem 12**. Wersja
Dillo i jej pomiary pamięci są nieznane. Ta informacja nie oznacza, że zrzut
przedstawia Debiana 12.

Bieżąca paczka 0.1.1 jest przeznaczona do natywnych prób na systemie użytkownika,
**Debianie 13.7 amd64**. Używa bibliotek Trixie i nie wymaga obcej architektury
i386 ani repozytoriów Bookworm. To etap rozwojowy na innym sprzęcie;
nie potwierdza działania na P2 ani nie zastępuje przyszłego wariantu i386.

## Uzgodnienia 1–25

1. **Przetwarzanie.** Domyślnie lokalne. Opcjonalny drugi komputer włączany ręcznie.
2. **Baza.** Początkowo istniejący silnik; później możliwość jego modyfikacji
   lub zastąpienia własnym. Silnik docelowy wymaga zatwierdzenia pomiarami.
3. **Zgodność.** Możliwie szeroka obsługa internetu; lista witryn testowych nie
   ogranicza projektu do tych serwisów.
4. **Zastosowanie.** Także rozbudowane aplikacje internetowe, nie tylko czytanie.
5. **System.** Cel: Debian 9 i nowsze w granicach zgodności sprzętu. Debian 13
   nie jest wspieraną instalacją systemową dla P2; bieżący Debian 13 amd64 służy
   próbom na współczesnym komputerze.
6. **Pulpit.** Zwykła aplikacja pod Xorg; GNOME, XFCE, i3, dwm oraz Openbox.
7. **RAM.** Domyślnie bez ręcznego limitu, użycie dostępnej pamięci według
   potrzeb. Limit ustawiany suwakiem lub liczbą obejmuje także procesy silnika.
8. **Swap.** Dopuszczony, przy pierwszeństwie RAM; różne rodzaje nośników.
9. **Przekroczenie RAM.** Włączamy dostępne oszczędności, korzystamy ze swapu
   i sygnalizujemy spowolnienie, starając się zachować stronę. Linux decyduje
   o rozmieszczeniu stron pamięci; nie gwarantujemy dalszych alokacji wyłącznie
   do swapu.
10. **Karty.** Domyślnie najwyżej pięć; użytkownik może zdjąć limit.
11. **Tło.** Domyślnie usypiamy karty i staramy się zwolnić ich RAM z zachowaniem
    stanu. Można pozostawić aktywną pojedynczą kartę lub włączyć domyślną
    aktywność wszystkich kart; to drugie ustawienie początkowo wyłączone.
12. **Czas.** Długie ładowanie wymagających stron jest akceptowane.
13. **Priorytet CPU.** Przetwarzanie strony ważniejsze od płynności interfejsu.
14. **Krytyczna pamięć.** Gdy brakuje RAM i swapu, dopuszczone przeładowanie
    w trybie tekstowym i utrata niezapisanego stanu. Użytkownik widzi przyczynę
    i tryb; reagujemy przed całkowitym wyczerpaniem pamięci, o ile to możliwe.
15. **Animacje.** Domyślnie nieruchome; można włączyć odtwarzanie.
16. **Obrazy.** Domyślnie na żądanie; opcja automatycznego pobierania.
17. **Jakość obrazów.** Domyślna optymalizacja pamięci z celem braku widocznego
    pogorszenia. Można ją wyłączyć.
18. **Układ.** Staramy się zachować układ autora.
19. **CSS.** Szeroka obsługa stylów.
20. **JS.** Docelowo wyłączony, oszczędny jako domyślny i pełny w zakresie
    silnika. Wykryty lub podejrzewany konflikt ograniczeń tworzy raport
    i osobne pytanie „Tak/Nie” o pełny JS. Tylko „Tak” dodaje wyjątek dla witryny.
    Ustawienia pozwalają przeglądać, dodawać i usuwać wyjątki. Raport oddziela
    potwierdzoną przyczynę od podejrzenia; nie wykryje każdej niesprawnej funkcji.
21. **API.** Rozbudowujemy zaawansowane API według faktycznych potrzeb stron.
22. **Aktualizacje na żywo.** Domyślnie wyłączone, z możliwością włączenia.
23. **Formularze.** Także sterowane JS. Bez JS pozostają możliwości oferowane
    przez witrynę bez skryptów.
24. **Logowanie.** Szeroka obsługa; bez JS tylko w serwisach, które to umożliwiają.
25. **Sesje.** Karty tej samej witryny współdzielą sesję. Zamknięcie ostatniej
    karty witryny lub przeglądarki kończy lokalną sesję. Próbujemy również
    wylogowania na serwerze tam, gdzie istnieje obsługiwany mechanizm;
    nie gwarantujemy unieważnienia sesji na dowolnym serwerze.

## Uzgodnienia 26–50

26. **Dane robocze.** Strony mogą zapisywać dane potrzebne do działania. Dane
    wyłącznie karty usuwamy przy jej zamknięciu; dane współdzielone po ostatniej
    karcie witryny. Obowiązuje **wspólny limit wszystkich witryn** i podgląd
    wykorzystania łącznego oraz udziału witryn. Limit RAM i cache są oddzielne.
    Świadomy zapis offline zachowuje treść, a nie przedłuża sesję logowania.
27. **Wideo.** Próba odtwarzania wewnątrz przeglądarki. Autoodtwarzanie domyślnie
    wyłączone, możliwe do włączenia. Preferowany lekki, dostępny strumień
    i kodek zgodny z CPU; można próbować niższej rozdzielczości. Przy braku
    możliwego wariantu informujemy użytkownika. Serwis nie musi oferować
    dowolnego kodeka; nie zakładamy lokalnego przekodowywania jako oszczędności.
28. **Dźwięk.** Odtwarzany, z wyciszaniem pojedynczych kart.
29. **Canvas/WebGL.** Domyślnie wyłączone; użytkownik może włączyć obsługę.
30. **Fonty.** Domyślnie systemowe. Opcjonalne pobieranie fontów, lista i rozmiary
    pobranych plików, czyszczenie oraz trwały cache na dysku.
31. **Języki.** Pierwsze uruchomienie pyta o kraj i język, dobiera fonty
    i preferencje. Dodatkowe języki w ustawieniach. Poprawny odczyt Unicode
    pozostaje niezależny od wyboru alfabetów.
32. **PDF.** Pobranie i otwarcie domyślną aplikacją; komunikat o jej braku.
33. **Pobieranie.** Prosty menedżer z historią, rozmiarem, prędkością i postępem.
    Procent zależy od możliwości ustalenia całkowitego rozmiaru.
34. **Historia.** Zapisywana na dysku.
35. **Zakładki.** Prosta lista adresów i foldery.
36. **Reklamy.** Proste reguły i DNS blokujący reklamy; wybór dostawcy lub własny
    DNS w ustawieniach.
37. **Cache.** Priorytet szybkiego ponownego ładowania; trwały cache oddzielony
    od danych roboczych i sesji.
38. **Offline.** Osobna lista świadomych zapisów. Cała treść bieżącego,
    załadowanego dokumentu, także poza ekranem, i potrzebne zasoby; bez
    odwiedzania podstron i funkcji zależnych od serwera. Celem jest kopia treści.
39. **Wyjątki serwisów.** Dopuszczone specjalne poprawki, z priorytetem ogólnych.
40. **Izolacja.** Standardowe rozdzielenie procesów silnika i sandbox pozostają
    włączone. Użytkownik zaakceptował ich koszt. Oszczędzamy najpierw na
    zasobach stron, cache i kartach w tle.
41. **Hasła.** Na początku bez menedżera zapamiętywania haseł.
42. **Utrzymanie.** Aktualizacje programu i zależności oraz poprawki bezpieczeństwa.
43. **Budowanie.** Kompilacja na mocniejszym komputerze dla docelowego środowiska.
44. **Licencja.** Nowy kod prototypu na MIT; otwarte źródła i wykaz komponentów,
    zasobów, autorów i licencji. Biblioteki oraz przyszłe modyfikacje silników
    podlegają swoim licencjom.
45. **Etapy.** Najpierw prototyp z szeroką diagnostyką, później program codzienny.
46. **Kolejność.** Najpierw minimalizacja zużycia zasobów, potem rozszerzanie zgodności.
47. **Weryfikacja.** Fizyczny P2 jest dostępny. Najpierw szybkie testy
    wirtualne/emulowane, później sprzęt. Sprawdzenie instrukcji wymaga emulacji
    P2; samo ograniczenie RAM nowoczesnej VM nie wystarcza.
48. **Utrata łączności.** Wybór „Czekaj” lub „Przełącz lokalnie”. Dopiero decyzja
    użytkownika przełącza; można przeładować i utracić niezapisane dane.
    Zachowanie logowania w obie strony jest celem bez gwarancji. Po odzyskaniu
    połączenia wracamy do wcześniej wybranego trybu zdalnego, chyba że
    użytkownik jawnie wybrał lokalny.
49. **Prywatność i transport.** Domyślnie automatycznie: LAN dopuszcza brak
    szyfrowania, poza LAN szyfrowanie wymagane. Osobne opcje „Zawsze szyfruj”
    i jawne „Bez szyfrowania”, także poza LAN. Dotyczy transportu pomocniczego;
    HTTPS do witryn pozostaje niezależny. Nieszyfrowany transport może ujawniać
    przesyłane treści i sesje podmiotowi mającemu dostęp do transmisji.
50. **Wyniki zdalne.** Wybrano zdalne JS, DOM i layout, z poleceniami rysowania
    oraz zasobami zwracanymi P2. Klient rysuje i wysyła wejście, serwer utrzymuje
    działającą stronę i odsyła zmiany. To osobny protokół wymagający własnego
    eksperymentu, nie gotowa funkcja WebKitGTK ani film z całego pulpitu.

## Witryny odniesienia

Stały zestaw wybrany z rankingu Similarweb za sierpień 2026: Google, YouTube,
Facebook, Instagram, ChatGPT, X, Reddit, Bing, TikTok i WhatsApp Web.
To zestaw porównawczy, a nie deklaracja zgodności obecnego prototypu.
Źródło wyboru: <https://www.similarweb.com/blog/research/market-research/most-visited-websites/>.

## Bieżący etap 0.1.1

Natywny prototyp Debiana 13 amd64 ma pojedynczy widok, prostą nawigację,
JS domyślnie wyłączony z przełącznikiem JS silnika, obrazy domyślnie wyłączone,
sandbox, profil tymczasowy i raporty. Lokalny test dotyczy własnego dokumentu
HTML/CSS/JS. Nie zastępuje testów P2 i całego zestawu witryn.

Oszczędny JS, pięć kart, limity danych, docelowe sesje, historia, zakładki,
offline i moduł zdalny pozostają dalszymi etapami. Aktualna paczka amd64 nie
potwierdza zgodności Debiana 9–12, i386, braku SSE/SSE2 ani działania przy 384 MB.
