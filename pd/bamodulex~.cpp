//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · Pd · bamodulex~ — AmbiX → tetrahedral, Pd glue
//
// [bamodulex~]
//   inlets:  W Y Z X (AmbiX, ACN order); print on the left inlet
//   outlets: LFU RFD RBU LBD (upright tetrahedron)
//
// The matrix lives in bamodulex_engine.h; this file only talks to Pd.
// See README.md for build and install.
//─────────────────────────────────────────────────────────────────────────────

#include "m_pd.h"
#include "bamodulex_engine.h"

static_assert (sizeof (t_sample) == sizeof (float), "bamodulex~ is built for 32-bit Pd");

static t_class* bamodulex_class;

struct t_bamodulex
{
    t_object x_obj;
    t_float  x_f;                          // CLASS_MAINSIGNALIN scalar
};

static t_int* bamodulex_perform (t_int* w)
{
    const t_sample* in[4] = { reinterpret_cast<t_sample*>(w[1]), reinterpret_cast<t_sample*>(w[2]),
                              reinterpret_cast<t_sample*>(w[3]), reinterpret_cast<t_sample*>(w[4]) };
    t_sample* out[4] = { reinterpret_cast<t_sample*>(w[5]), reinterpret_cast<t_sample*>(w[6]),
                         reinterpret_cast<t_sample*>(w[7]), reinterpret_cast<t_sample*>(w[8]) };
    Seam::pd::bamodulexBlock (in, out, static_cast<int>(w[9]));
    return w + 10;
}

static void bamodulex_dsp (t_bamodulex*, t_signal** sp)
{
    dsp_add (bamodulex_perform, 9,
             sp[0]->s_vec, sp[1]->s_vec, sp[2]->s_vec, sp[3]->s_vec,
             sp[4]->s_vec, sp[5]->s_vec, sp[6]->s_vec, sp[7]->s_vec,
             static_cast<t_int>(sp[0]->s_n));
}

static void bamodulex_print (t_bamodulex*)
{
    post ("bamodulex~");
}

static void* bamodulex_new (t_symbol*, int argc, t_atom*)
{
    auto* x = reinterpret_cast<t_bamodulex*>(pd_new (bamodulex_class));
    if (argc > 0)
        pd_error (x, "bamodulex~: takes no arguments, ignoring %d", argc);
    x->x_f = 0;
    for (int k = 1; k < 4; ++k)
        inlet_new (&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
    for (int k = 0; k < 4; ++k)
        outlet_new (&x->x_obj, &s_signal);
    return x;
}

extern "C" void bamodulex_tilde_setup (void)
{
    bamodulex_class = class_new (gensym ("bamodulex~"),
                                 reinterpret_cast<t_newmethod>(bamodulex_new), nullptr,
                                 sizeof (t_bamodulex), CLASS_DEFAULT, A_GIMME, 0);
    CLASS_MAINSIGNALIN (bamodulex_class, t_bamodulex, x_f);
    class_addmethod (bamodulex_class, reinterpret_cast<t_method>(bamodulex_dsp),
                     gensym ("dsp"), A_CANT, 0);
    class_addmethod (bamodulex_class, reinterpret_cast<t_method>(bamodulex_print),
                     gensym ("print"), A_NULL);
}
