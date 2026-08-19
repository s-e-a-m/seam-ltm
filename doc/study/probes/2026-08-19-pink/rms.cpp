#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>
using namespace std;
struct Sec { double b0,b1,a1; };
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
    { double zz=kCorrZero, zp=kCorrPole, g=(1.0-zp)/(1.0-zz); s.push_back({g,-g*zz,-zp}); }
    return s;
}
static double mag(const vector<Sec>& s,double f,double fs){
    complex<double> z=exp(complex<double>(0,-2*M_PI*f/fs)),H(1,0);
    for(auto&k:s) H*=(k.b0+k.b1*z)/(1.0+k.a1*z);
    return abs(H);
}
// old filter
static double oldmag(double f,double fs){
    static const double B[4]={0.049922035,-0.095993537,0.050612699,-0.004408786};
    static const double A[3]={-2.494956002,2.017265875,-0.522189400};
    complex<double> z=exp(complex<double>(0,-2*M_PI*f/fs)),z2=z*z,z3=z2*z;
    return abs((B[0]+B[1]*z+B[2]*z2+B[3]*z3)/(1.0+A[0]*z+A[1]*z2+A[2]*z3));
}
int main(){
    printf("Guadagno RMS su rumore bianco (dB), e livello a 1 kHz\n");
    printf("   fs      nuovo: RMS   @1kHz   RMS-@1k  |  vecchio: RMS   @1kHz\n");
    const double FS[]={44100,48000,88200,96000,176400,192000};
    for(double fs:FS){
        auto s=design(fs,2.0,1.0);
        const int K=200000; double sum=0, sumo=0;
        for(int k=0;k<K;++k){ double f=(k+0.5)*(fs/2)/K; double m=mag(s,f,fs); sum+=m*m;
                              double mo=oldmag(f,fs); sumo+=mo*mo; }
        double rms=10*log10(sum/K), k1=20*log10(mag(s,1000.0,fs));
        double rmso=10*log10(sumo/K), k1o=20*log10(oldmag(1000.0,fs));
        printf("%8.0f   %9.3f %8.3f %8.3f  |  %9.3f %8.3f\n",fs,rms,k1,rms-k1,rmso,k1o);
    }
    return 0;
}
