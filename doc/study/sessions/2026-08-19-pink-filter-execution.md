# L'esecuzione del piano, e le quattro cose che ha trovato la revisione

Diario dell'esecuzione di `docs/superpowers/plans/2026-08-19-pink-filter-mz.md`,
otto task, ciascuno implementato e revisionato separatamente, più una revisione
finale sull'intero ramo.
Il progetto e le misure stanno in `2026-08-19-pink-filter-design.md`; qui sta
solo ciò che è emerso *eseguendo*, e che nessun commit racconta per intero.

## Quattro difetti del piano, trovati dalle revisioni

Vale la pena elencarli insieme, perché sono tutti dello stesso tipo: cose
plausibili che nessuno aveva motivo di mettere in dubbio.

**1. La tolleranza che non era una tolleranza.**
`doctest::Approx(0.079).epsilon(0.05)` sembra ±5% di 0,079.
La formula di doctest è `|a−b| < epsilon·(scale + max(|a|,|b|))` con
`scale = 1.0`, quindi la tolleranza reale era **0,054 dB**, settanta volte più
larga di quanto la scrittura suggerisse.
Su valori molto minori di uno, la `scale` domina il termine relativo e il
confronto smette di essere relativo.
I sei valori pinnati sono ora confronti assoluti.

**2. L'A/B Faust↔C++ confrontava 24 bit, non 53.**
Il file di architettura includeva `faust/dsp/dsp.h` prima della classe generata,
e quell'header fa `#ifndef FAUSTFLOAT / #define FAUSTFLOAT float`: la guardia
della classe generata diventava un no-op, e `-double` rendeva doppia solo
l'aritmetica interna lasciando i buffer di I/O in singola.
Il risultato riportato — 3,9·10⁻¹⁰ contro una soglia di 1e−9 — non era rumore di
somma su una cascata di sedici sezioni, come il report sosteneva.
Era il **pavimento di quantizzazione di `float`** sul valore di picco della
risposta all'impulso: y[0] = 0,0138, il cui mezzo-ulp è 4,66·10⁻¹⁰.
Il margine vero era 2,6×, non mille.
Con un impulso normalizzato a 1,0 lo stesso codice corretto avrebbe letto
3·10⁻⁸ e sarebbe **fallito**.
Corretto il file di architettura, l'accordo reale è 3,766·10⁻¹⁷ a 48 kHz e
1,637·10⁻¹⁷ a 96 kHz, e la soglia è stata stretta a 1e−12.

**3. Faust non può srotolare un ciclo la cui lunghezza dipende da `ma.SR`.**
`seq(i,N,...)` e `par(i,N,...)` risolvono il conteggio a tempo di compilazione;
`ma.SR` è una costante a runtime.
Il codice Faust scritto nel piano non compilava, e non per un refuso: è
strutturale, verificato con una riduzione di due righe.
La specifica ha quindi la forma che il C++ ha già — cascata di profondità fissa
`NMAX = 32`, sezioni inutilizzate collassate all'identità `(1,0,0)`.
Da sapere: le sezioni inutilizzate **vengono valutate comunque a ogni
campione**, non saltate.

**4. Nessun test eseguiva il percorso audio del plugin.**
Il difetto più grave, e l'unico che solo una revisione sull'intero ramo poteva
vedere.
La ricorrenza delle sezioni era finita in quattro copie — processore, test del
motore, dump per l'A/B, benchmark — e le tre testate non erano quella spedita.
Verificato: scambiare gli indici `[sezione][stream]`, invertire il segno di
`a1`, togliere la riscrittura dello stato, o cancellare `design()` da
`setupProcessing` lasciavano **ctest verde al 100%** in tutti e quattro i casi.
Ora la ricorrenza vive una volta sola, in `pinkFilterBlock`, e tutti e quattro i
chiamanti la usano.

## Perché un test sull'uscita non bastava

Lo scambio degli indici è il caso istruttivo.
Con 64 stream, passo 64 e sedici sezioni la trasposizione è **iniettiva**:
ogni stato finisce in una cella distinta, quindi il filtro produce audio
bit-identico — mentre scrive l'indice 4047 in un buffer da 2048 float.
Il test che confronta le uscite dei 64 stream *passa*.
Ricompilato con AddressSanitizer, lo stesso test riporta `heap-buffer-overflow`.

Il rilevatore deterministico è il test che verifica **dove lo stato atterra**,
con passo più largo del numero di stream così che la scrittura sbagliata resti
dentro i limiti: fallisce su 240 confronti su 256, e i sedici che passano sono
esattamente la diagonale `sec == ch`, dove le due espressioni di indice
coincidono.

La regola che se ne ricava, più generale del caso: **un test che osserva solo
l'uscita non può giudicare un contratto sulla memoria.**

## Il costo, e la domanda che il piano non si è fatto

La regola di decisione chiedeva se 1,5 poli/ottava stesse sotto il 5% di un
core.
Nessuno ha chiesto se ci stesse **1,0**, che è la densità spedita.
Non ci sta: 16,5–20,1% di un core a 48 kHz e 77,8–87,7% a 192 kHz su un Intel
i7-8850H, nell'ordine di ciclo attuale — e sotto carico, rimisurato,
114–120% a 192 kHz, cioè sopra il tempo reale.

Tre leve, nessuna applicata:
1. **Invertire l'ordine del ciclo** — misurata mantenendo il layout
   section-major: 10,55% a 48 kHz e 56,6% a 192 kHz, cioè l'intero fattore 2,1×
   senza toccare il layout che l'argomento SIMD giustifica.
2. **Filtrare solo le righe rivendicate** invece di tutte e 64.
   La sorgente di rumore deve avanzare tutte e 64 per restare deterministica, ma
   i filtri per-stream sono indipendenti: `claimedStart_` e `claimedChannels_`
   sono fissi per tutta un'attivazione e lo stato è azzerato insieme in
   `setActive`, quindi l'uscita sarebbe bit-identica.
   Potenzialmente 8–32×.
3. **SIMD fra stream**, che il layout section-major già permette e che
   richiederebbe anche di trasporre lo scratch a `[campione][stream]`.

## Aperto

- [ ] Il render con `sox` e la lettura della tabella in sala: le due verifiche
      che richiedono un host, e che chiudono i 0,39 dB della vecchia costante.
- [ ] Un banco di prova per il processore, collegato all'SDK come
      `seam_state_test`: senza di esso, cancellare `design()` da
      `setupProcessing` resta invisibile ai test, e il plugin emetterebbe rumore
      **bianco** al livello di calibrazione.
      Mitigazione parziale a una riga: costruire `PinkDesign` già progettato a
      48 kHz, così l'errore degrada a rosa alla frequenza sbagliata invece che a
      bianco.
- [ ] Le tre leve di costo qui sopra.
- [ ] Proporre `spectral_tilt_mz` a monte in GRAME.
- [ ] L'estensione della griglia di `strx` sopra i 20 kHz (perimetro separato).
