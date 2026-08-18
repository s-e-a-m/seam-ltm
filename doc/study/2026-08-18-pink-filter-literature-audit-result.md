<!-- Result of the audit briefed in 2026-08-18-pink-filter-literature-audit.md.
     Run on Claude web with search, 2026-08-18. Read alongside the brief: the
     brief states what we already knew and must not re-derive, this states what
     came back. Not yet acted on -- the decision between A and B is open. -->

# Audit del filtro di "pinking" per un generatore di rumore rosa di qualità da calibrazione

## TL;DR
- Per un riferimento di calibrazione corretto da 44.1 a 192 kHz sull'intervallo 20 Hz–20 kHz, la scelta primaria è **B — `fi.spectral_tilt(N, f0, bw, −0.5)`** (Smith & Smith, arXiv:1606.06154): parametri in Hz (corretto per costruzione a ogni fs), ordine arbitrario, topologia in cascata di sezioni del primo ordine reali (numericamente robusta), con un articolo alle spalle sufficiente a implementarla dall'origine.
- Il **Candidato A** (rimappatura del fit esistente via prototipo analogico + ridiscretizzazione in `prepare(fs)`) è la scelta secondaria giusta e l'unica che preserva la comparabilità con le misure già effettuate; il **Candidato C** (tabella di coefficienti per fs standard) va declassato a fallback/cache, non a metodo, perché è "muto dove non è tabulato" e non copre fs arbitrarie.
- La shortlist A/B/C è corretta ma incompleta: manca un riferimento già normato — **SMPTE ST 2095-1** — che va usato come *specifica di accettazione* sovraordinata; i criteri 2–7 sono quelli giusti, ma vanno riordinati mettendo il **condizionamento numerico** e la **conformità a una tolleranza normativa dichiarata** come vincoli passa/non-passa, non come semplici assi di confronto.

## Key Findings

1. **Esiste uno standard di rumore rosa di riferimento per calibrazione, ed è la cornice giusta per questo audit.** SMPTE ST 2095-1 ("Calibration Reference Wideband Digital Pink Noise Signal", ed. 2015, rev. 2023) definisce esattamente il caso d'uso descritto: un segnale rosa il cui output è il riferimento contro cui si tara un sistema. La Table 1 dell'edizione 2023 fissa il livello-obiettivo assoluto e la tolleranza per banda: *"level of each 1/3-octave band from 20 Hz to 16 kHz · −36.74 dBFS RMS ±0.25 dB · −33.74 dBFS(AES17) ±0.25 dB"*, misurato su almeno un periodo di segnale unico. Questa **uniformità di ±0.25 dB per terzo d'ottava (20 Hz–16 kHz)** è la tolleranza operativa che il tuo generatore dovrebbe rispettare. Nota importante: lo standard **non specifica il filtro di pinking**; prescrive il *risultato* e, negli annessi *informativi*, adotta un rumore band-limited (Table 1: *"Bandwidth 10 Hz to 22400 Hz (−3 dB points); low frequency roll-off ≥21 dB per octave below 10 Hz; high frequency roll-off ≥36 dB per octave above 22.4 kHz"*) con roll-off Butterworth di 4° ordine realizzato per matched-z (basso) e bilineare con prewarping (alto). Il difetto di `multipink` (−3.09 dB a 20 Hz a 96 kHz) **sfora** ampiamente ±0.25 dB e quindi non è di qualità da calibrazione a 96 kHz.

2. **La diagnosi della causa è corretta e ben documentata in letteratura.** `no.pink_filter` di GRAME è il fit IIR a 3 poli/3 zeri "designed using invfreqz in Octave... by fitting three poles and zeros to a minimum-phase 1/√f amplitude response" (J.O. Smith, LAC-12). I coefficienti pubblicati sono `fi.iir((0.049922035, -0.095993537, 0.050612699, -0.004408786), (-2.494956002, 2.017265875, -0.522189400))`. Essendo un fit *nel piano z*, le sue frequenze d'angolo sono frazioni fisse di fs; raddoppiando fs, la regione a −3 dB/ottava trasla di un'ottava verso l'alto e la coda bassa esce dal fit — esattamente il comportamento misurato.

