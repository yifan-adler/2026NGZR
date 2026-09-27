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
    void SetActionResults(const std::vector<bool>& results) {
        action_results_ = results;
        action_result_cursor_ = 0;
    }

protected:
    explicit Plug(const std::string& name) : name_(name) {}
    virtual void Plan() = 0;
    virtual void Fini() {}

    const std::string& GetName() const { return name_; }
    const std::string& GetTestName() const { return empty_; }
    const std::string& GetEnvDes() const { return env_; }
    const std::string& GetTaskDes() const { return task_; }

    virtual bool Move(unsigned int) { return NextActionResult(); }
    virtual bool PickUp(unsigned int) { return NextActionResult(); }
    virtual bool PutDown(unsigned int) { return NextActionResult(); }
    virtual bool ToPlate(unsigned int) { return NextActionResult(); }
    virtual bool FromPlate(unsigned int) { return NextActionResult(); }
    virtual bool Open(unsigned int) { return NextActionResult(); }
    virtual bool Close(unsigned int) { return NextActionResult(); }
    virtual bool PutIn(unsigned int, unsigned int) { return NextActionResult(); }
    virtual bool TakeOut(unsigned int, unsigned int) { return NextActionResult(); }
    virtual std::string AskLoc(unsigned int) { ++platform_calls_; return "not_known"; }
    virtual void Sense(std::vector<unsigned int>& values) { ++platform_calls_; values.clear(); }

private:
    bool NextActionResult() {
        ++platform_calls_;
        return action_result_cursor_ < action_results_.size() ?
            action_results_[action_result_cursor_++] : true;
    }
    std::string name_;
    std::string empty_;
    std::string env_, task_;
    unsigned int platform_calls_ = 0;
    std::vector<bool> action_results_;
    std::size_t action_result_cursor_ = 0;
};

} // namespace _home
