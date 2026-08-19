#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>
using namespace std;
static double fcOf(int n){ return 1000.0*pow(10.0,n/10.0); }
static double flOf(int n){ return fcOf(n)*pow(10.0,-0.05); }
static double fuOf(int n){ return fcOf(n)*pow(10.0, 0.05); }
struct Sec { double b0,b1,a1; };

// ladder: poles geometric f0..f1; zero i sits e[i] log-steps above pole i
static vector<Sec> build(const vector<double>& fp,const vector<double>& e,double r,double fs){
    const double T=1.0/fs; vector<Sec> s;
    for(size_t i=0;i<fp.size();++i){
        double mp=2*M_PI*fp[i], mz=mp*pow(r,e[i]);
        double zz=exp(-mz*T), zp=exp(-mp*T), g=(1.0-zp)/(1.0-zz);
        s.push_back({ g, -g*zz, -zp });
    }
    return s;
}
static double magDb(const vector<Sec>& s,double f,double fs){
    complex<double> z=exp(complex<double>(0,-2*M_PI*f/fs)), H(1,0);
    for(auto& k:s) H*=(k.b0+k.b1*z)/(1.0+k.a1*z);
    return 20*log10(abs(H));
}
static double bandDb(const vector<Sec>& s,int n,double fs,int steps=512){
    double a=flOf(n),b=fuOf(n),sum=0;
    for(int k=0;k<steps;++k){ double f=a+(b-a)*(k+0.5)/steps,m=pow(10.0,magDb(s,f,fs)/20.0); sum+=m*m; }
    return 10*log10(sum*(b-a)/steps);
}
static double worstBand(const vector<Sec>& s,double fs,int* atn=nullptr){
    vector<int> ns; double lim=0.85*0.5*fs;
    for(int n=-17;n<40;++n) if(fuOf(n)<=lim) ns.push_back(n);
    vector<double> lv; for(int n:ns) lv.push_back(bandDb(s,n,fs));
    double mean=0; for(double v:lv)mean+=v; mean/=lv.size();
    double w=0; for(size_t i=0;i<lv.size();++i){ double d=fabs(lv[i]-mean); if(d>w){w=d; if(atn)*atn=ns[i];} }
    return w;
}
int main(){
    const double FS[]={44100,48000,88200,96000,176400,192000};
    const double f0=2.0, ppo=1.5;
    printf("Raffinamento iterativo degli zeri (matched-Z, f0=%.0f Hz, %.1f poli/ottava)\n\n",f0,ppo);
    printf("   fs      sezioni   prima    dopo   banda peggiore   esito\n");
    for(double fs:FS){
        double f1=0.5*fs;
        int N=(int)ceil(ppo*log2(f1/f0))+1;
        double r=pow(f1/f0,1.0/(N-1));
        vector<double> fp(N), e(N,0.5);
        for(int i=0;i<N;++i) fp[i]=f0*pow(r,i);
        double before=worstBand(build(fp,e,r,fs),fs);
        // error grid: 20 Hz .. 0.425 fs, log spaced
        vector<double> gf;
        for(double f=20; f<=0.425*fs; f*=1.03) gf.push_back(f);
        auto maxerr=[&](const vector<double>& ee){
            auto s=build(fp,ee,r,fs);
            double ref=magDb(s,gf[0],fs)+10*log10(gf[0]);
            double w=0; for(double f:gf) w=max(w,fabs(magDb(s,f,fs)+10*log10(f)-ref));
            return w;
        };
        double best=maxerr(e), step=0.25;
        for(int pass=0; pass<60 && step>1e-6; ++pass){
            bool imp=false;
            for(int k=0;k<N;++k) for(int sg=-1;sg<=1;sg+=2){
                vector<double> t=e; t[k]+=sg*step;
                if(t[k]<0.0||t[k]>2.0) continue;
                double c=maxerr(t);
                if(c<best-1e-9){ best=c; e=t; imp=true; }
            }
            if(!imp) step*=0.5;
        }
        int an=0; double after=worstBand(build(fp,e,r,fs),fs,&an);
        printf("%8.0f  %6d   %6.3f  %6.3f   %8.0f Hz     %s\n",fs,N,before,after,fcOf(an),after<=0.25?"PASS":"fail");
    }
    return 0;
}