3. **La famiglia Kellet è uno standard de-facto da mailing list, non un progetto parametrico.** I coefficienti "instrumentation grade" (accurati a ±0.05 dB sopra 9.2 Hz a 44100 Hz) e "economy" (±0.5 dB) sono stati postati da Paul Kellett sulla music-dsp list nell'ottobre 1999, archiviati da Robin Whittle su firstpr.com.au. Lo stesso Kellett ha scritto sulla list, verbatim: *"Unfortunately there is no algorithm behind those particular coefficients, just the general idea that it's a mixture of 1st order lowpasses with increasing cutoff frequency"* — cioè sono accordati a mano per una specifica fs. È la ragione strutturale per cui non "scalano" con fs.

4. **Il criterio del condizionamento numerico è dirimente e la tua misura è confermata dalla letteratura.** La forma diretta (Direct Form I/II) è notoriamente sensibile alla quantizzazione dei coefficienti del denominatore quando i poli sono ammassati vicino a z=1 (DC). Brian Neunaber, "Parameter Quantization in Direct-Form Recursive Audio Filters", Table 4: *"the error in filter corner frequency (Fc) for Fc=20Hz rises from about 0.2% to over 2.5% when the sample rate is increased from 48kHz to 192kHz using double precision"*; per Q=1.414 a 20 Hz l'errore di Q passa da ~1% (48 kHz) a >4% (192 kHz), e la Fc minima rappresentabile sale da ~2.6 Hz (48 kHz) a ~10.6 Hz (192 kHz), sempre *in doppia precisione*. Questo conferma il tuo collasso a 160 Hz e sotto in singola precisione, e implica che a f/fs ≈ 4·10⁻⁴ **solo topologie a bassa sensibilità sopravvivono in singola precisione**: cascata di sezioni del primo ordine reali, forme state-variable/trasposte, o Direct Form I in doppia precisione limitatamente alle sezioni più basse.

5. **La fase minima non è necessaria per un riferimento di misura.** Filtrando rumore bianco, la fase del filtro di pinking è arbitraria: il problema di progetto può essere formulato per corrispondere solo alla risposta in modulo di potenza |H|²=1/ω (Smith & Smith). Ciò che conta per il tuo `kCalibrationOffsetDb` è che l'attenuazione RMS integrata cambia quando cambi filtro (banda, roll-off ai bordi, ordine): ogni cambio di metodo o di ordine richiede di **ricalcolare la costante di calibrazione**.

## Details

### Mappa dei metodi (criterio 1)

| Metodo | Cosa ottimizza | Costo | Assunzione su fs |
|---|---|---|---|
| **Fit IIR nel piano z** (invfreqz/Levi, Prony, Steiglitz–McBride, Yule–Walker) | errore (spesso ai minimi quadrati, pesato) sulla risposta campionata su una griglia in frequenza | ordine molto basso (3–6 poli); economicissimo a runtime | **legato a fs**: la griglia è in frequenza normalizzata; rifittare per ogni fs. È esattamente `no.pink_filter`/`multipink` |
| **Cascata polo-zero in forma chiusa** (Smith & Smith 2016; predecessori: Oustaloup recursive approximation; Charef) | slope log-log costante = α; approccio Chebyshev equiripple nel limite di densità infinita | ordine su misura (una sezione del 1° ordine per polo); parametri in Hz | **indipendente da fs per costruzione** (poli/zeri prewarpati e ridiscretizzati con la fs corrente) |
| **Prototipo analogico + trasformazione bilineare/matched-z** | risposta analogica ideale nota, poi mappata; prewarping fissa una frequenza esatta | dipende dall'ordine del prototipo | **corretto se ridiscretizzato a runtime**; è il meccanismo del Candidato A e del roll-off SMPTE |
| **Integratore frazionario s^(−1/2)** (Oustaloup, Charef, continued-fraction) | modulo/fase di s^α su una banda [ωb, ωh] | ordine 3–9 tipico | banda in rad/s → **indipendente da fs** se le frequenze sono in Hz |
| **Shaping via FFT/overlap-add** | modulo esatto imposto bin per bin (√S(ω)) | costo per blocco FFT; latenza | indipendente da fs; ma **non è un filtro streaming** a stato minimo |
| **Voss–McCartney e affinamenti** (McCartney 1999; Trammell 2006, stocastico) | somma di S&H a tassi in ottava; genera direttamente rosa | bassissimo | **è un generatore, non un filtro di shaping**: non può rosare un ingresso arbitrario |

