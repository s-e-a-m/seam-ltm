//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · Pd · lr2xhgr~ — stereo → AmbiX (Haar), Pd glue
//
// [lr2xhgr~ divergence yaw pitch roll a1 a2 a3]
//   inlets:  L signal (+ messages divergence/yaw/pitch/roll <deg>,
//            a1/a2/a3 <dB>, print), R signal
//   outlets: W Y Z X (AmbiX, ACN order)
//
// Everything that computes lives in lr2xhgr_engine.h; this file only talks
// to Pd. See README.md for build and install.
//─────────────────────────────────────────────────────────────────────────────

#include "m_pd.h"
#include "lr2xhgr_engine.h"

#include <cmath>
#include <vector>

static_assert (sizeof (t_sample) == sizeof (float), "lr2xhgr~ is built for 32-bit Pd");

static t_class* lr2xhgr_class;

struct t_lr2xhgr
{
    t_object x_obj;
    t_float  x_f;                          // CLASS_MAINSIGNALIN scalar
    Seam::pd::Lr2xhgrEngine* engine;
};

static t_int* lr2xhgr_perform (t_int* w)
{
    auto* x = reinterpret_cast<t_lr2xhgr*>(w[1]);
    const t_sample* inL = reinterpret_cast<t_sample*>(w[2]);
    const t_sample* inR = reinterpret_cast<t_sample*>(w[3]);
    t_sample* out[4] = { reinterpret_cast<t_sample*>(w[4]), reinterpret_cast<t_sample*>(w[5]),
                         reinterpret_cast<t_sample*>(w[6]), reinterpret_cast<t_sample*>(w[7]) };
    x->engine->process (inL, inR, out, static_cast<int>(w[8]));
    return w + 9;
}

static void lr2xhgr_dsp (t_lr2xhgr* x, t_signal** sp)
{
    x->engine->prepare (sp[0]->s_sr);
    if (!x->engine->rateIsNominal ())
        pd_error (x, "lr2xhgr~: running at %g Hz — the Haar band mapping is tuned for 48000 Hz",
                  sp[0]->s_sr);
    dsp_add (lr2xhgr_perform, 8, x, sp[0]->s_vec, sp[1]->s_vec,
             sp[2]->s_vec, sp[3]->s_vec, sp[4]->s_vec, sp[5]->s_vec,
             static_cast<t_int>(sp[0]->s_n));
}

static void lr2xhgr_print (t_lr2xhgr* x)
{
    post ("%s", x->engine->objectLine ().c_str ());
}

static void lr2xhgr_anything (t_lr2xhgr* x, t_symbol* s, int argc, t_atom* argv)
{
    if (argc < 1 || argv[0].a_type != A_FLOAT
        || !x->engine->set (s->s_name, atom_getfloat (argv)))
        pd_error (x, "lr2xhgr~: no method for '%s'", s->s_name);
}

static void* lr2xhgr_new (t_symbol*, int argc, t_atom* argv)
{
    auto* x = reinterpret_cast<t_lr2xhgr*>(pd_new (lr2xhgr_class));
    constexpr int np = Seam::pd::Lr2xhgrEngine::kNumParams;
    // A non-numeric argument stays in its slot as NaN (the engine uses the
    // default), so a typo never shifts the later values into other parameters.
    std::vector<double> args;
    for (int i = 0; i < argc && i < np; ++i)
        args.push_back (argv[i].a_type == A_FLOAT ? atom_getfloat (argv + i) : NAN);
    x->x_f = 0;
    x->engine = new Seam::pd::Lr2xhgrEngine (args.data (), static_cast<int>(args.size ()));
    for (int i = 0; i < argc && i < np; ++i)
        if (argv[i].a_type != A_FLOAT)
            pd_error (x, "lr2xhgr~: argument %d (%s) is not a number, using the default",
                      i + 1, x->engine->paramName (i));
    if (argc > np)
        pd_error (x, "lr2xhgr~: ignoring %d extra argument%s", argc - np, argc - np > 1 ? "s" : "");
    inlet_new (&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
    for (int k = 0; k < 4; ++k)
        outlet_new (&x->x_obj, &s_signal);
    return x;
}

static void lr2xhgr_free (t_lr2xhgr* x)
{
    delete x->engine;
}

extern "C" void lr2xhgr_tilde_setup (void)
{
    lr2xhgr_class = class_new (gensym ("lr2xhgr~"),
                               reinterpret_cast<t_newmethod>(lr2xhgr_new),
                               reinterpret_cast<t_method>(lr2xhgr_free),
                               sizeof (t_lr2xhgr), CLASS_DEFAULT, A_GIMME, 0);
    CLASS_MAINSIGNALIN (lr2xhgr_class, t_lr2xhgr, x_f);
    class_addmethod (lr2xhgr_class, reinterpret_cast<t_method>(lr2xhgr_dsp),
                     gensym ("dsp"), A_CANT, 0);
    class_addmethod (lr2xhgr_class, reinterpret_cast<t_method>(lr2xhgr_print),
                     gensym ("print"), A_NULL);
    class_addanything (lr2xhgr_class, lr2xhgr_anything);
}
