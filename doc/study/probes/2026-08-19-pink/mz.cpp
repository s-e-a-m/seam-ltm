#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>
using namespace std;
static double fcOf(int n){ return 1000.0*pow(10.0,n/10.0); }
static double flOf(int n){ return fcOf(n)*pow(10.0,-0.05); }
static double fuOf(int n){ return fcOf(n)*pow(10.0, 0.05); }
struct Sec { double b0,b1,a1; };   // (b0 + b1 z^-1)/(1 + a1 z^-1)

// BILINEAR (Faust fi.spectral_tilt, verbatim)
static vector<Sec> designBLT(int N,double f0,double f1,double fs,double alpha=-0.5){
    const double T=1.0/fs, w0=2*M_PI*f0, r=pow(f1/f0,1.0/(N-1)), c=1.0/tan(0.5/fs);
    auto pw=[&](double w,double wp){ return wp*tan(w*T/2)/tan(wp*T/2); };
    vector<Sec> s;
    for(int i=0;i<N;++i){
        double mz=w0*pow(r,-alpha+i), mp=w0*pow(r,(double)i);
        double mzh=pw(mz,w0), mph=pw(mp,w0);
        if(mzh<=0||mph<=0) continue;
        double g=mph/mzh, d=mph+c;
        s.push_back({ g*(mzh+c)/d, g*(mzh-c)/d, (mph-c)/d });
    }
    return s;
}
// MATCHED-Z: analog pole -mp -> z=exp(-mp T); analog zero -mz -> z=exp(-mz T)
static vector<Sec> designMZ(int N,double f0,double f1,double fs,double alpha=-0.5){
    const double T=1.0/fs, w0=2*M_PI*f0, r=pow(f1/f0,1.0/(N-1));
    vector<Sec> s;
    for(int i=0;i<N;++i){
        double mz=w0*pow(r,-alpha+i), mp=w0*pow(r,(double)i);
        double zz=exp(-mz*T), zp=exp(-mp*T);
        double g=(1.0-zp)/(1.0-zz);              // unity gain at DC
        s.push_back({ g, -g*zz, -zp });
    }
    return s;
}
static double magDb(const vector<Sec>& s,double f,double fs){
    complex<double> z=exp(complex<double>(0,-2*M_PI*f/fs)), H(1,0);
    for(auto& k:s) H*=(k.b0+k.b1*z)/(1.0+k.a1*z);
    return 20*log10(abs(H));
}
static double bandDb(const vector<Sec>& s,int n,double fs,int steps=1024){
    double a=flOf(n),b=fuOf(n),sum=0;
    for(int k=0;k<steps;++k){ double f=a+(b-a)*(k+0.5)/steps,m=pow(10.0,magDb(s,f,fs)/20.0); sum+=m*m; }
    return 10*log10(sum*(b-a)/steps);
}
static void show(const char* tag,vector<Sec> s,double fs,bool full){
    vector<int> ns; double lim=0.85*0.5*fs;
    for(int n=-17;n<40;++n) if(fuOf(n)<=lim) ns.push_back(n);
    vector<double> lv; for(int n:ns) lv.push_back(bandDb(s,n,fs));
    double mean=0; for(double v:lv) mean+=v; mean/=lv.size();
    double w=0; for(double v:lv) w=max(w,fabs(v-mean));
    printf("%-28s fs=%-7.0f sezioni=%-3zu peggiore=%6.3f dB  %s\n",tag,fs,s.size(),w,w<=0.25?"PASS":"fail");
    if(full){ printf("   ");
        for(size_t i=0;i<ns.size();++i){ if(i%8==0&&i)printf("\n   "); printf("%7.0f:%+6.2f",fcOf(ns[i]),lv[i]-mean); }
        printf("\n"); }
}
int main(){
    const double FS[]={44100,48000,88200,96000,176400,192000};
    printf("== bilineare (Faust as-is), f0=10 Hz, f1=0.45 fs, 2 poli/ottava ==\n");
    for(double fs:FS){ double f1=0.45*fs,f0=10; int N=(int)ceil(2*log2(f1/f0))+1;
        show("BLT",designBLT(N,f0,f1,fs),fs,false); }
    printf("\n== matched-Z, stessi parametri ==\n");
    for(double fs:FS){ double f1=0.45*fs,f0=10; int N=(int)ceil(2*log2(f1/f0))+1;
        show("matched-Z",designMZ(N,f0,f1,fs),fs,false); }
    printf("\ncurva matched-Z a 48 kHz:\n");
    { double fs=48000,f1=0.45*fs,f0=10; int N=(int)ceil(2*log2(f1/f0))+1;
      show("matched-Z 48k",designMZ(N,f0,f1,fs),fs,true); }
    printf("\ncurva matched-Z a 96 kHz:\n");
    { double fs=96000,f1=0.45*fs,f0=10; int N=(int)ceil(2*log2(f1/f0))+1;
      show("matched-Z 96k",designMZ(N,f0,f1,fs),fs,true); }
    return 0;
}
