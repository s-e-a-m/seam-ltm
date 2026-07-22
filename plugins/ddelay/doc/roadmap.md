# ddelay — future work

Notes recorded at the host check of 2026-07-22.

## ADDELAY — distance should colour the sound, not only postpone it (2026-07-22)

### What is missing

ddelay converts a distance in metres into an integer-sample delay at
c = 331.4 m/s (`seam.math.lib::isos`), rounds it up to the next prime, and
stops there.
The plugin therefore models exactly one consequence of distance: arrival
time.

A loudspeaker twenty metres away is not only late, it is duller than one at
two metres.
Air attenuates high frequencies as sound travels through it, and the effect
is large enough to hear well inside the 30 m the plugin already covers.
The missing half of "distance" is that spectral tilt.

### Why it matters

Time alignment alone makes a far speaker *punctual*, not *far*.
When ddelay is used to place virtual sources at different depths rather than
to correct a physical array, the ear reads the undimmed top end as
nearness and the illusion collapses — everything sounds equally close and
merely mistimed.
Adding the air term is what turns a delay line into a distance.

### Shape the answer probably takes

The owner suggests a **separate plugin**, provisionally `ADDELAY` (the extra
letter for air, or absorption), rather than an option inside ddelay.
That split is worth keeping.
ddelay's promise is exactness — integer samples, no interpolation, no
crossfade, no smoothing, a tool for aligning real loudspeakers — and a
filter in that path changes what the plugin is for.
ADDELAY would inherit the metres-to-samples core and add the filter, so the
alignment tool stays as sharp as it is.

The filter itself is a low-pass whose attenuation grows with both frequency
and distance, most simply a first-order shelf or a short cascade fitted to
the absorption curve at the current distance, redesigned when distance
changes.

**The coefficients want sourcing, not inventing.**
Air absorption is a measured physical phenomenon with a standard behind it:
the attenuation coefficient α depends on frequency, temperature, relative
humidity and atmospheric pressure, and ISO 9613-1 gives it in decibels per
metre, with the acoustics literature (Bass, Sutherland and Zuckerwar on
atmospheric absorption) behind the standard.
Humidity matters as much as distance over these ranges, which is why the
plugin plausibly needs a humidity control and a temperature control beside
the distance one.

The implementer reads that source, cites it in the processor header's
`FAUST REFERENCE` block the way the rest of the suite cites Blumlein, Gerzon
and Linkwitz, and fits the filter to the published curve.
A plausible-sounding tilt invented at the keyboard would be indistinguishable
from the real thing to a casual listener and useless to a student reading
the code to learn what air does.
