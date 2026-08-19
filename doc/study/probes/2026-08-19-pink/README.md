# Sonde del progetto del filtro di pinking (2026-08-19)

Programmi usa-e-getta scritti durante il brainstorming del 2026-08-19, tenuti
perché sono la prova dei numeri citati in
`doc/study/sessions/2026-08-19-pink-filter-design.md`.
Non fanno parte della build e non sono test: il giudice del progetto è
`tests/multipink_pink_test.cpp`.

Si compilano tutti allo stesso modo, senza dipendenze:

```
clang++ -O2 -std=c++17 -o <nome> <nome>.cpp && ./<nome>
```

| file | cosa risponde |
|---|---|
| `sweep.cpp` | `fi.spectral_tilt` (bilineare, fedele a filters.lib:3359) al variare di guardia inferiore, guardia superiore e poli/ottava. Porta l'errore del fattore di larghezza di banda già corretto. |
| `mz.cpp` | bilineare contro matched-Z a parità di parametri, con le curve per banda a 48 e 96 kHz. |
| `mz2.cpp` | matched-Z con guardie spinte fino a poli oltre Nyquist: mostra la saturazione a ~0,8 dB. |
| `res.cpp` | misura il residuo matched-Z in frequenza normalizzata a due fs lontane, e ne mostra la coincidenza. |
| `refine.cpp` | raffinamento iterativo delle posizioni degli zeri contro l'errore digitale. Si ferma a 0,50–0,75 dB: la strada che non ha funzionato, e perché. |
| `fit2.cpp` | fit Nelder-Mead con partenze multiple della sezione di correzione a coefficienti fissi. Da 1,48 dB a 0,029. |
| `final.cpp` | verifica di accettazione end-to-end: scala + correzione, energia di banda ISO, sei sample rate. |
| `rms.cpp` | guadagno RMS e livello a 1 kHz, nuovo contro vecchio, per la costante di calibrazione. |
