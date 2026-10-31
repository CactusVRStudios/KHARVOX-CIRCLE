#include "performance_stats.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
void require(bool ok){if(!ok){std::cerr<<"Performance counter failure\n";std::exit(1);}}
bool near(double a,double b){return std::abs(a-b)<0.001;}
int main(){
    circle::motor::PerformanceStats s;s.reset(0,0);
    for(unsigned i=1;i<=90;++i){auto id=(i*2+2)/3;s.sample(i/90.0,id,true,true,id);}
    require(s.rates.valid&&near(s.rates.produced,60)&&near(s.rates.fresh,60));
    require(near(s.rates.submissions,90)&&near(s.rates.repeats,100.0/3));
    // Repeating a frozen image must show zero fresh FPS and growing age.
    for(unsigned i=1;i<=90;++i)s.sample(1+i/90.0,60,true,true,60);
    require(near(s.rates.fresh,0)&&near(s.rates.repeats,100)&&s.imageAgeMs(2)>=1000);
    // Failed submissions and loading-card/overlay-only frames do not count.
    s.reset(0,100);s.sample(.2,110,false,true,1);s.sample(.5,120,true,false,2);
    s.sample(1,130,false,false,0);
    require(near(s.rates.produced,30)&&near(s.rates.fresh,0)&&near(s.rates.submissions,0));
    // A dropped rendered pair must not inflate fresh submitted FPS.
    s.reset(0,0);s.sample(.3,3,true,true,3);s.sample(1,10,true,true,10);
    require(near(s.rates.produced,10)&&near(s.rates.fresh,2));
    // Empty frames do not make the same resumed image fresh again.
    s.sample(1.2,10,true,false,0);s.sample(2,10,true,true,10);
    require(near(s.rates.fresh,0)&&near(s.rates.repeats,100));
    // Session/transport rebuilds reset identity and never underflow totals.
    s.reset(3,10);s.sample(4,12,true,true,1);require(near(s.rates.fresh,1));
    s.sample(5,0,true,true,1);require(!s.rates.valid);
    // 45 real + 45 generated: output must be 90, real source remains 45.
    s.reset(0,0);
    for(unsigned i=1;i<=90;++i)s.sample(i/90.0,(i+1)/2,true,true,i,i%2==0);
    require(near(s.rates.output,90)&&near(s.rates.fresh,45)&&near(s.rates.produced,45));
    // Repeated FG, failed and empty frames cannot inflate output.
    s.sample(1.2,45,true,true,90,true);
    s.sample(1.4,45,false,true,91,true);
    s.sample(1.6,45,true,false,92,true);
    s.sample(2,45,true,false,0);
    require(near(s.rates.output,0)&&near(s.rates.fresh,0));
    std::cout<<"Performance cadence, repeats, stalls, drops and reset PASS\n";
}
