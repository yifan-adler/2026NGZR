// Compiled against BOTH versions, with identical stub and input bytes.
#include "rdfw.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
using namespace _home;
std::string read(const char* p) { std::ifstream f(p); std::ostringstream s; s<<f.rdbuf(); return s.str(); }
void snapshot(RDFW& w, const char* phase) {
    std::cout << "SNAP " << phase << " robot=" << w.location << ',' << w.hold_id << ',' << w.plate_id << '\n';
    for(auto o:w.objects) {
        std::cout<<"SNAP object "<<o->id<<' '<<o->sort<<' '<<o->location;
        auto s=std::dynamic_pointer_cast<SmallObject>(o);
        auto c=std::dynamic_pointer_cast<Container>(o);
        auto b=std::dynamic_pointer_cast<BigObject>(o);
        if(s) std::cout<<" small "<<s->color<<' '<<s->inside<<' '<<s->on;
        else if(c) std::cout<<" container "<<c->isOpen;
        else if(b) std::cout<<" big";
        std::cout<<'\n';
    }
    auto list=[&](const char* name,const std::vector<Instruction>& ins) {
        for(std::size_t i=0;i<ins.size();++i) {
            const auto& t=ins[i];
            std::cout<<"SNAP "<<name<<' '<<i<<' '<<t.behave<<" X=";
            for(auto o:t.X) std::cout<<o->id<<',';
            std::cout<<" Y=";
            for(auto o:t.Y) std::cout<<o->id<<',';
            std::cout<<" CX="<<t.conditionX.sort<<'/'<<t.conditionX.color
                <<" CY="<<t.conditionY.sort<<'/'<<t.conditionY.color
                <<" useY="<<t.isUseY<<" missing="<<t.hasMissingObjects<<'\n';
        }
    };
    list("task",w.tasks); list("info",w.infos); list("not_task",w.not_taskConstrains);
    list("not_info",w.not_infoConstrains); list("must_info",w.notnot_infoConstrains);
    std::cout<<"SNAP END "<<phase<<'\n';
}
int main(int argc,char**argv) {
    if(argc!=6) return 2;
    auto w=std::make_shared<RDFW>();
    char name[]="snapshot",path[]="-path";
    char* args[]={name,path,argv[1]};
    w->Init(3,args); w->stage=std::atoi(argv[4]);
    if(!w->ParseEnv(read(argv[2]))) return 3;
    if(std::string(argv[5])=="nt") w->ParseNaturalLanguage(read(argv[3]));
    else if(!w->ParseInstruction(read(argv[3]))) return 4;
    snapshot(*w,"parsed");
#ifdef FIXED_VERSION
    if(!w->RunQuestionPreflight()) return 5;
#endif
    snapshot(*w,"preflight");
    for(const auto& info:w->infos) w->ParseInfo(info);
    if(w->stage==2) { w->ApplyOpenCloseCorrection(); w->ApplyMustInConstraintCorrection(); w->ApplyMustNearConstraintCorrection(); }
    snapshot(*w,"corrected");
    w->Fini();
}
