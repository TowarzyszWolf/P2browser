# Deterministyczne obciążenia lokalne

`generate.py NOWY_KATALOG` tworzy HTML, PNG i manifest z SHA-256. Dane są
generowane przed pomiarem. Nie zapisujemy w repozytorium kilku MiB
powtarzalnych dokumentów ani wygenerowanych bitmap. Generator wymaga tylko
Pythona 3 i biblioteki standardowej; nie jest zależnością przeglądarki.

Przypadki obejmują 12000 statycznych akapitów, cztery PNG o boku
256/1024/2048/4096 px, 5000 elementów tworzonych przez JS i zachowany bufor
48 MiB oraz wariant mieszany (2000 elementów, 32 MiB, dwa największe PNG).
Pamięć bufora jest dotykana co stronę 4096 B, aby faktycznie utworzyć
przydział, a nie wyłącznie rezerwację przestrzeni adresowej.

JS nie korzysta z timerów ani losowości. Tytuł `P2STRESS:ready:NAZWA`
powstaje po wykonaniu całego synchronicznego zadania. Przy wyłączonym JS
tytuł pozostaje `P2STRESS:disabled:NAZWA`; ten wynik jest oczekiwany dla
próby porównawczej `js-off`.

Pełny protokół i ograniczenia opisuje [stress-method.md](../../docs/stress-method.md).
