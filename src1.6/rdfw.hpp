/*
 * File: rdfw.hpp
 * Author : ShiQiao Chen(陈世侨)
 * Affiliation: WuHan University of Technology
 */
#pragma once

#include "cserver/plug.hpp"
#include "string"
#include "unordered_map"
#include "map"
#include "vector"
#include "functional"
#include "memory"
#include "stdexcept"
#include "typeinfo"
#include "debuglog.hpp"
#include "deadline_manager.hpp"
#include "terminal_checker.hpp"
#include "score_evaluator.hpp"
#include "candidate_plan.hpp"
#include "question_preflight.hpp"
using namespace std;

class parser;
namespace _home
{
    static const int UNKNOWN = -1;
    static const int NONE = 0;
    // The platform uses compact numeric identifiers.  Hard bounds keep a
    // malformed/sparse input from turning an id into an unbounded allocation.
    static const unsigned int MAX_OBJECT_ID = 255;
    static const int MAX_LOCATION_ID = 4095;
    static const std::size_t MAX_INPUT_BYTES = 1024 * 1024;

    // Source of the current fact, separate from whether it is reliable enough
    // to finish a Stage 2 task. Constraint heuristic votes remain weak.
    enum class EvidenceSource {
        UNKNOWN, INITIAL, EXPLICIT_INFO, CONSTRAINT_DERIVED,
        CONSTRAINT_HEURISTIC, SENSE, ACTION_SUCCESS, ACTION_FAILURE, ASK_ANSWER
    };

    // 这里先放一些数据结构，方便理解整个架构
    // under _home namespace

    ///////////////////////////////////////////////////////////////////////
    // Instruction, tasks, cons
    class Instruction;


    ///////////////////////////////////////////////////////////////////////
    // Instruction, tasks, cons
    class Object;
    class SmallObject;
    class BigObject;
    class Container;
    class Robot;


    ///////////////////////////////////////////////////////////////////////
    // Others
    struct SyntaxNode;
    struct Condition;


    ///////////////////////////////////////////////////////////////////////
    // Main Workspace
    class RDFW;



    /**
     * @brief   Object Class
     * characters:
     *      location    (int)   : ...
     *      id          (int)   : ...
     *      is_keep     (int)   : whether to keep constrain
     *      unable_site (int)   : ??
     *
     * Methods:
     *      Object (Init)       : Initialize id, sort and location (id must be known)
     *      ToString            : get object's infomation strings
     */
    class Object
    {
    public:
        int location;
        int id;
        int is_keep = 0;  // is_keep 越大，这个约束越值得维护
        int unable_site = -1;

        string sort = "";
        Object(int id, const string &sort = "", int location = UNKNOWN)
            : sort(sort), location(location), id(id) {}   // Initialize id, sort and location (id must be known)

        Object();
        virtual string ToString()
        {
            return "id:" + to_string(id) + "    at:" + to_string(location) + "     sort:" + sort + "\n";
        }
        ~Object() {}
    };



    /**
     * @brief   Small Object Class
     * characters:
     *      color       (string): ...
     *      inside      (int)   : the big object id which small object inside
     *
     * Methods:
     *      Object (Init)       : Initialize id, sort and location (id must be known)
     *      ToString            : get object's infomation strings
     */
    class SmallObject : public Object
    {
    public:
        string color = "";
        int inside = UNKNOWN;  // the big object id which small object inside
        int on = UNKNOWN;      // the big object id which small object on

        // Initialize through info
        SmallObject(int id, int location = UNKNOWN, const string &sort = "", const string &color = "")
            : Object(location, sort, id), color(color) {}

        // Initialize through Object class
        SmallObject(shared_ptr<Object> obj)
            : Object(*obj) {}

        // Override ToString
        virtual string ToString() override
        {
            return Object::ToString() + "color:" + color + "    inside:" + to_string(inside) + "    on:" + to_string(on) + "\n";
        }
        ~SmallObject() {}
    };


