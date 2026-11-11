# Znikająca karta przy filmie — diagnostyka alpha2

Data: 11 listopada 2026. Wersja 0.2.0~alpha2 dla Debiana 13 amd64.
Ta zmiana usprawnia odzyskiwanie strony i diagnozę. Nie potwierdza jeszcze
usunięcia przyczyny zgłoszonej awarii YouTube.

## Co wynika ze zgłoszenia

Użytkownik testował alpha1 z WebKitGTK 2.54.0 w VM Debian 13 amd64,
z przydzielonymi 4 GB RAM. Po uruchomieniu filmu znika zawartość karty,
a okno przeglądarki pozostaje. Log zawiera `web_process_terminated reason=0`.
To kategoria `WEBKIT_WEB_PROCESS_CRASHED`, bez informacji o sygnale
lub konkretnym module, który spowodował zakończenie.

Ostatnia próbka przed awarią miała 1061200 KiB PSS (około 1036 MiB),
następna 291365 KiB (około 285 MiB). To całe drzewo procesów, w tym inne
karty i procesy wspólne; nie pamięć samego filmu. VmSwap wynosiło zero.
Przefiltrowany dziennik kernela odczytany przez roota nie zawierał wpisów
OOM/segfault/trap. Nie ustalono braku pamięci ani konkretnej awarii kodeka.
Wcześniejsze `WebKitNetworkError code=302` oznacza anulowane ładowanie;
nie jest samo w sobie przyczyną późniejszego zakończenia procesu.

## Co zmienia alpha2

- W karcie pojawia się komunikat GTK z przyciskiem `Wczytaj ponownie`.
  Awaria nie uruchamia automatycznej pętli przeładowań.
- Adres i wspólny kontekst sesji pozostają. Przeładowanie może stracić
  formularze i stan aplikacji; odzyskanie logowania zależy od witryny.
- Awaria karty w tle nie przełącza aktywnej karty. Powrót do uszkodzonej
  karty pozwala zdecydować o ponownym wczytaniu.
- Stan nawigacji i oczekujące odczyty tekstu są unieważniane, żeby martwy
  proces nie został uznany za poprawnie załadowany dokument.
- Raport rozróżnia `crashed`, `engine_memory_limit`, `terminated_by_api`
  i nieznany powód. Limit zgłoszony przez silnik nie jest dowodem Linux OOM.
- Dołącza ostatnią dostępną próbkę pamięci całego drzewa wraz z jej wiekiem.
  To poprzedni pomiar, a nie gwarancja odczytu dokładnego szczytu awarii.
- Przy włączonym JS rejestruje odtwarzanie i kody błędów elementów audio/wideo:
  1 przerwanie, 2 sieć, 3 dekodowanie, 4 nieobsługiwane źródło. Nie zapisuje
  adresu filmu, tytułu, treści wyjątku ani danych formularzy.
- Inwentaryzacja systemu obejmuje wersje znanych pakietów GStreamer.

## Jak zebrać raport

Uruchom w terminalu zwykłego użytkownika pulpitu, bez sudo. Katalog musi
być nowy:

```sh
p2browser-probe --javascript --images --report-dir ./raport-youtube-01 2>bledy-youtube-01.log
```

Po odtworzeniu problemu sprawdź `raport-youtube-01/events.tsv` i końcówkę
`bledy-youtube-01.log`. Samo `reason=0` nie rozstrzyga przyczyny.
Log stderr pochodzi również z bibliotek i może zawierać więcej danych
niż filtrowany dziennik aplikacji; przejrzyj go przed udostępnieniem.

Osobna próba z diagnostycznym wyłączeniem wyjścia GL/DMA-BUF dla wideo:

```sh
p2browser-probe --javascript --images --video-compat --report-dir ./raport-youtube-02 2>bledy-youtube-02.log
```

Opcja ustawia `WEBKIT_GST_DISABLE_GL_SINK=1` i
`WEBKIT_GST_DMABUF_SINK_DISABLED=1` przed uruchomieniem GTK i procesów silnika.
Zachowuje sandbox. Nie wymusza programowego dekodera ani konkretnego kodeka;
może być obojętna, gdy polityka `NEVER` już wyłącza przyspieszony tor obrazu.
To próba porównawcza, nie potwierdzona naprawa. `video_output=engine_default`
oznacza brak tej opcji aplikacji; zewnętrzne zmienne środowiska nadal mogą
wpływać na biblioteki.

Jeśli jest podejrzenie Linux OOM, odczyt kernela wymaga odpowiednich praw:

```sh
free -h
sudo journalctl -k -b --no-pager | grep -Ei 'out of memory|oom-kill|killed process|segfault|trap' | tail -n 20
```

Sam komunikat o braku uprawnień nie potwierdza braku wpisów. Brak pasujących
wpisów także nie identyfikuje przyczyny awarii w kodzie użytkownika.

## Wykonane sprawdzenia

W natywnym Trixie amd64, jako zwykły użytkownik, z Xvfb/D-Bus i sandboxem:

Pełny zestaw 55 testów przeszedł. Obejmuje wcześniejsze sprawdzenia CLI,
GUI, pamięci, diagnostyki i pakowania oraz trzy nowe regresje opisane niżej.

- Kontrolowane zakończenie własnego WebProcess przez SIGKILL: interfejs
  pozostaje, powstaje nazwany raport i komunikat, strona czeka na działanie
  użytkownika. Rzeczywiste kliknięcie `Odśwież` ponownie ładuje dokument.
- Ten sam test dla karty w tle przy tekście na pierwszym planie: tekst
  pozostaje, wybór uszkodzonej karty nie wywołuje przeładowania, ręczne
  odzyskanie działa. Oba scenariusze kończą się normalnym zamknięciem okna.
- Nieobsługiwany typ multimediów: kod 4 w raporcie, bez awarii procesu
  i bez prywatnego identyfikatora źródła w dzienniku.
- Lokalne filmy VP8/Vorbis i VP8 bez dźwięku: rzeczywiste kliknięcie,
  `playing`, następnie `ended`, bez awarii; zdarzenie odtwarzania zapisane.
- H.264/AAC w minimalnym środowisku bez libav/plugins-bad: pozostaje
  `waiting`, bez potwierdzonego odtwarzania i bez awarii procesu. Braku
  tych pakietów nie utożsamiamy z przyczyną zgłoszonego problemu.

Testy awarii są w `tests/test_crash_recovery.py`, test błędu multimediów
w `tests/test_probe.py`. XTest służy wyłącznie testom GUI; nie staje się
nową zależnością uruchomieniową aplikacji. Lokalne filmy były syntetyczne,
8-sekundowe, 160×90; ich koszt pamięci nie reprezentuje YouTube.

Próba połączenia z publicznym filmem YouTube w środowisku deweloperskim
otrzymała odmowę proxy HTTPS CONNECT 403. Nie odtworzono tu zdalnego
filmu ani zgłoszonej awarii. Nie obchodzono tej blokady ani sandboxu.
Alpha2 nie przeszła nowej próby na fizycznym P2 ani w gościu i386;
wcześniejsze wyniki alpha1 pozostają opisane oddzielnie.
