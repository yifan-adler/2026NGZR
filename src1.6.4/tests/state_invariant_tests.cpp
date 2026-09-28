#include "rdfw.hpp"
#include <cassert>
#include <cstdlib>
#include <memory>
using namespace _home;
namespace _home {
struct ScoreSemanticsTestAccess {
    static bool Ask(RDFW& w) { return w.GetSmallObjectStatus(3); }
    static void Sense(RDFW& w) { w.SenseCurrentLocationOnly(true); }
    static CandidatePlan FinalMove(RDFW& w) { return w.PreviewFinalMove(5); }
    static bool Act(RDFW& w, int action) {
        switch (action) {
        case 0: return w.Open(2);
        case 1: return w.PickUp(3);
        case 2: return w.ToPlate(3);
        case 3: return w.FromPlate(3);
        case 4: return w.PutDown(3);
        case 5: return w.PutIn(3,2);
        case 6: return w.TakeOut(3,2);
        case 7: return w.Move(5);
        case 8: return w.Close(2);
        }
        return false;
    }
    static bool Dry(const RDFW& w, const char* name, std::vector<unsigned> args) {
        return w.DryRunActionSucceeds(name,args);
    }
};
}
static Instruction Task(const char* name, std::shared_ptr<Object> x,
                        std::shared_ptr<Object> y=nullptr) {
    Instruction t; t.behave=name; t.X={x};
    if(y) {t.Y={y};t.isUseY=true;}
    return t;
}
int main(int argc,char** argv) {
    assert(argc==3);
    int test=std::atoi(argv[2]);
    auto w=std::make_shared<RDFW>();
    char program[]="invariant", path[]="-path";
    char* args[]={program,path,argv[1]}; w->Init(3,args); w->stage=2;
    assert(w->ParseEnv("(hold 0) (plate 0) (at 0 1) "
        "(sort 1 human) (size 1 big) (at 1 1) "
        "(sort 2 cupboard) (size 2 big) (type 2 container) (at 2 4) (closed 2) "
        "(sort 3 cup) (size 3 small) (color 3 red) (inside 3 2) "
        "(sort 4 cabinet) (size 4 big) (type 4 container) (at 4 5) (opened 4)"));
    auto s=std::dynamic_pointer_cast<SmallObject>(w->objects[3]);
    auto c=std::dynamic_pointer_cast<Container>(w->objects[2]);
    if(test==0) {
        SmallObject a(7,11); BigObject b(8,12); Container d(9,13);
        assert(a.id==7 && a.location==11 && b.id==8 && b.location==12);
        assert(d.id==9 && d.location==13);
    } else if(test==1) {
        assert(ScoreSemanticsTestAccess::Act(*w,0));
        assert(c->location==w->location && w->IsLocationVerified(2));
        assert(s->location==c->location && !w->IsLocationVerified(3));
    } else if(test==2) {
        assert(ScoreSemanticsTestAccess::Act(*w,1));
        assert(s->inside==NONE && c->smallObjectsInside.empty());
        assert(w->hold==s && w->hold_id==3);
        assert(ScoreSemanticsTestAccess::Act(*w,2));
        assert(!w->hold && w->plate==s);
        assert(ScoreSemanticsTestAccess::Act(*w,7));
        assert(s->location==5);
        assert(ScoreSemanticsTestAccess::Act(*w,3));
        assert(!w->plate && w->hold==s);
        assert(ScoreSemanticsTestAccess::Act(*w,4));
        assert(!w->hold && s->inside==NONE);
    } else if(test==3) {
        w->ParseInfo(Task("inside",s,w->objects[4]));
        w->ParseInfo(Task("inside",s,w->objects[4]));
        auto d=std::dynamic_pointer_cast<Container>(w->objects[4]);
        assert(c->smallObjectsInside.empty() && d->smallObjectsInside.size()==1);
        w->ParseInfo(Task("on",s,w->objects[1]));
        assert(d->smallObjectsInside.empty());
        assert(ScoreSemanticsTestAccess::Act(*w,1));
        assert(s->on==UNKNOWN || s->on==NONE);
    } else if(test==4) {
        c->isOpen=UNKNOWN; c->location=w->location; w->SetHold(s);
        assert(!ScoreSemanticsTestAccess::Dry(*w,"PutIn",{3,2}));
        assert(!ScoreSemanticsTestAccess::Dry(*w,"Open",{2}));
        assert(!ScoreSemanticsTestAccess::Dry(*w,"Move",{1}));
    } else if(test==5) {
        for(int action=0;action<=8;++action) {
            w->SetActionResults({false});
            int loc=w->location, sloc=s->location, in=s->inside, op=c->isOpen;
            auto contents=c->smallObjectsInside;
            assert(!ScoreSemanticsTestAccess::Act(*w,action));
            assert(w->location==loc && s->location==sloc && s->inside==in);
            assert(c->isOpen==op && c->smallObjectsInside==contents);
        }
    } else if(test==6) {
        w->stage=1; c->location=w->location; s->location=w->location;
        c->isOpen=1; w->tasks.push_back(Task("takeout",s,c));
        const auto before=c->smallObjectsInside;
        CandidatePlan p=w->PreviewCandidatePlan(0);
        assert(p.dry_run_succeeded && w->hold_id==NONE && s->inside==2);
        assert(c->smallObjectsInside==before && w->TestPlatformCalls()==0);
        assert(w->SolveTask(w->tasks[0]));
        assert(w->GetTerminalSummary().goals==p.terminal_after.goals);
        assert(w->GetScoreSnapshot().deterministic_base_score==p.score_after.deterministic_base_score);
    }
    else if(test==7) {
        c->location=1; s->location=1; w->SetSenseResult({2});
        w->tasks.push_back(Task("goto",s));
        w->SolveTask_Goto(3);
        assert(!w->IsLocationVerified(3));
        assert(w->GetTerminalSummary().goals[0]==TerminalStatus::UNKNOWN);
    } else if(test==8) {
        w->notnot_infoConstrains.push_back(Task("near",w->objects[2],w->objects[4]));
        w->objects[4]->location=UNKNOWN;
        w->ApplyMustNearConstraintCorrection();
        assert(w->objects[4]->location==4);
        c->location=1; w->objectLocationVerified[2]=true;
        w->objectLocationInferredByMustNear[2]=false;
        w->objectLocationSource[2]=EvidenceSource::SENSE;
        w->RefreshMustNearConstraintState(false);
        assert(w->objects[4]->location==UNKNOWN);
        assert(!w->IsLocationVerified(4));
    } else if(test==9) {
        s->inside=UNKNOWN; s->location=UNKNOWN;
        w->SetAskResult("inside(3,4)");
        assert(ScoreSemanticsTestAccess::Ask(*w));
        w->tasks.push_back(Task("putin",s,w->objects[4]));
        assert(!w->IsInsideVerified(3) && !w->IsLocationVerified(3));
        assert(w->GetTerminalSummary().goals[0]==TerminalStatus::UNKNOWN);
    } else if(test==10) {
        c->location=1; s->location=1; c->isOpen=UNKNOWN;
        w->SetSenseResult({2,3}); ScoreSemanticsTestAccess::Sense(*w);
        assert(s->inside==2 && !w->IsInsideVerified(3));
        w->tasks.push_back(Task("takeout",s,c));
        assert(w->GetTerminalSummary().goals[0]==TerminalStatus::UNKNOWN);
    } else if(test==11) {
        assert(ScoreSemanticsTestAccess::Act(*w,0));
        assert(ScoreSemanticsTestAccess::Act(*w,6));
        assert(w->hold==s && s->inside==NONE && c->smallObjectsInside.empty());
        assert(ScoreSemanticsTestAccess::Act(*w,5));
        assert(!w->hold && s->inside==2 && c->smallObjectsInside.size()==1);
        assert(c->location==s->location && c->isOpen==1);
        assert(ScoreSemanticsTestAccess::Act(*w,8));
        w->tasks.push_back(Task("open",c));
        w->tasks.push_back(Task("close",c));
        auto goals=w->GetTerminalSummary().goals;
        assert(goals[0]==TerminalStatus::UNSATISFIED && goals[1]==TerminalStatus::SATISFIED);
    } else if(test==12) {
        w->stage=1;
        assert(w->ParseInstruction("(:task (pickup X) (:cond (sort X cup) (color X red)))"));
        auto binding=w->tasks[0].X[0];
        auto goal=Task("inside",s,c); w->notnot_infoConstrains.push_back(goal);
        assert(ScoreSemanticsTestAccess::Act(*w,6));
        assert(w->tasks[0].X[0]==binding && binding==w->objects[3]);
        auto summary=w->GetTerminalSummary();
        assert(summary.goals[0]==TerminalStatus::SATISFIED);
        assert(summary.constraints[0]==TerminalStatus::UNSATISFIED);
        assert(ScoreSemanticsTestAccess::Act(*w,5));
        summary=w->GetTerminalSummary();
        assert(summary.goals[0]==TerminalStatus::UNSATISFIED);
        assert(summary.constraints[0]==TerminalStatus::SATISFIED);
        assert(!summary.constraint_eligible[0]);
    } else if(test==13) {
        w->SetHold(s); w->objectInsideVerified[3]=true;
        w->tasks.push_back(Task("goto",s));
        CandidatePlan p=ScoreSemanticsTestAccess::FinalMove(*w);
        assert(w->location==1 && !w->IsLocationVerified(3));
        assert(ScoreSemanticsTestAccess::Act(*w,7));
        assert(w->GetTerminalSummary().goals==p.terminal_after.goals);
    } else if(test==14) {
        w->stage=1;
        w->objectInsideVerified[3]=true;
        w->ParseInfo(Task("on",s,c));
        assert(s->inside==2 && c->smallObjectsInside.size()==1);
        w->ParseInfo(Task("near",s,c));
        assert(s->inside==2 && w->IsInsideVerified(3));
        s->inside=UNKNOWN; w->objectInsideVerified[3]=false;
        w->ParseInfo(Task("near",s,c));
        assert(s->inside==UNKNOWN && !w->IsInsideVerified(3));
    } else if(test==15) {
        w->posSensedFlag[4]=true;
        w->locationSensedObjects[4].object_ids={2};
        assert(ScoreSemanticsTestAccess::Act(*w,0));
        assert(!w->posSensedFlag[4] && !w->HasObjectAtLocation(4,2));
        w->SetPlate(s);
        assert(ScoreSemanticsTestAccess::Act(*w,1));
        assert(w->hold_id==3 && w->plate_id==NONE && !w->plate);
    } else if(test==16) {
        w->notnot_infoConstrains.push_back(Task("near",w->objects[2],w->objects[4]));
        w->objects[4]->location=UNKNOWN;
        w->ApplyMustNearConstraintCorrection();
        assert(w->LocationSource(2)==EvidenceSource::INITIAL);
        assert(w->LocationSource(4)==EvidenceSource::CONSTRAINT_HEURISTIC);
        w->RefreshMustNearConstraintState(false);
        assert(w->objects[2]->location==4 && w->objects[4]->location==4);
        assert(!w->IsLocationVerified(4));
        c->location=UNKNOWN; w->objectLocationVerified[2]=false;
        w->RefreshMustNearConstraintState(false);
        assert(w->objects[4]->location==UNKNOWN);
    } else if(test==17) {
        w->notnot_infoConstrains.push_back(Task("inside",s,c));
        w->ApplyMustInConstraintCorrection();
        assert(w->InsideSource(3)==EvidenceSource::CONSTRAINT_DERIVED);
        w->tasks.push_back(Task("goto",s));
        assert(ScoreSemanticsTestAccess::Act(*w,0));
        assert(s->location==1);
        assert(w->LocationSource(3)==EvidenceSource::CONSTRAINT_DERIVED);
        w->constraint_eligible.assign(1,false);
        w->constraint_uncertain.assign(1,false);
        assert(w->GetTerminalSummary().goals[0]==TerminalStatus::UNKNOWN);
    } else if(test==18) {
        c->location=1; s->location=1; c->isOpen=UNKNOWN;
        w->SetSenseResult({2}); ScoreSemanticsTestAccess::Sense(*w);
        assert(s->inside==2 && s->location==1);
        assert(!w->IsLocationVerified(3));
    } else if(test==19) {
        c->isOpen=UNKNOWN;
        w->notnot_infoConstrains.push_back(Task("opened",c));
        w->ApplyOpenCloseCorrection();
        assert(c->isOpen==1 && w->IsContainerStateVerified(2));
        w->tasks.push_back(Task("open",c));
        assert(w->GetTerminalSummary().goals[0]==TerminalStatus::SATISFIED);
    }
}
