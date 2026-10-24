//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · Pd · multipink~ — four calibrated pink streams, Pd glue
//
// [multipink~]
//   no inlet, no arguments, no controls
//   outlets: four independent pink streams, every third-octave band at
//            -39.5 dBFS at any rate (total RMS -23 dBFS at 48 kHz)
//
// Everything that computes lives in multipink_engine.h; this file only talks
// to Pd. See README.md for build and install.
//─────────────────────────────────────────────────────────────────────────────

#include "m_pd.h"
#include "multipink_engine.h"

static_assert (sizeof (t_sample) == sizeof (float), "multipink~ is built for 32-bit Pd");

static t_class* multipink_class;

struct t_multipink
{
    t_object x_obj;
    Seam::pd::MultipinkEngine* engine;
};

static t_int* multipink_perform (t_int* w)
{
    auto* x = reinterpret_cast<t_multipink*>(w[1]);
    t_sample* out[4] = { reinterpret_cast<t_sample*>(w[2]), reinterpret_cast<t_sample*>(w[3]),
                         reinterpret_cast<t_sample*>(w[4]), reinterpret_cast<t_sample*>(w[5]) };
    x->engine->process (out, static_cast<int>(w[6]));
    return w + 7;
}

// No signal inlet, so sp[] holds the four outlets only.
static void multipink_dsp (t_multipink* x, t_signal** sp)
{
    x->engine->prepare (sp[0]->s_sr, sp[0]->s_n);
    dsp_add (multipink_perform, 6, x,
             sp[0]->s_vec, sp[1]->s_vec, sp[2]->s_vec, sp[3]->s_vec,
             static_cast<t_int>(sp[0]->s_n));
}

static void* multipink_new (t_symbol*, int argc, t_atom*)
{
    auto* x = reinterpret_cast<t_multipink*>(pd_new (multipink_class));
    if (argc > 0)
        pd_error (x, "multipink~: takes no arguments, ignoring %d", argc);
    x->engine = new Seam::pd::MultipinkEngine;
    for (int k = 0; k < Seam::pd::MultipinkEngine::kChannels; ++k)
        outlet_new (&x->x_obj, &s_signal);
    return x;
}

static void multipink_free (t_multipink* x)
{
    delete x->engine;
}

extern "C" void multipink_tilde_setup (void)
{
    multipink_class = class_new (gensym ("multipink~"),
                                 reinterpret_cast<t_newmethod>(multipink_new),
                                 reinterpret_cast<t_method>(multipink_free),
                                 sizeof (t_multipink), CLASS_NOINLET, A_GIMME, 0);
    class_addmethod (multipink_class, reinterpret_cast<t_method>(multipink_dsp),
                     gensym ("dsp"), A_CANT, 0);
}
