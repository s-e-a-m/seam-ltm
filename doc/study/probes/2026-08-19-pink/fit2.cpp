#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>
using namespace std;
struct Sec { double b0,b1,a1; };
static vector<Sec> designMZ(int N,double f0,double f1,double fs,double alpha=-0.5){
    const double T=1.0/fs, w0=2*M_PI*f0, r=pow(f1/f0,1.0/(N-1));
    vector<Sec> s;
    for(int i=0;i<N;++i){
        double mz=w0*pow(r,-alpha+i), mp=w0*pow(r,(double)i);
        double zz=exp(-mz*T), zp=exp(-mp*T), g=(1.0-zp)/(1.0-zz);
        s.push_back({ g,-g*zz,-zp });
    }
    return s;
}
static double magDb(const vector<Sec>& s,double f,double fs){
    complex<double> z=exp(complex<double>(0,-2*M_PI*f/fs)),H(1,0);
    for(auto&k:s) H*=(k.b0+k.b1*z)/(1.0+k.a1*z);
    return 20*log10(abs(H));
}
static vector<double> XS,ERR;
// params: M pairs (zero, pole), real, |.|<1 ; unity DC gain
static double corrDb(const double* p,int M,double x){
    complex<double> z=exp(complex<double>(0,-2*M_PI*x)),H(1,0);
    for(int i=0;i<M;++i){
        double zz=tanh(p[2*i]), zp=tanh(p[2*i+1]);
        H *= ((1.0-zp)/(1.0-zz))*(1.0-zz*z)/(1.0-zp*z);
    }
    return 20*log10(abs(H));
}
static double cost(const double* p,int M){
    double w=0;
    for(size_t i=0;i<XS.size();++i) w=max(w,fabs(ERR[i]+corrDb(p,M,XS[i])));
    return w;
}
// Nelder-Mead
static double nelder(vector<double>& x0,int M,int iters=20000){
    int n=x0.size();
    vector<vector<double>> S(n+1,x0); vector<double> F(n+1);
    for(int i=0;i<n;++i) S[i+1][i]+=0.5;
    for(int i=0;i<=n;++i) F[i]=cost(S[i].data(),M);
    for(int it=0;it<iters;++it){
        vector<int> idx(n+1); for(int i=0;i<=n;++i) idx[i]=i;
        sort(idx.begin(),idx.end(),[&](int a,int b){return F[a]<F[b];});
        vector<vector<double>> S2; vector<double> F2;
        for(int i:idx){ S2.push_back(S[i]); F2.push_back(F[i]); }
        S=S2; F=F2;
        if(F[n]-F[0]<1e-12) break;
        vector<double> c(n,0);
        for(int i=0;i<n;++i) for(int j=0;j<n;++j) c[j]+=S[i][j]/n;
        auto tryPt=[&](double a){ vector<double> p(n); for(int j=0;j<n;++j) p[j]=c[j]+a*(c[j]-S[n][j]); return p; };
        vector<double> xr=tryPt(1.0); double fr=cost(xr.data(),M);
        if(fr<F[0]){ vector<double> xe=tryPt(2.0); double fe=cost(xe.data(),M);
            if(fe<fr){S[n]=xe;F[n]=fe;} else {S[n]=xr;F[n]=fr;} }
        else if(fr<F[n-1]){ S[n]=xr;F[n]=fr; }
        else { vector<double> xc=tryPt(-0.5); double fc=cost(xc.data(),M);
            if(fc<F[n]){S[n]=xc;F[n]=fc;}
            else { for(int i=1;i<=n;++i){ for(int j=0;j<n;++j) S[i][j]=S[0][j]+0.5*(S[i][j]-S[0][j]); F[i]=cost(S[i].data(),M);} } }
    }
    int bi=0; for(int i=0;i<=n;++i) if(F[i]<F[bi]) bi=i;
    x0=S[bi]; return F[bi];
}
int main(){
    double fs=48000,f0=2.0,f1=0.5*fs;
    int N=(int)ceil(1.5*log2(f1/f0))+1;
    auto lad=designMZ(N,f0,f1,fs);
    double ref=magDb(lad,0.0005*fs,fs)+10*log10(0.0005*fs);
    for(double x=0.0005;x<=0.4255;x*=1.015){ XS.push_back(x); ERR.push_back(magDb(lad,x*fs,fs)+10*log10(x*fs)-ref); }
    printf("residuo del ladder: max %+.3f dB su %zu punti (fino a x=0.425)\n\n",ERR.back(),XS.size());
    srand(12345);
    for(int M=1;M<=3;++M){
        double best=1e9; vector<double> bp;
        for(int start=0;start<40;++start){
            vector<double> p(2*M);
            for(int i=0;i<2*M;++i) p[i]=((double)rand()/RAND_MAX)*3.0-1.0;
            double c=nelder(p,M);
            if(c<best){best=c;bp=p;}
        }
        printf("M=%d  errore max dopo correzione: %.4f dB\n",M,best);
        for(int i=0;i<M;++i) printf("     zero=%+.9f  polo=%+.9f\n",tanh(bp[2*i]),tanh(bp[2*i+1]));
    }
    return 0;
}
