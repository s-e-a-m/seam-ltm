// The lines of DDELAYProcessor::updateDelaySamples (ddelay_processor.cpp:143-160), verbatim in substance.
#include <cmath>
#include <cstdio>
#include <cstdlib>
static bool isPrime(int n){ if(n<2)return false; if(n<4)return true; if((n&1)==0)return false;
  int lim=(int)std::sqrt((double)n); for(int i=3;i<=lim;i+=2) if(n%i==0) return false; return true; }
static int nextPrime(int n){ if(n<2)return 2; int c=(n&1)?n+2:n+1; while(!isPrime(c)) c+=2; return c; }
int main(int argc,char**argv){ double sr=atof(argv[1]); long N=atol(argv[2]); double step=atof(argv[3]);
  FILE*f=fopen(argv[4],"wb"); const double kSpeedOfSound=331.4;
  for(long k=0;k<N;k++){ double distanceMeters_=(double)k*step;
    const double mm = std::round(distanceMeters_*1000.0)/1000.0;
    const double rawSamples = mm*sr/kSpeedOfSound;
    int n = static_cast<int>(std::lround(rawSamples));
    int d = (n<2)? n : nextPrime(n);
    double v=d; fwrite(&v,8,1,f);} fclose(f); }