    /**
     * @brief   Big Object Class
     * characters:
     *      color       (string): ...
     *      inside      (int)   : the big object id which small object inside
     *
     */
    class BigObject : public Object
    {
    public:
        // Initialize through info
        BigObject(int id, int location = UNKNOWN, string sort = "")
            : Object(location, sort, id) {}

        // Initialize through Object class
        BigObject(shared_ptr<Object> obj)
            : Object(*obj) {}

        ~BigObject() {}
    };


    /**
     * @brief   Container
     * characters:
     *      smallObjectsInside  (vector<shared_ptr<SmallObject>>)
     *      isOpen      (int)   : 0(closed)  1(open)
     *      color       (string): ...
     *      inside      (int)   : the big object id which small object inside
     *
     * Methods:
     *      DeleteObjectInside  : delete target id object
     *      ToString            : override...
     */
    class Container : public BigObject
    {
    public:
        vector<shared_ptr<SmallObject>> smallObjectsInside;
        int isOpen = 0;
        // Through info
        Container(int id, int location = UNKNOWN, bool isOpen = true, string sort = "")
            : BigObject(id, location, sort), isOpen(isOpen) {}

        // through Object
        Container(shared_ptr<Object> obj)
            : BigObject(obj), isOpen(UNKNOWN) {}

        // through BigObject
        Container(shared_ptr<BigObject> obj)
            : BigObject(*obj), isOpen(UNKNOWN) {}


        void DeleteObjectInside(shared_ptr<SmallObject> target)
        {
            for (int i = 0; i < smallObjectsInside.size(); i++)
            {
                if (smallObjectsInside[i]->id == target->id) {
                    smallObjectsInside.erase(smallObjectsInside.begin() + i);
                }
            }
        }

        virtual string ToString() override
        {
            string out = BigObject::ToString() + "Small Objects Inside:\n";
            for (int i = 0; i < smallObjectsInside.size(); i++)
            {
                out += to_string(i) + " " + smallObjectsInside[i]->ToString();
            }
            return out;
        }
        ~Container() {}
    };



    /**
     * @brief   Robot
     * characters:
     *      hold        (shared_ptr<SmallObject>)   : ...
     *      plate       (shared_ptr<SmallObject>)   : ...
     *      hold_id     (int)   : ...
     *      plate_id    (int)   : ...
     *
     * Methods:
     *      SetHold     : ...
     *      SetPlate    : ...
     *      ToString    : ...
     */
    class Robot : public Object
    {
    public:
        shared_ptr<SmallObject> hold;
        shared_ptr<SmallObject> plate;

        int hold_id = NONE, plate_id = UNKNOWN;

        Robot(int id, int location = UNKNOWN)
            : Object(id, "robot", location) {}
        Robot()
            : Robot(0) {}

        void SetHold(const shared_ptr<SmallObject> &hold) {
            this->hold = hold;
            if (hold != nullptr) {
                this->hold->location = location;
                hold_id = hold->id;
                hold->inside = NONE;
            }
            else
                hold_id = NONE;
        }

        void SetPlate(const shared_ptr<SmallObject> &plate) {
            this->plate = plate;
            if (plate != nullptr) {
                this->plate->location = location;
                plate_id = plate->id;
                plate->inside = NONE;
            }
            else
                plate_id = NONE;
        }

        virtual string ToString() override{
            return Object::ToString() + "hold:\n" + (hold != nullptr ? hold->ToString() : "") + "plate:\n" + (plate != nullptr ? plate->ToString() : "");
        }

        ~Robot() {}
    };


