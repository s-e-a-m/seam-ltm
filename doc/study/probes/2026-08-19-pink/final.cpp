#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>
using namespace std;
static double fcOf(int n){ return 1000.0*pow(10.0,n/10.0); }
static double flOf(int n){ return fcOf(n)*pow(10.0,-0.05); }
static double fuOf(int n){ return fcOf(n)*pow(10.0, 0.05); }
struct Sec { double b0,b1,a1; };
// universal alias-tail correction, fitted once in normalised frequency
static const double kCorrZero=-0.250775213, kCorrPole=-0.160124183;
static vector<Sec> design(double fs,double f0,double ppo,double alpha=-0.5){
    const double T=1.0/fs, f1=0.5*fs, w0=2*M_PI*f0;
    int N=(int)ceil(ppo*log2(f1/f0))+1;
    double r=pow(f1/f0,1.0/(N-1));
    vector<Sec> s;
    for(int i=0;i<N;++i){
        double mz=w0*pow(r,-alpha+i), mp=w0*pow(r,(double)i);
        double zz=exp(-mz*T), zp=exp(-mp*T), g=(1.0-zp)/(1.0-zz);
        s.push_back({ g,-g*zz,-zp });
    }
    { double zz=kCorrZero, zp=kCorrPole, g=(1.0-zp)/(1.0-zz);
      s.push_back({ g,-g*zz,-zp }); }
    return s;
}
static double magDb(const vector<Sec>& s,double f,double fs){
    complex<double> z=exp(complex<double>(0,-2*M_PI*f/fs)),H(1,0);
    for(auto&k:s) H*=(k.b0+k.b1*z)/(1.0+k.a1*z);
    return 20*log10(abs(H));
}
static double bandDb(const vector<Sec>& s,int n,double fs,int steps=8192){
    double a=flOf(n),b=fuOf(n),sum=0;
    for(int k=0;k<steps;++k){ double f=a+(b-a)*(k+0.5)/steps,m=pow(10.0,magDb(s,f,fs)/20.0); sum+=m*m; }
    return 10*log10(sum*(b-a)/steps);
}
int main(){
    const double FS[]={44100,48000,88200,96000,176400,192000};
    for(double ppo : {1.0,1.5,2.0}){
        printf("=== %.1f poli/ottava, f0 = 2 Hz, correzione fissa ===\n",ppo);
        printf("   fs      sezioni   banda giudicata     peggiore   esito\n");
        for(double fs:FS){
            auto s=design(fs,2.0,ppo);
            vector<int> ns; double lim=0.85*0.5*fs;
            for(int n=-17;n<40;++n) if(fuOf(n)<=lim) ns.push_back(n);
            vector<double> lv; for(int n:ns) lv.push_back(bandDb(s,n,fs));
            double mean=0; for(double v:lv)mean+=v; mean/=lv.size();
            double w=0; int wn=0;
            for(size_t i=0;i<lv.size();++i){double d=fabs(lv[i]-mean); if(d>w){w=d;wn=ns[i];}}
            printf("%8.0f  %6zu   20 Hz - %8.0f Hz  %7.3f dB  %s (peggiore a %.0f Hz)\n",
                   fs,s.size(),fcOf(ns.back()),w,w<=0.25?"PASS":"FAIL",fcOf(wn));
        }
        printf("\n");
    }
    // full table for the recommended setting
    printf("=== tabella completa, 1.5 poli/ottava, fs = 96 kHz ===\n");
    { double fs=96000; auto s=design(fs,2.0,1.5);
      vector<int> ns; double lim=0.85*0.5*fs;
      for(int n=-17;n<40;++n) if(fuOf(n)<=lim) ns.push_back(n);
      vector<double> lv; for(int n:ns) lv.push_back(bandDb(s,n,fs));
      double mean=0; for(double v:lv)mean+=v; mean/=lv.size();
      for(size_t i=0;i<ns.size();++i){ if(i%8==0&&i)printf("\n"); printf("%8.0f:%+6.3f",fcOf(ns[i]),lv[i]-mean); }
      printf("\n"); }
    return 0;
}
