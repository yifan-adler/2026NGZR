#pragma once
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace _home {
struct ReplayState {
    std::vector<std::string> requests, replies;
    std::size_t cursor = 0;
    long elapsed = 0;
    void load(const std::string& path) {
        std::ifstream in(path); std::string line;
        while(std::getline(in,line)) {
            auto bar=line.find('|');
            requests.push_back(line.substr(0,bar)); replies.push_back(line.substr(bar+1));
        }
    }
    std::string act(const std::string& request) {
        if(cursor>=requests.size() || requests[cursor]!=request)
            throw std::runtime_error("replay mismatch at " + std::to_string(cursor) + ": " + request);
        std::cout << "REPLAY " << cursor << ' ' << request << '|' << replies[cursor] << '\n';
        const std::string reply=replies[cursor++];
        // Shared test clock: 80ms per observed call, then deadline at tape end.
        // This tests policy under identical observations/time, NOT runtime speed.
        elapsed=cursor==requests.size()?4700:static_cast<long>(cursor)*80;
        return reply;
    }
};
inline ReplayState& Replay() { static ReplayState state; return state; }
class Plug {
public:
    virtual ~Plug() {}
    void Run() { Plan(); }
    void SetTestInput(const std::string& env,const std::string& task) { env_=env; task_=task; }
protected:
    explicit Plug(const std::string& name):name_(name) {}
    virtual void Plan()=0;
    virtual void Fini() {}
    const std::string& GetName()const { return name_; }
    const std::string& GetTestName()const { return name_; }
    const std::string& GetEnvDes()const { return env_; }
    const std::string& GetTaskDes()const { return task_; }
    bool one(const char* name,unsigned int x) {
        return Replay().act(std::string(name)+" "+std::to_string(x))=="true";
    }
    virtual bool Move(unsigned int x){return one("Move",x);}
    virtual bool PickUp(unsigned int x){return one("PickUp",x);}
    virtual bool PutDown(unsigned int x){return one("PutDown",x);}
    virtual bool ToPlate(unsigned int x){return one("ToPlate",x);}
    virtual bool FromPlate(unsigned int x){return one("FromPlate",x);}
    virtual bool Open(unsigned int x){return one("Open",x);}
    virtual bool Close(unsigned int x){return one("Close",x);}
    virtual bool PutIn(unsigned int x,unsigned int y){return Replay().act("PutIn "+std::to_string(x)+" "+std::to_string(y))=="true";}
    virtual bool TakeOut(unsigned int x,unsigned int y){return Replay().act("TakeOut "+std::to_string(x)+" "+std::to_string(y))=="true";}
    virtual std::string AskLoc(unsigned int x){return Replay().act("AskLoc "+std::to_string(x));}
    virtual void Sense(std::vector<unsigned int>& values){
        values.clear(); std::istringstream in(Replay().act("Sense")); unsigned int x;
        while(in>>x) values.push_back(x);
    }
private:
    std::string name_,env_,task_;
};
}