    // 安全的Object转换，保证返回不为nullptr
    template <class T>
    __inline__ __attribute__((always_inline)) shared_ptr<T> ObjectPtrCast(const shared_ptr<Object> &obj)
    {
        if (!obj)
            throw std::invalid_argument("ObjectPtrCast: null object");
        auto p = dynamic_pointer_cast<T>(obj); // try convert type
        if (p == nullptr)
        {
            LOG_ERROR("Object (%d %s) cast error.", obj->id, obj->sort.c_str());
            throw std::runtime_error(
                "ObjectPtrCast: object " + std::to_string(obj->id) +
                " has incompatible runtime type");
        }
        return p;
    }


    // 类似树，有值和节点的数据结构
    struct SyntaxNode
    {
        string value;
        vector<shared_ptr<SyntaxNode>> sons;
    };


    struct Condition
    {
        string sort = "";
        string color = "";
        string declared_type = "";
        int object_id = UNKNOWN;
        bool has_explicit_id = false;

        // get condition String
        string ToString() const {
            return "(Sort:" + sort + ",Color:" + color + ")";
        }

        bool IsObjectSatisfy(const shared_ptr<Object> &target) const;  // see ".cpp" file
    };





    //////////////////////////////////////////////////////////////////////////////////////////////////////////
    /**
     * @brief       Class: RDFW:  this class is used to contain all the objs, envs, tasks, cons variebles
     *                            and all actions are done in it
     *                            You can regard it as our team "Workspace"
     *
     * @public      Class: Plug:  the official API
     *              Class: Robot: operator
     *              Class: enable_shared_from_this<RDFW>: 我也不知道这个干啥的
     *
     * @memberof    懒得写了,往下看吧....
     */

