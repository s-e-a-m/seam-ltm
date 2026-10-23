//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · Pd · m2xhgr~ — mono → AmbiX (Haar), Pd glue
//
// [m2xhgr~ yaw pitch roll a1 a2 a3]
//   inlet:   mono signal; messages yaw/pitch/roll <deg>, a1/a2/a3 <dB>, print
//   outlets: W Y Z X (AmbiX, ACN order)
//
// Everything that computes lives in m2xhgr_engine.h; this file only talks
// to Pd. See README.md for build and install.
//─────────────────────────────────────────────────────────────────────────────

#include "m_pd.h"
#include "m2xhgr_engine.h"

#include <vector>

static_assert (sizeof (t_sample) == sizeof (float), "m2xhgr~ is built for 32-bit Pd");

static t_class* m2xhgr_class;

struct t_m2xhgr
{
    t_object x_obj;
    t_float  x_f;                          // CLASS_MAINSIGNALIN scalar
    Seam::pd::M2xhgrEngine* engine;
};

static t_int* m2xhgr_perform (t_int* w)
{
    auto* x = reinterpret_cast<t_m2xhgr*>(w[1]);
    const t_sample* in = reinterpret_cast<t_sample*>(w[2]);
    t_sample* out[4] = { reinterpret_cast<t_sample*>(w[3]), reinterpret_cast<t_sample*>(w[4]),
                         reinterpret_cast<t_sample*>(w[5]), reinterpret_cast<t_sample*>(w[6]) };
    x->engine->process (in, out, static_cast<int>(w[7]));
    return w + 8;
}

static void m2xhgr_dsp (t_m2xhgr* x, t_signal** sp)
{
    x->engine->prepare (sp[0]->s_sr);
    if (!x->engine->rateIsNominal ())
        post ("m2xhgr~: running at %g Hz — the Haar band mapping is tuned for 48000 Hz",
              sp[0]->s_sr);
    dsp_add (m2xhgr_perform, 7, x, sp[0]->s_vec,
             sp[1]->s_vec, sp[2]->s_vec, sp[3]->s_vec, sp[4]->s_vec,
             static_cast<t_int>(sp[0]->s_n));
}

static void m2xhgr_print (t_m2xhgr* x)
{
    post ("%s", x->engine->objectLine ().c_str ());
}

static void m2xhgr_anything (t_m2xhgr* x, t_symbol* s, int argc, t_atom* argv)
{
    if (argc < 1 || argv[0].a_type != A_FLOAT
        || !x->engine->set (s->s_name, atom_getfloat (argv)))
        pd_error (x, "m2xhgr~: no method for '%s'", s->s_name);
}

static void* m2xhgr_new (t_symbol*, int argc, t_atom* argv)
{
    auto* x = reinterpret_cast<t_m2xhgr*>(pd_new (m2xhgr_class));
    std::vector<double> args;
    for (int i = 0; i < argc; ++i) {
        if (argv[i].a_type == A_FLOAT)
            args.push_back (atom_getfloat (argv + i));
        else
            pd_error (x, "m2xhgr~: ignoring non-numeric argument '%s'",
                      atom_getsymbol (argv + i)->s_name);
    }
    x->x_f = 0;
    x->engine = new Seam::pd::M2xhgrEngine (args.data (), static_cast<int>(args.size ()));
    for (int k = 0; k < 4; ++k)
        outlet_new (&x->x_obj, &s_signal);
    return x;
}

static void m2xhgr_free (t_m2xhgr* x)
{
    delete x->engine;
}

extern "C" void m2xhgr_tilde_setup (void)
{
    m2xhgr_class = class_new (gensym ("m2xhgr~"),
                              reinterpret_cast<t_newmethod>(m2xhgr_new),
                              reinterpret_cast<t_method>(m2xhgr_free),
                              sizeof (t_m2xhgr), CLASS_DEFAULT, A_GIMME, 0);
    CLASS_MAINSIGNALIN (m2xhgr_class, t_m2xhgr, x_f);
    class_addmethod (m2xhgr_class, reinterpret_cast<t_method>(m2xhgr_dsp),
                     gensym ("dsp"), A_CANT, 0);
    class_addmethod (m2xhgr_class, reinterpret_cast<t_method>(m2xhgr_print),
                     gensym ("print"), A_NULL);
    class_addanything (m2xhgr_class, m2xhgr_anything);
}
