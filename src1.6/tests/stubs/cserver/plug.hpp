#pragma once

#include <string>
#include <vector>

namespace _home {

// Header-only platform stub used only by local unit/syntax tests.  Production
// builds put the competition SDK include directory before this test directory.
class Plug {
public:
    virtual ~Plug() {}
    void Run() { Plan(); Fini(); }
    void SetTestInput(const std::string& env, const std::string& task) {
        env_ = env;
        task_ = task;
        platform_calls_ = 0;
    }
    unsigned int TestPlatformCalls() const { return platform_calls_; }

protected:
    explicit Plug(const std::string& name) : name_(name) {}
    virtual void Plan() = 0;
    virtual void Fini() {}

    const std::string& GetName() const { return name_; }
    const std::string& GetTestName() const { return empty_; }
    const std::string& GetEnvDes() const { return env_; }
    const std::string& GetTaskDes() const { return task_; }

    virtual bool Move(unsigned int) { ++platform_calls_; return true; }
    virtual bool PickUp(unsigned int) { ++platform_calls_; return true; }
    virtual bool PutDown(unsigned int) { ++platform_calls_; return true; }
    virtual bool ToPlate(unsigned int) { ++platform_calls_; return true; }
    virtual bool FromPlate(unsigned int) { ++platform_calls_; return true; }
    virtual bool Open(unsigned int) { ++platform_calls_; return true; }
    virtual bool Close(unsigned int) { ++platform_calls_; return true; }
    virtual bool PutIn(unsigned int, unsigned int) { ++platform_calls_; return true; }
    virtual bool TakeOut(unsigned int, unsigned int) { ++platform_calls_; return true; }
    virtual std::string AskLoc(unsigned int) { ++platform_calls_; return "not_known"; }
    virtual void Sense(std::vector<unsigned int>& values) { ++platform_calls_; values.clear(); }

private:
    std::string name_;
    std::string empty_;
    std::string env_, task_;
    unsigned int platform_calls_ = 0;
};

} // namespace _home
