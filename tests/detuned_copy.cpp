#include "engine.cpp"
#include "chien.cpp"
#include "drone_tone.cpp"
#include "strings.cpp"
#include "string_samples.cpp"
#include <cmath>
#include <cstdio>
#include <cstring>
int main() {
    using namespace hurdygurdy;
    const unsigned count=65536;
    static double signal[count], window[count];
    const double pi=3.14159265358979323846, rate=11025;
    const double freqs[]={55,110,215.33203125,440,880,1760};
    const i32 amounts[]={268435456,1073741824,2147483647};
    for(unsigned i=0;i<count;++i) window[i]=0.5-0.5*std::cos(2*pi*i/(count-1));
    double worst=0;
    for(double freq:freqs) for(i32 amount:amounts) {
        DetunedCopy state={};
        i32 input[16], output[16];
        const i32 ratio=lookup33(hg_twin_ratio,amount);
        double energy=0;
        for(unsigned block=0;block<512+count/16;++block) {
            for(unsigned i=0;i<16;++i) input[i]=static_cast<i32>(16000*std::sin(2*pi*freq*(block*16+i)/rate));
            const u32 period=static_cast<u32>(rate/freq*4096); const u32 periods[4]={period,period,period,period};
            detuned_copy(state,output,input,ratio,periods);
            for(unsigned i=0;i<16;++i) {
                if(output[i]<-16001 || output[i]>16001) return 2;
                if(block>=512) { double value=output[i]/32768.0; energy+=value*value; signal[(block-512)*16+i]=value*window[(block-512)*16+i]; }
            }
        }
        const double target=freq*2147483648.0/ratio;
        double best=-1,cents=0;
        for(int step=-24;step<=24;++step) {
            double coefficient=2*std::cos(2*pi*target*std::pow(2.0,step*0.25/1200)/rate), prev=0,older=0;
            for(unsigned i=0;i<count;++i) { double next=signal[i]+coefficient*prev-older; older=prev;prev=next; }
            double power=prev*prev+older*older-coefficient*prev*older;
            if(power>best) {best=power;cents=step*0.25;}
        }
        if(std::fabs(cents)>worst) worst=std::fabs(cents);
        std::printf("copy %.3f Hz amount %d error %.2f cents rms %.5f\n",freq,amount,cents,std::sqrt(energy/count));
        if(std::fabs(cents)>3 || energy/count<0.005) return 1;
    }
    std::printf("Detuned copy: worst %.2f cents\n",worst);
}
