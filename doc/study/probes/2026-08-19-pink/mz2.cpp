#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>
using namespace std;
static double fcOf(int n){ return 1000.0*pow(10.0,n/10.0); }
static double flOf(int n){ return fcOf(n)*pow(10.0,-0.05); }
static double fuOf(int n){ return fcOf(n)*pow(10.0, 0.05); }
struct Sec { double b0,b1,a1; };
static vector<Sec> designMZ(int N,double f0,double f1,double fs,double alpha=-0.5){
    const double T=1.0/fs, w0=2*M_PI*f0, r=pow(f1/f0,1.0/(N-1));
    vector<Sec> s;
    for(int i=0;i<N;++i){
        double mz=w0*pow(r,-alpha+i), mp=w0*pow(r,(double)i);
        double zz=exp(-mz*T), zp=exp(-mp*T);
        double g=(1.0-zp)/(1.0-zz);
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
static double worst(const vector<Sec>& s,double fs){
    vector<int> ns; double lim=0.85*0.5*fs;
    for(int n=-17;n<40;++n) if(fuOf(n)<=lim) ns.push_back(n);
    vector<double> lv; for(int n:ns) lv.push_back(bandDb(s,n,fs));
    double mean=0; for(double v:lv) mean+=v; mean/=lv.size();
    double w=0; for(double v:lv) w=max(w,fabs(v-mean));
    return w;
}
int main(){
    const double FS[]={44100,48000,88200,96000,176400,192000};
    printf("matched-Z: guardia bassa f0, guardia alta f1 = k * fs (poli oltre Nyquist ammessi)\n");
    printf("  f0     f1/fs  ppo |");
    for(double f:FS) printf("%8.0f",f);
    printf(" |  N     esito\n");
    for(double f0 : {10.0,5.0,2.0,1.0})
    for(double k  : {0.45,0.5,1.0,2.0,4.0})
    for(double ppo: {1.0,2.0}){
        double mx=0; int nmin=999,nmax=0; char buf[512]; int p=0;
        for(double fs:FS){
            double f1=k*fs;
            int N=(int)ceil(ppo*log2(f1/f0))+1;
            double w=worst(designMZ(N,f0,f1,fs),fs);
            mx=max(mx,w); nmin=min(nmin,N); nmax=max(nmax,N);
            p+=snprintf(buf+p,sizeof(buf)-p,"%8.3f",w);
        }
        printf("%5.1f%9.2f%5.1f |%s | %2d..%2d %s\n",f0,k,ppo,buf,nmin,nmax,mx<=0.25?"<== PASS":"");
    }
    return 0;
}
