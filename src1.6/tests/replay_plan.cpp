#define main unused_snapshot_main
#include "parse_snapshot.cpp"
#undef main
int main(int argc,char**argv) {
    if(argc!=6) return 2;
    auto w=std::make_shared<RDFW>();
    char name[]="replay",path[]="-path",err[]="-err",ask[]="-ask_2",one[]="1";
    char* args[]={name,path,argv[1],err,one,ask,one};
    w->Init(7,args); w->stage=2;
    Replay().load(argv[4]);
    w->SetTestInput(read(argv[2]),read(argv[3]));
    w->Plan();
    snapshot(*w,"final");
    if(Replay().cursor!=Replay().requests.size()) return 7;
    std::cout<<"REPLAY COMPLETE "<<Replay().cursor<<'\n';
    w->Fini();
}
