// Probe (throwaway): fi.spectral_tilt design swept against the SMPTE-extended
// acceptance criterion. Faithful to faustlibraries/filters.lib:3359.
#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>
using namespace std;

static double fcOf(int n){ return 1000.0*pow(10.0, n/10.0); }
static double flOf(int n){ return fcOf(n)*pow(10.0,-0.05); }
static double fuOf(int n){ return fcOf(n)*pow(10.0, 0.05); }

static vector<int> bandsFor(double fs){
    vector<int> v; double lim = 0.85*0.5*fs;
    for(int n=-17;n<40;++n) if(fuOf(n)<=lim) v.push_back(n);
    return v;
}

struct Sec { double b0,b1,a1; };

static vector<Sec> design(int N,double f0,double bw,double fs,double alpha=-0.5){
    const double T=1.0/fs, w0=2*M_PI*f0, f1=f0+bw;
    const double r=pow(f1/f0, 1.0/(N-1));
    const double c=1.0/tan(0.5/fs);                    // tf1s(...,w1=1)
    auto pw=[&](double w,double wp){ return wp*tan(w*T/2)/tan(wp*T/2); };
    vector<Sec> s;
    for(int i=0;i<N;++i){
        double mz=w0*pow(r,-alpha+i), mp=w0*pow(r,(double)i);
        double mzh=pw(mz,w0), mph=pw(mp,w0);
        double g=mph/mzh, d=mph+c;
        s.push_back({ g*(mzh+c)/d, g*(mzh-c)/d, (mph-c)/d });
    }
    return s;
}

static double magDb(const vector<Sec>& s,double f,double fs){
    complex<double> z=exp(complex<double>(0,-2*M_PI*f/fs)), H(1,0);
    for(auto& k:s) H *= (k.b0 + k.b1*z)/(1.0 + k.a1*z);
    return 20*log10(abs(H));
}

static double bandDb(const vector<Sec>& s,int n,double fs,int steps){
    double a=flOf(n), b=fuOf(n), sum=0;
    for(int k=0;k<steps;++k){
        double f=a+(b-a)*(k+0.5)/steps, m=pow(10.0, magDb(s,f,fs)/20.0);
        sum+=m*m;
    }
    return 10*log10(sum*(b-a)/steps);
}

// worst deviation from the mean over the judged bands
static double worst(const vector<Sec>& s,double fs,int steps,int* atN=nullptr){
    auto ns=bandsFor(fs); vector<double> lv;
    for(int n:ns) lv.push_back(bandDb(s,n,fs,steps));
    double mean=0; for(double v:lv) mean+=v; mean/=lv.size();
    double w=0; int wi=0;
    for(size_t i=0;i<lv.size();++i){ double d=fabs(lv[i]-mean); if(d>w){w=d;wi=(int)i;} }
    if(atN)*atN=ns[wi];
    return w;
}

struct Rule { int N; double f0,bw; };
static Rule rule(double fs,double gl,double gh,double ppo){
    double f0=20.0/pow(2.0,gl);
    double ftop=fuOf(bandsFor(fs).back());
    double f1=min(ftop*pow(2.0,gh), 0.45*fs);
    int N=(int)ceil(ppo*log2(f1/f0))+1;
    return {N,f0,f1-f0};
}

int main(){
    const double FS[]={44100,48000,88200,96000,176400,192000};
    printf("  gl  gh  ppo |");
    for(double f:FS) printf("%9.0f",f);
    printf(" |  N\n");
    for(double gl:{1.0,2.0,3.0})
    for(double gh:{0.5,1.0,2.0})
    for(double ppo:{1.0,1.5,2.0,3.0}){
        double mx=0; int nmin=999,nmax=0; char buf[512]; int p=0;
        for(double fs:FS){
            Rule r=rule(fs,gl,gh,ppo);
            double w=worst(design(r.N,r.f0,r.bw,fs),fs,512);
            mx=max(mx,w); nmin=min(nmin,r.N); nmax=max(nmax,r.N);
            p+=snprintf(buf+p,sizeof(buf)-p,"%9.3f",w);
        }
        printf("%4.1f%4.1f%5.1f |%s | %2d..%2d%s\n",gl,gh,ppo,buf,nmin,nmax,
               mx<=0.25?"  <== PASS":"");
    }
    return 0;
}