    class RDFW : public Plug,  // official API
                 public Robot,
                 public enable_shared_from_this<RDFW>
    {
    public:

        /////////////////////////////////////////////////////////////////////////////////////////////////
        // Some Initialize function statements
        RDFW();
        ~RDFW() override;
        void Init(int argc, char **argv);
        void InitializeDynamicArrays(int max_size = 100);  // 初始化动态数组


        /////////////////////////////////////////////////////////////////////////////////////////////////
        // official API Override function statement

        /**
         * Plug::Plan() override
         * The processing of a plan should be implemented in this function.
         */
        void Plan();

        const DeadlineManager& GetDeadlineManager() const { return deadline_manager; }
        TerminalSummary GetTerminalSummary() const {
            return terminal_checker.evaluateAll(*this);
        }
        ScoreSnapshot GetScoreSnapshot() const {
            return score_evaluator.snapshot(*this, terminal_checker);
        }
        std::size_t GetStopRescanCount() const { return stop_rescan_count; }
        CandidatePlan PreviewCandidatePlan(std::size_t candidate_task_index);
        const std::vector<CandidateExecutionRecord>& DecisionFeedback() const {
            return decision_feedback;
        }
        void ExecuteTerminalRecovery();



        /////////////////////////////////////////////////////////////////////////////////////////////////
        // Some envs, tasks, cons, infos, objects variable



        /**
         * @brief   all the objects of the envs.
         * @typedef shared_ptr<Object/...>
         */
        shared_ptr <BigObject>                  human;          // human
        vector     <shared_ptr <Object>>        objects;        // 场景中所有Object, id为索引
        vector     <shared_ptr <SmallObject>>   smallObjects;   // 场景中所有SmallObject

        /**
         * @brief   Store all instructions (tasks, infos, cons, ...)
         * @typedef Instruction
         */
        vector<Instruction> tasks;  // 需完成 任务list
        vector<Instruction> infos;  // 补充 info list
        vector<Instruction> not_infoConstrains;    // 约束条件：不能执行的信息
        vector<Instruction> not_taskConstrains;    // 约束条件：不能执行的任务
        vector<Instruction> notnot_infoConstrains; // 约束条件：必须保持的信息
        // One entry per constraint in TerminalChecker order. False is permanent
        // for this question, including after the predicate becomes true again.
        vector<bool> constraint_eligible;




        /**
         * @brief   Store all execute Mode and Status variables (stage, keepCons, NLP, Ask, Pass...)
         * @typedef int / bool
         */
        int  stage              = 2;           // 阶段 (1 or 2)
        int  task_index         = 0;           // 当前正在处理的任务索引
        int  err_times          = 0;           // 执行任务出错次数
        int  task_limit         = 4;           // 执行任务的限制次数

        bool isKeepConstrain    = false;       // 是否维护约束 ( =1: Do not do any tasks)
        bool isAutoConstrain    = false;       // ?????
        bool isAskTwice         = false;       // 是否在在纠错模式下，询问两次
        bool isErrorCorrection  = false;       // 是否开启纠错模式
        bool isNaturalParse     = false;       // 是否开启自然语言处理
        bool isPass             = false;       // 是否跳过任务



        /////////////////////////////////////////////////////////////////////////////////////////////////
        // Cons, Tasks look-up tables

        /**
         * @brief   Constraints look-up table (动态数组版本)
         * @typedef int / bool
         */
        vector<int> goto_cons;           //not_task   goto
        vector<vector<int>> putin_cons;      //not_info   inside   + not_task   putin
        vector<vector<int>> takeout_cons;    // ontnot_infor  inside   + not_task   takeout
        vector<vector<int>> putdown_cons;    //not_info   on   + not_task   puton
        vector<int> putdown1_cons;        //not_task   putdown  + hold  + plate
        vector<vector<int>> move_cons;       //not_info   near   +
        vector<int> open_cons;           //not_info   opened   + notnot_info   closed  + not_task   open
        vector<int> close_cons;          //not_info   closed   + notnot_info   opened  + not_task   close
        vector<int> pickup_cons;         //not_info   plate   + not_task   pickup
        //vector<vector<int>> pickup1_cons;
        vector<int> givehuman_cons;      //not_task   give
        vector<int> fromplate_cons;      //hold
        vector<int> toplate_cons;        //plate


        vector<vector<int>> mustnear_cons;       //notnot_info   mustnear

        vector<bool> rightlocation;

        // ==================== Must Near 纠错与补全 ====================

        // 锁位：must near 成组后为所有成员上锁
        std::vector<bool> lock_by_mustnear;  // size == objects.size()

        // must-near 关系是对象到对象的关系，组件编号始终按对象 id 建立。
        // objectLocationInferredByMustNear 用来区分直接证据和约束推导证据，
        // 避免一次推导在后续刷新中被误当成新的独立票源。
        std::vector<int> mustNearComponent;
        std::vector<bool> objectLocationInferredByMustNear;

        // 并查集（must near 等价类）
        int uf_parent[256], uf_size[256], uf_groupLoc[256]; // UNKNOWN=-1

        // 感知回调（外部绑定到现有 Sense/Ask 管线）
        std::function<bool(unsigned /*id*/, int /*loc*/)> sense_cb;

        // 配置开关
        // Repeated competition goals carry multiplicity. Only a caller that
        // explicitly selects a unique-goal input contract may erase copies.
        bool deduplicate_input = false;
        bool enable_near_correction = true;    // 总开关
        bool enable_must_lock = true;          // must 组锁位




        /////////////////////////////////////////////////////////////////////////////////////////////////
        // Parsing Functions and variables

        parser *nlp_parser = nullptr;
        vector<string> errorlist;
        vector<bool> posCorrectFlag;  // 位置物品正确性标识
        vector<bool> posSensedFlag;   // 位置感知记录标识，避免重复感知
        // 每个当前事实的值保存在对象中，以下并行数组保存来源及其是否足以
        // 判定 Stage 2 终态。初始描述/AskLoc 为弱证据；可靠约束推导、
        // 直接观察和动作反馈按各自语义更新。每题初始化，dry-run 快照恢复。
        vector<bool> objectLocationVerified;
        vector<bool> objectInsideVerified;
        vector<bool> containerStateVerified;
        vector<EvidenceSource> objectLocationSource;
        vector<EvidenceSource> objectInsideSource;
        vector<EvidenceSource> containerStateSource;
        bool isMultiGotoMode;         // 是否在多goto任务模式下，用于控制是否跳过Sense操作

        // 位置感知物体记录
        struct LocationSensedInfo {
            vector<unsigned int> object_ids;     // 感知到的物体ID列表
            unsigned int container_id;           // 感知到的容器ID（一个位置只能有一个大物体）
            bool has_container;                  // 该位置是否有容器
        };
        vector<LocationSensedInfo> locationSensedObjects;  // 每个位置的感知物体记录

        // 位置感知物体记录访问函数
        const LocationSensedInfo& GetLocationSensedInfo(int location) const;
        bool HasObjectAtLocation(int location, unsigned int object_id) const;
        bool HasContainerAtLocation(int location) const;
        vector<unsigned int> GetObjectsAtLocation(int location) const;
        unsigned int GetContainerAtLocation(int location) const;
        int CountObjectsAtLocation(int location) const;


        bool ParseEnv(const string &env);  // 环境解析
        bool ParseEnvSentence(const string &str);  // 环境单句解析
        bool ParseInstruction(const string &task); // 指令解析
        void ParseNaturalLanguage(const string &src);  // 自然语言解析
        bool ParseNaturalLanguageSentence(const string &s);  // 自然语言单句解析
        void ParseInfo(const Instruction &info);  // 解析补充info list

        enum class InstructionKind {
            TASK,
            INFO,
            NOT_TASK_CONSTRAINT,
            NOT_INFO_CONSTRAINT,
            MUST_INFO_CONSTRAINT
        };

        // Schema validation is public so negative tests and diagnostic tools can
        // validate hand-built instructions without entering the planner.
        bool ValidateInstruction(Instruction &instruction, InstructionKind kind);
        // The one shared gate for IT and NL; call before info/corrections/planning.
        bool RunQuestionPreflight();
        const QuestionPreflightReport& preflightReport() const { return preflight_report; }
        static string SemanticInstructionKey(const Instruction& instruction,
                                             InstructionKind kind);
        std::size_t discardedInstructionCount() const {
            return discarded_instruction_count;
        }

        bool IsValidObjectId(int id) const;
        shared_ptr<Object> GetObject(int id) const;

        //下面两个是为了优化新增的
        string ExtractValue(const string& taskDis, int tag1, int tag2);
        // void ExtractInstructions(const vector<shared_ptr<SyntaxNode>>& nodes, vector<Instruction>& instructions, const string& instructionType);
        void ExtractInstructions(const vector<shared_ptr<SyntaxNode>>& nodes);

        // 完结撒花
        void Fini();

        // 内存管理优化
        void OptimizeMemoryUsage();


        /////////////////////////////////////////////////////////////////////////////////////////////////
        // Print out Functions
        void LogInstructionError(const Instruction& task); //新增
        void PrintInstruction();    // 输出所有指令
        void PrintEnv();  // 输出场景信息

        // 推断未知位置函数
        void InferUnknownLocations();



        // 约束规划函数
        void Cons_plan();
        void FilterConstraintsByTaskConflicts();

        // 更新任务列表函数
        void UpdateTaskList(const string &behave, const shared_ptr<Object> &x, const shared_ptr<Object> &y = nullptr);

        // 任务优化函数
        vector<Instruction> TaskOptimization();

        // 查询题目是否提出过任务；已完成/停用的任务仍算，NONE 表示不匹配 Y。
        bool HasRequestedTask(const string &behave, unsigned int object_id,
                              unsigned int target_id = NONE) const;

        // 任务执行相关函数
        bool SolveTask(const Instruction &task);
        bool DoBehavious(const string &behavious, unsigned int x);
        bool DoBehavious(const string &behavious, unsigned int x, unsigned int y);
        bool HoldSmallObject(unsigned int a);

        // 任务求解函数
        bool SolveTask_PickUp(unsigned int a);
        bool SolveTask_PutDown(unsigned int a);
        bool SolveTask_Goto(unsigned int a);
        bool SolveTask_Open(unsigned int a);
        bool SolveTask_Close(unsigned int a);
        bool SolveTask_Give(unsigned int a);
        bool SolveTask_Putin(unsigned int a, unsigned int b);
        bool SolveTask_TakeOut(unsigned int a, unsigned int b);
        bool SolveTask_PutOn(unsigned int a, unsigned int b);

        // 风险评估函数
        int CalculateTaskRisk(Instruction &t);
        int CalculateStepRisk(Instruction &t);

        // 逻辑处理函数
        enum class TakeOutResult {
            Success,
            NeedContainerLocation,
            NeedObjectLocation,
            VerifyObjectRelation
        };
        TakeOutResult TakeOutLogic(unsigned int small, unsigned int cont);

        // 任务选择和执行函数
        void MustChooseOne();
        bool CheckAndDeferMultiGoto();
        void ExecuteMultiGotoAggregation();

        // 感知和询问函数
        std::string AskLoc(unsigned int a);
        void Sense();
        void SenseAndUpdateEnvironment();  // 新增：每次移动后的环境感知和更新
        void SenseCurrentLocationOnly(bool force = false);   // 只感知当前位置的物体

        // 状态检查函数
        bool Isinside(unsigned int a, unsigned int b);

        // 任务后处理函数
        void AfterSolveTask(const Instruction &task);

        // 任务执行循环函数
        void ExecuteMainTaskLoop(bool defer_multi_goto);
        void ExecuteCheckPhase(bool defer_multi_goto);

        // 辅助函数
        bool IsKeepingGoing(unsigned int t);
        bool sense(unsigned int a);
        int findrightlocation(unsigned int a);

        // ==================== Must Near 纠错与补全函数 ====================

        // 并查集操作
        int FindUF(int x);
        void UnionUF(int a, int b);

        // 入口：解析后、规划前调用的一致化修正
        void ApplyMustNearConstraintCorrection();

        // 从 must near/nextto 约束构建对称的对象关系图（覆盖 X × Y）。
        void BuildMustNearRelations();

        // 刷新组内位置一致性与锁定状态。propagate_evidence=true 时，
        // 允许把无冲突的位置证据传播给同一 must-near 组件。
        void RefreshMustNearConstraintState(bool propagate_evidence);

        // 记录直接位置证据；直接证据的优先级高于 must-near 推导证据。
        void MarkDirectLocationEvidence(unsigned int id, bool verified,
            EvidenceSource source = EvidenceSource::ACTION_SUCCESS);
        void SetInsideEvidence(unsigned int id, bool verified, EvidenceSource source);
        void SetContainerEvidence(unsigned int id, bool verified, EvidenceSource source);
        EvidenceSource LocationSource(unsigned int id) const;
        EvidenceSource InsideSource(unsigned int id) const;
        EvidenceSource ContainerSource(unsigned int id) const;

        bool hold_mustnear = false;
        bool plate_mustnear = false;

        // ==================== Must In 纠错与补全函数 ====================
        void ApplyMustInConstraintCorrection();
        void ApplyOpenCloseCorrection();


        // === Zero-Action Precheck (stage2 only) ===
        bool IsZeroActionSatisfy(const Instruction& t) const;
        bool ZeroActionPreCheck(Instruction& t);

        // Read-only evidence queries used by deterministic terminal evaluation.
        bool IsLocationVerified(unsigned int id) const;
        bool IsInsideVerified(unsigned int id) const;
        bool IsContainerStateVerified(unsigned int id) const;
        bool IsAbsentFromSensedLocation(unsigned int id, int loc) const;


    private:

        friend class TerminalChecker;

        DeadlineManager deadline_manager;
        TerminalChecker terminal_checker;
        ScoreEvaluator score_evaluator;
        std::chrono::milliseconds plan_safety_margin{300};
        bool normal_stop_requested = false;
        bool shadow_dry_run = false;
        std::vector<CandidateAction>* shadow_action_sink = nullptr;
        bool has_active_candidate = false;
        CandidatePlan active_candidate;
        TerminalSummary active_terminal_before;
        ScoreSnapshot active_score_before;
        std::vector<CandidateExecutionRecord> decision_feedback;
        std::size_t next_candidate_id = 1;
        bool active_action_failed = false;
        std::size_t active_actual_actions = 0;
        std::vector<std::string> active_failure_reasons;
        std::size_t discarded_instruction_count = 0;
        QuestionPreflightReport preflight_report;
        std::size_t world_revision = 0;
        std::size_t stop_rescan_count = 0;
        std::vector<std::size_t> failed_task_revision;
        static const std::size_t NO_FAILED_REVISION = static_cast<std::size_t>(-1);

        CandidatePlan BuildCandidatePlan(std::size_t candidate_task_index);
        CandidatePlan BuildTaskGroupPlan(const std::vector<std::size_t>& group);
        CandidatePlan PreviewFinalMove(unsigned int destination);
        bool ShouldStartConstraintTrade(
            const CandidatePlan& candidate,
            const std::vector<CandidatePlan>& alternatives,
            const char* phase);
        std::vector<CandidateEvidence> CaptureCandidateEvidence() const;
        CandidatePlan BuildSyntheticPutOnCandidate(unsigned int object_id,
                                                    unsigned int target_id);
        std::vector<CandidatePlan> EvaluateShadowCandidates(
            const char* phase, bool include_goto, std::size_t legacy_choice);
        void RefreshTaskStates();
        const CandidatePlan* FindCandidate(
            const std::vector<CandidatePlan>& candidates,
            std::size_t candidate_task_index) const;
        bool HasExecutableCandidate(
            const std::vector<CandidatePlan>& candidates) const;
        bool StopGate(const char* phase, bool include_goto,
                      const std::vector<CandidatePlan>& candidates);
        bool CanStartPlan(const CandidatePlan& candidate, const char* phase) const;
        void BeginCandidateExecution(const CandidatePlan& candidate,
                                     const char* selection_reason = "legacy_task_order");
        void EndCandidateExecution(bool succeeded);
        void RecordAction(ActionCategory category, const char* name,
                          const std::vector<unsigned int>& arguments);
        void RecordActionOutcome(const char* name, bool succeeded);
        void InitializeConstraintLedger();
        void UpdateConstraintLedger(const char* action,
                                    const std::vector<unsigned int>& arguments);
        bool DryRunActionSucceeds(const char* name,
                                  const std::vector<unsigned int>& arguments) const;
        void DryRunSenseIds(std::vector<unsigned int>& sensed_ids) const;
        void LogProgressSnapshot(const char* phase) const;

        bool GetSmallObjectStatus(unsigned int a);
        bool GetBigObjectStatus(unsigned int a);
        bool EnsureEvidenceCapacity(unsigned int id);
        void InvalidateSenseAtLocation(int loc);
        //确保数组不越界
        bool EnsureLocationCapacity(int loc);
        bool EnsureObjectExists(unsigned id, bool prefer_small=false);
        bool AddValidatedInstruction(const shared_ptr<SyntaxNode>& node,
                                     InstructionKind kind,
                                     vector<Instruction>& destination);
        /*=====================原子动作==========================*/
         bool Move(unsigned int x);
         bool PickUp(unsigned int a);
         bool PutDown(unsigned int a);
         bool ToPlate(unsigned int a);
         bool FromPlate(unsigned int a);
         bool Open(unsigned int a);
         bool Close(unsigned int a);
         bool PutIn(unsigned int a, unsigned int b);
         bool TakeOut(unsigned int a, unsigned int b);



#pragma endregion


        /////////////////////////////////////////////////////////////////////////////////////////////////
        // 剩下一些小屁函数，直接放头文件里了

        void ErrTimesAdd()
        {
            err_times++;
            if (err_times > 2)
                isPass = true;
        }

        int solved_task_num = 0;

        void SetSolvedTaskNum(int num)
        {
            solved_task_num = num;
            if (isAutoConstrain)
                if (num > task_limit)   // 根据做的任务数量，决定是否需要维护约束
                    isKeepConstrain = 1;
                else
                    isKeepConstrain = 0;
        }

    }; // RDFW