Note sui bordi: Voss–McCartney ha un ripple residuo ineliminabile (~1 dB) indipendentemente dal numero di righe (analisi di Allan Herriman su firstpr.com.au); lo stesso Trammell scrive che per un "laboratory grade noise source... this is not the best way to go". Sono quindi da escludere per uso da calibrazione, oltre a non essere filtri.

### Accuratezza in funzione dell'ordine (criterio 2)

Questo è il punto in cui la letteratura primaria è **meno quantitativa di quanto si vorrebbe, e va detto esplicitamente**. L'articolo Smith & Smith (arXiv:1606.06154) **non fornisce una tabella esplicita "poli per ottava → dB di ripple massimo"** sulla banda 20 Hz–20 kHz; quantifica l'accuratezza come *errore di pendenza* (in neper/neper) e in forma grafica. In particolare:

- **Spaziatura in ottava (r=2), α=−0.5**: l'unica cifra in dB è di *bordo*, non di ripple interno — "the gain drops by less the[sic] 2 dB at ω = 1" per N=5, senza banda di guardia.
- **Densità maggiore → errore minore** (qualitativo): "the interlacing pole-zero pattern can be made more dense, e.g., by placing poles every half octave, or third octave"; e Fig. 5 mostra il miglioramento da N=5 a N=12.
- **Banda di guardia K**: introducendo K sezioni polo-zero *prima e dopo* la banda utile, l'errore interno crolla. "Numerical experiments indicate that K = 3 is a cost-effective choice." Per **N=20, K=3**, α=−0.5 (poli a spaziatura in neper, ≈1.44 poli/ottava) l'errore di pendenza normalizzato è entro ~0.999–1.001 (≲±0.1%); con K=0 lo stesso N=20 varia ~0.8–1.05 ai bordi.
- **Ottimalità Chebyshev**: l'abstract dichiara che il metodo è *"close to Chebyshev optimal in the interior of the pole-zero array, approaching conjectured Chebyshev optimality over all frequencies in the limit as the order approaches infinity"* — cioè ripple equioscillante, un ciclo per coppia polo-zero, che diminuisce infittendo.

Conclusione operativa: la leva d'accuratezza è **densità di poli e banda di guardia, non ordine totale in sé**. Per ±0.5 dB su 10 ottave è tipicamente sufficiente ~1 polo/ottava con K≈3 (coerente con lo `spectral_tilt(9,...)` della demo GRAME, che copre bw = 0.8·fs/2 − f0); per ±0.1 dB serve infittire a ~2 poli/ottava ed estendere la banda di guardia oltre 20 Hz–20 kHz. **La verifica va fatta empiricamente** (FFT del filtro contro slope ideale), perché nessuna fonte primaria dà una legge chiusa densità→dB.

Per contrasto, il fit IIR a basso ordine (Kellet "instrumentation") raggiunge ±0.05 dB sopra 9.2 Hz *ma solo alla fs di progetto*; è accuratissimo dove fittato e degrada altrove — l'opposto del profilo di robustezza che serve qui.

### Comportamento ai bordi di banda, 20–40 Hz (criterio 3)

Ogni metodo degrada sotto il proprio limite inferiore di progetto. Per la cascata polo-zero, il degrado di bordo è controllato dai K poli di guardia posti *sotto* fmin: senza di essi l'errore a 20 Hz è dominato dal primo polo/zero (il "drop <2 dB" di Smith & Smith). Porre f0 ben sotto 20 Hz (es. 10–15 Hz) e usare K≥3 sposta l'errore fuori banda. Il fit z-plane, invece, non ha manopola di bordo: dove il fit finisce, finisce — ed è precisamente il fallimento di `multipink` (−3.09 dB a 20 Hz a 96 kHz; entro 0.6 dB a 44.1/48 kHz). SMPTE affronta il bordo separando lo *slope* dal *band-limiting*: 3 dB/ottava esteso, poi Butterworth 4° ordine a 10 Hz (≥21 dB/ottava sotto). Adottare la stessa separazione (pinking a banda larga + highpass di protezione opzionale) è la pratica corretta per 20–40 Hz, dove la correzione d'ambiente conta di più.

### Condizionamento numerico (criterio 4)

Gerarchia di robustezza a f/fs ≈ 4·10⁻⁴ (20 Hz @ 48 kHz), dal migliore al peggiore:

