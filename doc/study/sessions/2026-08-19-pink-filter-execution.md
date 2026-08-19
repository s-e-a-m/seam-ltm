# L'esecuzione del piano, e le cinque cose che ha trovato — quattro leggendo, una misurando

Diario dell'esecuzione di `docs/superpowers/plans/2026-08-19-pink-filter-mz.md`,
otto task, ciascuno implementato e revisionato separatamente, più una revisione
finale sull'intero ramo.
Il progetto e le misure stanno in `2026-08-19-pink-filter-design.md`; qui sta
solo ciò che è emerso *eseguendo*, e che nessun commit racconta per intero.

## Quattro difetti del piano, trovati dalle revisioni

Vale la pena elencarli insieme, perché sono tutti dello stesso tipo: cose
plausibili che nessuno aveva motivo di mettere in dubbio.
Il quinto, in fondo, è di un tipo diverso — e nessuna lettura poteva trovarlo.

**1. La tolleranza che non era una tolleranza.**
`doctest::Approx(0.079).epsilon(0.05)` sembra ±5% di 0,079.
La formula di doctest è `|a−b| < epsilon·(scale + max(|a|,|b|))` con
`scale = 1.0`, quindi la tolleranza reale era **0,054 dB**, settanta volte più
larga di quanto la scrittura suggerisse.
Su valori molto minori di uno, la `scale` domina il termine relativo e il
confronto smette di essere relativo.
I sei valori pinnati sono ora confronti assoluti.

Ed è ricomparso: `doctest::Approx(0.28).epsilon(0.05)` sul passo di RMS totale
fra 48 e 96 kHz ammetteva ±0,064 dB, cioè ±23%, sopravvivendo alla stessa
revisione che aveva corretto i sei.
Anche quello è ora un confronto assoluto (0,2825 ± 0,005 dB).
La lezione operativa: `Approx().epsilon()` è sicuro solo su valori dell'ordine
dell'unità o più grandi; sotto, va scritto `fabs(a - b) < tol`.

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

## Il quinto difetto, e l'unico che solo una misura poteva trovare

**5. La calibrazione contava il filtro e dimenticava la sorgente.**
Il più grave dei cinque, trovato *dopo* otto revisioni di task e una revisione
sull'intero ramo, con il plugin già in host: `multipink` direttamente dentro
`strx`, 76 s di integrazione a 48 kHz e 61 s a 96 kHz.

| | 48 kHz | 96 kHz |
|---|---|---|
| livello medio di banda di terzo d'ottava | −38,4 dB | −41,3 dB |

