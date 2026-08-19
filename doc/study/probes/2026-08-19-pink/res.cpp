#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>
using namespace std;
struct Sec { double b0,b1,a1; };
static vector<Sec> designMZ(int N,double f0,double f1,double fs,double alpha=-0.5){
    const double T=1.0/fs, w0=2*M_PI*f0, r=pow(f1/f0,1.0/(N-1));
    vector<Sec> s;
    for(int i=0;i<N;++i){
        double mz=w0*pow(r,-alpha+i), mp=w0*pow(r,(double)i);
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
int main(){
    printf("Errore di modulo contro pink ideale (riferito a f/fs = 0.01), per due fs lontane\n");
    printf("   f/fs      err@48k    err@192k    (dB)\n");
    for(double x=0.005; x<=0.46; x*=1.3){
        double e[2];
        double FS[2]={48000,192000};
        for(int i=0;i<2;++i){
            double fs=FS[i], f1=1.0*fs, f0=2.0;
            int N=(int)ceil(1.0*log2(f1/f0))+1;
            auto s=designMZ(N,f0,f1,fs);
            double ref=magDb(s,0.01*fs,fs) + 10*log10(0.01*fs);  // level + 3dB/oct removed
            e[i]=magDb(s,x*fs,fs) + 10*log10(x*fs) - ref;
        }
        printf("  %7.4f  %+9.3f  %+9.3f\n",x,e[0],e[1]);
    }
    return 0;
}