    //////////////////////////////////////////////////////////////////////////////////////////////////////////
    /**
     * @brief       Class: Instruction:  contain all informations of an instruction
     *
     * Basic params
     * @param       bahave      (string)
     * @param       conditionX  (Condition)
     * @param       conditionY  (Condition)
     * @param       X           (vector <shared_ptr<Object>>)
     * @param       Y           (vector <shared_ptr<Object>>)
     *
     * Extension params
     * @param       risk            : 风险系数
     * @param       repeat_times    : 同类任务出现次数
     * @param       is_cheat        : 打标签, 是否是欺骗任务
     * @param       ask_times       : 询问次数
     * @param       isUseY          : 此任务是否含有Y
     * @param       isEnable        : 不知道干啥
     * @param       isfalse         : 任务是否失败
     * @param       conflictnum     : 矛盾任务类别
     *
     * functions
     * @param       SearchConditionObject
     * @param       TaskSelfOptimization
     * @param       IsInstructionInvoke
     * @param       ToString
     *
     */
    class Instruction
    {
    public:
        enum class ValidationStatus {
            UNCHECKED,
            VALID,
            REPAIRED,
            INFERRED,
            REJECTED
        };

        string behave;
        Condition conditionX, conditionY;
        int risk=0;
        int repeat_times=1;
        int is_cheat=0;
        int ask_times=0;
        vector<shared_ptr<Object>> X, Y;
        bool isUseY = false;
        bool isEnable = true;
        bool isfalse = 0;
        bool isMultiPuton = false;  // 标记是否为多puton任务（同一大物体有多个puton任务）
        bool hasMissingObjects = false;  // 标记是否包含不存在的物体
        bool syntaxValid = true;
        bool structuredSource = false;
        vector<string> declaredParameters;
        ValidationStatus validationStatus = ValidationStatus::UNCHECKED;
        string validationMessage;
        int conflictnum=0;//定义这是哪一种矛盾任务；

        Instruction();
        Instruction(const shared_ptr<SyntaxNode> &node, const shared_ptr<RDFW> &rdfw);

        void SearchConditionObject(const shared_ptr<RDFW> &rdfw, bool is_every=false);
        bool IsUsable() const {
            return syntaxValid && !hasMissingObjects &&
                   validationStatus != ValidationStatus::REJECTED;
        }
        static const char* ValidationStatusName(ValidationStatus status);
        // void Instruction_NotNot(const shared_ptr<SyntaxNode> &node, const shared_ptr<RDFW> &rdfw);

        __inline__ __attribute__((__always_inline__)) bool IsInstructionInvoke(const string &behave, const shared_ptr<Object> &x, const shared_ptr<Object> &y = nullptr);

        string ToString() const
        {
            string result = "Behave:" MAGENTA + behave + RESET "\nConditionX:" MAGENTA + conditionX.ToString() + RESET "\nConditionY:" MAGENTA + conditionY.ToString() + RESET "\n";
            if (validationStatus == ValidationStatus::REJECTED ||
                validationStatus == ValidationStatus::REPAIRED)
                result += string("Validation:") + ValidationStatusName(validationStatus) + " " + validationMessage + "\n";
            return result;
        }

    private:
    };


} //_home