Due virgola nove dB di caduta dove il progetto **prometteva zero**: la decisione
di ancorare il livello di BANDA (e non l'RMS totale) esisteva proprio perché un
amplificatore tarato a una frequenza di campionamento restasse tarato a
un'altra.

La sonda che aveva giustificato la costante fissa `kCalibrationOffsetDb = 36,180`
misurava l'integrale di |H|² del filtro sulla banda, lo trovava invariante — e lo
**è**, il filtro è ancorato in Hz e la misura era corretta.
Solo che il livello di banda ha quattro termini, non tre:

```
livello di banda = guadagno + RMS della sorgente
                 + 10*log10( ∫_banda |H(f)|² df )
                 - 10*log10(fs/2)          ← la DENSITÀ della sorgente
```

L'ultimo termine è quello dimenticato.
L'RMS del generatore LCG è lo stesso a ogni frequenza di campionamento, ma quella
potenza è distribuita su 0…fs/2: la sua **densità spettrale** si dimezza quando
fs raddoppia, e ogni banda fissa perde 3,01 dB per raddoppio per quanto
perfettamente il filtro sia ancorato.
Previsto 3,01 dB, misurato 2,9 dB.

Corretto: `offset(fs) = 36,180 + 10*log10(fs / 48000)`, dove 36,180 è il valore
**a 48 kHz**, la frequenza di riferimento in cui il termine di densità è nullo.
Il livello previsto della banda di 1 kHz passa da
−39,114 / −39,489 / −42,127 / −42,501 / −45,139 / −45,513 dBFS a
−39,4825 / −39,4891 / −39,4847 / −39,4909 / −39,4866 / −39,4923 dBFS
(44,1 / 48 / 88,2 / 96 / 176,4 / 192 kHz): 0,0098 dB di dispersione, e 0,0837 dB
nel caso peggiore su tutte e trenta le bande misurabili a ogni frequenza.
L'RMS totale, di conseguenza, **sale** di circa 0,27–0,28 dB per raddoppio
(0,2825 dB da 48 a 96 kHz, 0,2844 da 44,1 a 88,2, 0,2652 da 96 a 192: non è un
numero solo, perché la sezione più alta della scala si sposta) — ed è giusto
così: se ogni banda sta ferma, ogni ottava in più aggiunge un po' di energia
totale.

Il filtro non è stato toccato: le sei deviazioni di accettazione restano
0,079 / 0,071 / 0,080 / 0,072 / 0,081 / 0,072 dB.

### Perché nessuna revisione poteva vederlo

Nove revisioni sono passate sopra questo numero.
Non hanno sbagliato: **ognuna ha verificato il codice contro la specifica, e il
codice implementava la specifica fedelmente.**
Era la specifica a essere sbagliata — un errore di fisica, non di trascrizione, e
una revisione che *legge* non può trovare un errore di fisica nel documento
contro cui sta leggendo.
Poteva trovarlo solo una misura del percorso di segnale reale, ed è ciò che l'ha
trovato.

Due cause strutturali, entrambe rimosse:

1. **La costante viveva dove nessun test poteva leggerla.**
   Stava in `multipink_processor.h`, che include l'SDK VST3, e il binario dei
   test non può includere l'SDK.
   Ora sta in `multipink_pink.h`, che è SDK-free, come
   `PinkDesign::kCalibrationOffsetBaseDb` più l'accessore
   `calibrationOffsetDb()` calcolato una volta in `design(fs)` e letto come load
   sul thread audio.
2. **Il test asseriva la derivazione, non la proprietà.**
   `-(whiteRmsDb + rmsGainDb) == 36,180` è vera per costruzione e non dice nulla
   su ciò che il progetto promette.
   Ora il test asserisce la promessa: il livello previsto è invariante sulle sei
   frequenze, su tutte e trenta le bande misurabili a ogni frequenza, entro
   0,20 dB.
   La soglia è **derivata**, non scelta: sotto c'è il ripple del filtro stesso,
   che l'invarianza non può battere perché ogni frequenza progetta la propria
   scala e colloca una data banda in un punto leggermente diverso del ripple;
   sopra c'è l'errore da 3,01 dB per raddoppio.
   Il numero che regge il pavimento è quello **misurato**: 0,0837 dB di
   dispersione fra frequenze, sulla banda di 15,85 kHz.
   I 0,071–0,081 dB per frequenza del test di accettazione sono *coerenti* con
   quel valore e non lo implicano: il ripple di accettazione è uno scostamento
   dalla media *della singola frequenza*, su un insieme di bande giudicate che
   cambia con la frequenza, mentre il test di invarianza confronta livelli
   assoluti a banda fissa fra frequenze diverse. Raddoppiarlo per ottenere un
   presunto limite di ~0,16 dB è un'euristica che per fortuna racchiude la
   misura, non una derivazione.
   Una soglia di 0,01 dB, come nella prima stesura, passava per 2% di margine e
   sarebbe fallita se qualcuno l'avesse riscritta sulla banda di 15,85 kHz: un
   numero senza derivazione è un numero che il prossimo sposterà tirando a
   indovinare.
   Verificato per mutazione — togliendo il termine di densità diventa rosso di
   3,010 dB a 96 kHz e 6,021 dB a 192 kHz.

La regola, che vale oltre questo caso: **una sonda che conferma la specifica non
la sta mettendo alla prova.**
La sonda misurava il filtro perché la specifica parlava del filtro.
Il segnale che esce dal plugin non è il filtro: è il filtro *per* la sorgente, e
nessuno l'ha misurato finché non l'ha misurato l'analizzatore in host.

### La stessa confusione, due volte

Ma quella regola, da sola, è ancora troppo comoda: descrive *come* l'errore è
sfuggito, non *che cosa* fosse.
Perché questo progetto lo stesso errore l'ha fatto **due volte**, a una sonda di
distanza, e il diario di progetto lo registra in cima a sé stesso.

`2026-08-19-pink-filter-design.md`, § «**Errore 1 — densità invece di energia**»:
la prima sonda dava scostamenti di 14–20 dB, e nell'integrale di banda mancava
il fattore di larghezza (fu − fl).
Misuravo la densità media di potenza invece dell'energia. Nel **filtro**.

L'errore corretto oggi è lo stesso: densità invece di energia in una banda.
Nella **sorgente**.
La prima volta mancava (fu − fl) nell'integrale del filtro; la seconda mancava
1/(fs/2) nella densità della sorgente. È la stessa quantità dimenticata nello
stesso punto dell'espressione, su due fattori diversi dello stesso prodotto.

La regola vera, quindi, non è solo che una sonda compiacente non prova nulla.
È questa: **un integrale di banda ha una larghezza dentro, ovunque compaia.**
Un livello di banda è sempre una densità moltiplicata per una larghezza, e ogni
fattore del segnale — il filtro *e* la sorgente — porta la propria.
Quando se ne dimentica una, il conto resta dimensionalmente muto: dB contro dB,
nessun compilatore protesta, e il risultato ha ancora un ordine di grandezza
plausibile.
Entrambe le volte è stata la **plausibilità** a proteggere l'errore — 14–20 dB
sembravano un filtro mal fittato, 3 dB sembravano tolleranza di misura — e
entrambe le volte a smascherarlo è stata una misura del percorso completo, non
una rilettura.

Il seguito operativo sta nel test: la sorgente non è più un termine assunto.
`Seam::multipink::whiteNoiseRow` vive accanto a `pinkFilterBlock` in
`multipink_pink.h`, e il caso end-to-end in
`tests/multipink_pink_engine_test.cpp` genera con la sorgente vera, filtra con
il filtro vero, applica il guadagno vero e legge il livello di banda dai
campioni, a 48 e a 96 kHz.
Verificato per mutazione: cambiando il divisore del cast da 2³¹ a 2³⁰ quel caso
diventa rosso di 6,02 dB mentre le altre 203 asserzioni restano verdi — che è
esattamente il contrasto che prima non esisteva.

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

## La verifica in sala, che è ciò che ha trovato l'errore e ciò che lo chiude

Cinque misure diritte di `multipink` dentro Reaper, senza microfono né
altoparlante, lette da `strx` attraverso il banco ISO 266.

| | fs | livello medio di banda |
|---|---|---|
| prima del fix | 48 kHz | −38,38 dB |
| prima del fix | 96 kHz | **−41,31 dB** |
| dopo il fix | 48 kHz | −38,33 |
| dopo il fix | 96 kHz | −38,33 |
| dopo il fix | 96 kHz, ripetuta | −38,39 |

Prima: 2,93 dB di caduta raddoppiando il sample rate, dove il progetto ne
prometteva zero.
Dopo: 0,00 e −0,06 dB.
Il valore a 48 kHz non si muove, come deve, perché a 48 kHz il termine di
correzione vale zero per costruzione.

**Il pavimento di rumore del metodo, misurato.**
Le due letture a 96 kHz distano sei secondi e guardano lo stesso identico
segnale: differiscono di 0,20 dB mediani per banda e 1,00 dB nel caso peggiore
(banda 1,25 k, −37,9 contro −38,9).
Quindi la dispersione `comp_db` di ±0,5 che compare in queste tabelle è rumore
di misura, non errore del filtro — che sta a 0,07 dB.
Le medie delle due ripetizioni invece differiscono di 0,06 dB, circa √31 volte
meglio della singola banda, che è quanto ci si aspetta mediando 31 bande
indipendenti: il rumore si comporta da rumore.

La conseguenza metodologica vale più del numero.
Una misura di questo tipo **valida il sistema**; non può giudicare il filtro,
perché non lo risolve.
Il giudizio del filtro resta analitico, ed è per questo che il test di
accettazione lo è.

## Aperto

- [x] La lettura della tabella di banda in host — fatta, ed è ciò che ha trovato
      il difetto 5.
- [x] Il render con `sox` — fatti due, offline, misurati **per canale**:
      −22,997 dBFS a 48 kHz contro un previsto −23,000, e −22,721 a 96 kHz
      contro −22,718. Tre millidecibel a entrambi i rate, e la salita fra i due
      misura +0,276 dB contro il +0,2825 previsto. Il modello a quattro termini
      — guadagno, RMS della sorgente, integrale di banda, densità della
      sorgente — descrive il segnale vero.
      Si misura un canale solo: `sox stat` sul render a 4 canali media stream
      che su 31 s si disperdono di qualche centesimo di dB, e legge −23,024.
      Quindi i 0,39 dB stavano nella misura del 7 maggio, non nel calcolo, ma
      **la causa non è determinata**: un lead-in silenzioso, un canale non a
      livello dentro una media multicanale o un fader la produrrebbero tutti, e
      di come fu fatto quel render non resta traccia.
      Conseguenza retroattiva: con 26,45 il vecchio `multipink` emetteva 0,39 dB
      sopra il livello dichiarato, con un bias comune a tutte le bande e quindi
      ininfluente sulle decisioni di equalizzazione.
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