1. **Cascata di sezioni del primo ordine reali** (una per polo, come in `spectral_tilt`): ogni sezione quantizza il proprio polo indipendentemente; la sensibilità è minima perché non c'è denominatore di ordine elevato con radici ammassate. È la topologia che sopravvive alla singola precisione.
2. **Forme state-variable / trasposte** (Chamberlin modificato, TDF-II): densità di poli quantizzati più alta vicino a DC; raccomandate esplicitamente in letteratura per poli low-Q a bassa frequenza.
3. **Direct Form (I/II) di ordine ≥2 con poli vicini a DC**: da evitare. "A higher-order transfer function should never be realized as a single direct form structure, but realized as a cascade or parallel of second-order and first-order sections" (IEEE). Neunaber quantifica l'errore residuo persino in doppia precisione a fs alte.

La tua misura (bande a 1/3 d'ottava ≤160 Hz sul floor dell'epsilon a 32 bit, corrette in doppia) è la manifestazione attesa. Il criterio va trattato come **vincolo passa/non-passa**: qualunque metodo che a runtime richieda una forma diretta di ordine elevato è squalificato per un riferimento a fs fino a 192 kHz.

### Fase e livello (criterio 5)

- **Fase minima non necessaria**: filtrando bianco, solo |H|² conta. Il fit invfreqz di GRAME è a fase minima per scelta, non per necessità.
- **Livello / `kCalibrationOffsetDb`**: l'attenuazione RMS integrata dipende da banda, ordine e roll-off. Cambiando da `multipink` a `spectral_tilt` (o rimappando A), **la costante va rimisurata** su un periodo lungo (SMPTE usa ≥10 s di periodo unico). Attenzione alla convenzione di metering: la Table 1 di SMPTE ST 2095-1:2023 fissa lo stesso segnale a due scale diverse — *"Level · −21.5 dBFS RMS · −18.5 dBFS(AES17)"* — cioè un offset di 3.01 dB tra dBFS RMS e dBFS(AES17). È un errore facile nel definire l'offset.

### Uso multi-stream, 64 flussi indipendenti (criterio 6)

- **Filtri (A, B, fit z-plane)**: stato **per-istanza**, nessuno stato condiviso. 64 istanze di `spectral_tilt` con 64 sorgenti di rumore indipendenti sono mutuamente scorrelate per costruzione. È l'opzione pulita.
- **Voss–McCartney / Trammell**: mantengono un vettore di stato a righe; se ingenuamente condividono generatori o schedule di update tra flussi, **correlano i flussi**. Trammell nota inoltre che la sua variante a "at most one stage per update" introduce dipendenza statistica tra stadi (perde indipendenza completa). Ulteriore ragione per non usarli qui.
- Requisito pratico: ogni stream deve avere il proprio PRNG con seed distinto e periodo lungo (SMPTE richiede periodo unico ≥10 s; per 64 stream servono seed/sequenze non sovrapposte).

### Cosa dicono gli standard (criterio 7)

- **IEC 61260-1:2014** (filtri di banda d'ottava e frazione d'ottava): definisce classi 1 e 2. Alla frequenza esatta di centro banda l'acceptance limit sull'attenuazione relativa è **−0.4/+0.4 dB per classe 1** e **−0.6/+0.6 dB per classe 2** (si allarga a −0.4/+0.5 dB a G^±1/8 appena fuori centro); coerentemente, §5.12.2 fissa che *"the acceptance limits for the effective bandwidth deviation are ±0,4 dB for class 1 instruments and ±0,6 dB for class 2 instruments"*. È lo standard dei *filtri di analisi* con cui si misurerà il tuo rosa, non del rosa stesso — ma fissa il metro: se lo strumento di misura è classe 1 (±0.4 dB a centro banda), un riferimento con errore >0.25 dB/terzo d'ottava è già dello stesso ordine dell'incertezza dello strumento e va evitato.
- **SMPTE ST 2095-1**: lo standard *del segnale* rosa di riferimento per calibrazione. Uniformità **±0.25 dB per terzo d'ottava (20 Hz–16 kHz)**, crest factor 11.5–12 dB, banda 10 Hz–22.4 kHz, distribuzione gaussiana, periodo unico ≥10 s. È la tolleranza normativa che il tuo generatore *dovrebbe rispettare*.
- **IEC 60268-1 / -21 / -16**: definiscono spettri di programma simulato e rumore rosa band-limited per prove su altoparlanti (es. tabella di potenze per terzo d'ottava con roll-off a bassa frequenza), e rimandano a IEC 61260 per i filtri di banda. Utili se in futuro servisse un rosa "pesato programma" anziché rosa puro.

Non esiste, per quanto emerge, uno standard che imponga *quale* filtro di pinking usare; gli standard vincolano il *segnale risultante* (SMPTE, ±0.25 dB/terzo) e i *filtri di analisi* (IEC 61260, ±0.4 dB classe 1).

## Recommendations

**La shortlist va corretta così**: B come metodo primario, A come compatibilità/ponte, C declassato a cache, + SMPTE ST 2095-1 come specifica di accettazione sovraordinata.

1. **Adotta B — `fi.spectral_tilt(N, f0, bw, −0.5)` — come generatore di riferimento.** Motivazione: corretto per costruzione a ogni fs da 44.1 a 192 kHz; topologia in cascata di sezioni del primo ordine reali (sopravvive alla singola precisione, ma usa comunque doppia precisione per le sezioni più basse); fase minima non richiesta; stato per-istanza (64 stream scorrelati). Procedura di progetto in fondo.
   - **Soglia di accettazione**: FFT del filtro contro slope −3.01 dB/ottava; il segnale generato deve stare entro **±0.25 dB per ogni terzo d'ottava 20 Hz–16 kHz** (SMPTE) a *tutte* le fs target. Se un ordine non ci arriva a 192 kHz, aumenta poli/ottava e/o estendi la banda di guardia (f0 ↓ verso 10–15 Hz, bw ↑, K≥3).

2. **Mantieni A — rimappatura del fit esistente — come modalità di compatibilità.** Estrai poli/zeri di `multipink`, inverti la bilineare per il prototipo analogico, ridiscretizza in `prepare(fs)` prewarpando alla **frequenza di progetto originale** (da stabilire empiricamente: è la fs, verosimilmente 44.1 o 48 kHz, a cui `multipink` è già entro 0.6 dB a 20 Hz). Alla fs di progetto il filtro resta bit-identico a oggi (misure storiche confrontabili); a ogni altra fs diventa corretto. Usa A quando devi confrontarti con dati raccolti col vecchio filtro; usa B per ogni nuova campagna di calibrazione.

3. **Declassa C a cache, non a metodo.** Una tabella di set di coefficienti prefittati per 44.1/48/88.2/96/176.4/192 kHz è utile solo come *ottimizzazione* (evita di rieseguire il progetto a ogni `prepare`), generata *da* A o *da* B offline. Da sola è "muta dove non tabulata" e non copre fs non standard: inaccettabile come unico meccanismo per un riferimento.

4. **Separa slope e band-limiting, come SMPTE.** Applica il pinking a banda larga; se serve protezione woofer/subwoofer o conformità a un profilo band-limited, aggiungi un highpass Butterworth (matched-z in basso) come stadio distinto, non incorporato nel pinking.

5. **Ricalcola `kCalibrationOffsetDb`** dopo aver fissato metodo e ordine, integrando l'RMS su ≥10 s, e **fissa la convenzione di metering** (dBFS RMS vs dBFS(AES17), differenza 3.01 dB).

**Benchmark che cambierebbero la raccomandazione:**
- Se il vincolo diventasse "±0.1 dB/terzo d'ottava" (più stretto di SMPTE), servirebbe infittire B verso ~2 poli/ottava con banda di guardia estesa, e verificare che la doppia precisione basti a 192 kHz sulle sezioni più basse.
- Se la comparabilità storica prevalesse sulla correttezza a fs alte, A diventerebbe primario.
- Se emergesse un vincolo di CPU stringente (molti più di 64 stream), un fit z-plane a basso ordine *ricalcolato per fs* (A generalizzato) sarebbe più economico di uno `spectral_tilt` di ordine alto — ma con margini di bordo peggiori.

## Procedura di progetto per i due migliori (implementabili dall'articolo)

### B — `spectral_tilt` (Smith & Smith, arXiv:1606.06154), α = −1/2

1. Scegli banda [fmin, fmax] (es. 20 Hz–20 kHz; meglio 10 Hz–22 kHz per margine di bordo) e ordine N (numero di poli).
2. Scegli K = numero di poli di guardia prima e dopo la banda (K=3 raccomandato).
3. Risolvi per la prima frequenza di polo f₁ e il rapporto r = f_{k+1}/f_k il sistema lineare 2×2 (con x̃ = ln x):
   [1  K; 1  N−K−1]·[f̃₁; r̃] = [f̃min; f̃max].
4. Poli s-plane: pₙ = −2π f₁ r^{n−1}, n=1..N. Zeri: z_m = p_m · r^{−α}, con α=−1/2 (per α=−1/2 gli zeri stanno a mezza spaziatura log tra i poli).
5. **Digitizzazione** (indipendenza da fs): usa la bilineare con prewarping. Scegli c = 2π f₁ / tan(π f₁ T) per mappare esattamente f₁; prewarpa gli altri poli f̂_k = f₁·tan(π f_k T)/tan(π f₁ T). Limita N ai poli con f_{N+1} ≤ fs/2. Poli s all'infinito → z=−1 (innocui se #poli ≥ #zeri).
6. Realizza come **cascata di sezioni del primo ordine reali** (una per coppia polo-zero). In Faust: `fi.spectral_tilt(N, f0, bw, -0.5)` fa già i passi 3–6 a compile time con la fs corrente. Verifica con FFT contro slope ideale; regola N, f0, bw finché entro ±0.25 dB/terzo a ogni fs.

### A — rimappatura del fit `multipink` esistente

1. Prendi i coefficienti attuali (num/den z-plane di `no.pink_filter`) e fattorizza in poli/zeri z: {z_i}, {p_i}.
2. **Inverti la bilineare** per tornare all's-plane, usando la stessa T=1/fs di progetto originale: per ogni radice, s_root = c·(1−root)/(1+root), con c=2/T (o c prewarpato se il fit originale lo era — da determinare empiricamente confrontando le frequenze d'angolo ricostruite con la risposta nota).
3. **Stabilisci la frequenza di progetto** fs₀ per tentativi: è la fs a cui la ridiscretizzazione riproduce bit-per-bit i coefficienti odierni (verosimilmente 44.1 o 48 kHz, coerente col fatto che a 44.1/48 kHz `multipink` è entro 0.6 dB e a 96 kHz no).
4. In `prepare(fs)`: **ridiscretizza** i poli/zeri s-plane con la fs corrente via bilineare con prewarping ancorato a fs₀ (così a fs=fs₀ il filtro è identico all'attuale). Ricostruisci num/den e realizza in cascata di sezioni del 1°/2° ordine (non forma diretta di ordine 3+).
5. Verifica: a fs₀ i coefficienti coincidono con gli attuali; a 96/192 kHz la deviazione a 20 Hz deve rientrare (era −3.09 dB; obiettivo <0.25 dB). Ricalcola `kCalibrationOffsetDb` se cambia l'RMS.

## Caveats
- **Lacuna quantitativa dichiarata**: la letteratura primaria (Smith & Smith) non fornisce una legge chiusa "poli/ottava → dB di ripple" sulla banda audio; l'accuratezza è data come errore di pendenza e in forma grafica. Le soglie in poli/ottava sopra (~1/ottava per ±0.5 dB, ~2/ottava per ±0.1 dB) sono estrapolazioni pratiche da verificare empiricamente con FFT del tuo filtro, non numeri tratti dall'articolo.
- **Frequenza di progetto di `multipink` non documentata**: il Candidato A richiede di determinarla empiricamente; se il fit originale usava prewarping non standard, l'inversione della bilineare va calibrata sui dati.
- **Kellet ha coefficienti senza algoritmo**: qualsiasi tentativo di "scalare" i coefficienti Kellet con fs è mal posto per stessa ammissione dell'autore; A/B non ereditano questo problema perché lavorano da un prototipo analogico o da parametri in Hz.
- **SMPTE ST 2095-1 copre solo 48 e 96 kHz** e un caso d'uso cinema (definisce *"an example algorithm to generate compliant LPCM pink noise signals in DSP devices with sampling rates of 48.00 kHz and 96.00 kHz"* per una B-chain di sala); la sua tolleranza ±0.25 dB/terzo è però un ragionevole target trasferibile a 44.1–192 kHz. Gli annessi con l'algoritmo sono *informativi*, non normativi.
- **Accesso agli standard**: i valori IEC 61260-1 qui riportati provengono da preview/estratti pubblici; i valori SMPTE dal testo integrale liberamente accessibile su pub.smpte.org. Per una certificazione formale conviene verificare sull'edizione acquistata di IEC 61260-1:2014.