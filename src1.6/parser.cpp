/*
 * File: parser.cpp
 * Author : ShiQiao Chen(陈世侨)
 * Affiliation: WuHan University of Technology
 */
#include "parser.hpp"
#include "fstream"
#include "debuglog.hpp"
#include "rdfw.hpp"
#include "sstream"

ostream &operator<<(ostream &os, const token &t);
ostream &operator<<(ostream &os, const shared_ptr<syntax_node> &p);
static unordered_map<uint8_t, string> define_map;

//词汇表初始化
void parser::words_map_initialize(const string &words_map_path)
{
    //ifstream 输入文件流，world.list
    ifstream f(words_map_path);
    if (!f.is_open())
    {
        LOG_ERROR("%s not found.", words_map_path.c_str());
    }
    string str;
    uint8_t mode;
    while (getline(f, str))
    {
        if (str == "")
            continue;
        if (str == "n:")
            mode = N;
        else if (str == "v:")
            mode = V;
        else if (str == "adj:")
            mode = ADJ;
        else if (str == "art:")
            mode = ART;
        else if (str == "prep:")
            mode = PREP;
        else if (str == "be:")
            mode = BE;
        else
        {
            words_map.emplace(str, mode);
        }
    }
    f.close();

    //words_map是一个std::unorederd_map类
    // unordered_map是一个将key和value关联起来的容器,它可以高效的根据单个key值查找对应的value
    //emplace函数将键及其元素插入unordered_map容器中。有效地将容器尺寸增加了一个
    //words_map.emplace(key, value);
    words_map.emplace("there", THERE);
    words_map.emplace("not", NOT);
    words_map.emplace("no", NOT);
    words_map.emplace("must", MUST);
    words_map.emplace("which", WHICH);
    words_map.emplace("of", OF);

    define_map.emplace(S, "S");
    define_map.emplace(NP, "NP");
    define_map.emplace(VP, "VP");
    define_map.emplace(N, "N");
    define_map.emplace(ADJ, "ADJ");
    define_map.emplace(ART, "ART");
    define_map.emplace(PREP, "PREP");
    define_map.emplace(V, "V");
    define_map.emplace(BE, "BE");
    define_map.emplace(THERE, "THERE");
    define_map.emplace(NOT, "NOT");
    define_map.emplace(MUST, "MUST");
    define_map.emplace(WHICH, "WHICH");
    define_map.emplace(OF, "OF");

    //color_set为std::unordered_set类
    //unordered_set是C++ STL提供的一个用于存储元素的唯一集合容器，它的特点在于元素的存储不需要遵守特定顺序，同时也不允许出现重复元素
    color_set.emplace("white");
    color_set.emplace("black");
    color_set.emplace("red");
    color_set.emplace("green");
    color_set.emplace("yellow");
    color_set.emplace("blue");
    color_set.emplace("bule");
}

//空
parser::parser()
{
}

parser::~parser()
{
    clear_tree();
}

void parser::clear_tree()
{
    vector<shared_ptr<syntax_node>> pending;
    pending.reserve(tokens.size() + stack.size() + 1);
    pending.insert(pending.end(), tokens.begin(), tokens.end());
    pending.insert(pending.end(), stack.begin(), stack.end());
    if (root) pending.push_back(root);

    unordered_set<syntax_node*> visited;
    while (!pending.empty()) {
        shared_ptr<syntax_node> node = pending.back();
        pending.pop_back();
        if (!node || !visited.insert(node.get()).second) continue;
        pending.insert(pending.end(), node->sons.begin(), node->sons.end());
        node->father.reset();
        node->sons.clear();
    }
    tokens.clear();
    stack.clear();
    root.reset();
}

//解析主程序
bool parser::parse(const string &str)
{
    clear_tree();
    is_must = false;
    is_not = false;
    is_last_token = false;
    is_every = false;

    try {
        to_token(str); //第一步过滤词汇
        if (tokens.empty()) {
            LOG_ERROR("The sentence contains no recognized tokens: %s", str.c_str());
            return false;
        }
        push_down_automata();
    } catch (const std::exception& error) {
        LOG_ERROR("Natural-language parser rejected sentence (%s): %s",
                  str.c_str(), error.what());
        root = nullptr;
        return false;
    }
    if (root)
    {
        stringstream ss;
        ss << root;
        LOG("%s", ss.str().c_str());
        return true;
    }
    else
    {
        stringstream ss;
        for (auto &a : stack)
            ss << a;
        LOG_ERROR("The garmmar of sentence:(%s) is incorrect!\n %s", str.c_str(), ss.str().c_str());
        return false;
    }
}

void parser::to_token(string str)
{
    size_t last = 0;
    string temp = "";

    //提取有效信息（空格、大小写字符、句号）（消除干扰字符）
    for (int i = 0; i < str.size(); i++)
    {
        if (str[i] == ' ' || (str[i] >= 'a' && str[i] <= 'z') || (str[i] >= 'A' && str[i] <= 'Z') || str[i] == '.')
            temp.push_back(str[i]);
    }
    str = temp;
    for (int i = 0; i < str.size(); i++)
    {
        //大写变小写
        if (str[i] >= 'A' && str[i] <= 'Z')
            str[i] -= ('A' - 'a');

        //断句
        if (str[i] == ' ' || str[i] == '.')
        {
            string temp = str.substr(last, i - last);
            last = i + 1;
            if (temp.empty())
                continue;
            //过滤语气词
            if (temp == "please")
                continue;
            //特指next to这个词组
            if (temp == "to" && !tokens.empty() &&
                tokens.back()->token.value == "next")
            {
                temp = tokens.back()->token.value + temp;
                tokens.pop_back();
            }

            if (words_map.find(temp) == words_map.end()) //没找到对应，即错误词汇
            {
                LOG_ERROR("%s is unknown type word\n", temp.c_str());
            }
            else //分词+词性标注
            {
                tokens.push_back(make_shared<syntax_node>(token(words_map[temp], temp)));
            }
        }
    }
}

//建立词块树
void parser::push_down_automata()
{
    for (int i = 0; i < tokens.size(); i++)
    {
        if (i == tokens.size() - 1) //最后一个词判断
            is_last_token = true;
        push_down(tokens[i]);

        //     for (int tem = 0; tem < stack.size(); tem++)
        //     {
        //         cout << stack[tem] << "----------------------------------------------\n";
        //     }
        //     cout << "=================================================\n";
    }
    if (stack.size() == 1 && stack.back()->token.type == VP)
    {
        root = make_shared<syntax_node>(S);
        root->sons.push_back(stack.back());
        stack.back()->father = root;
        stack.pop_back();
    }
    else if (stack.size() == 2 && (stack[0]->token.type == THERE || stack[0]->token.type == NP) && stack.back()->token.type == VP)
    {
        root = make_shared<syntax_node>(S);
        for (int i = 0; i < 2; i++)
        {
            root->sons.push_back(stack[i]);
            stack[i]->father = root;
        }
        stack.clear();
    }
    else if (stack.size() == 3 && stack[0]->token.type == THERE && stack[1]->token.type == VP &&
             (stack.back()->token.type == ADJ || (stack.back()->token.type == VP && is_match_rule(stack.back(), BE, ADJ))))
    {
        root = make_shared<syntax_node>(S);
        for (int i = 0; i < 3; i++)
        {
            root->sons.push_back(stack[i]);
            stack[i]->father = root;
        }
        stack.clear();
    }
    else if (stack.size() > 1 && stack[0]->token.type == VP)
    {
        root = make_shared<syntax_node>(S);
        root->sons.push_back(stack[0]);
        stack.back()->father = root;
        stack.pop_back();
    }
}
void parser::push_down(shared_ptr<syntax_node> &p)
{
    if (p->token.type == ART) //ART：冠词
    {
        //指向每一个
        if (p->token.value == "every" || p->token.value == "all" || p->token.value == "each")
            is_every = true;
        stack.push_back(p);
    }
    else if (p->token.type == WHICH) //WHICH
    {
        while (!stack.empty() && stack.back()->token.type != NP)  //存放的句子最后不是名词短语
        {
            auto p = stack.back();
            stack.pop_back();
            for (auto &s : p->sons)
                stack.push_back(s);
        }
        if (stack.empty()) return;
        stack.push_back(p);
    }
    else if (p->token.type == THERE)
    {
        stack.push_back(p);
    }
    else if (p->token.type == MUST)
    {
        stack.push_back(p);
        is_must = true;
    }
    else if (p->token.type == NOT)
    {
        stack.push_back(p);
        is_not = true;
    }
    else if (p->token.type == V)
    {
        stack.push_back(p);
    }
    else if (p->token.type == BE)
    {
        stack.push_back(p);
    }
    else if (p->token.type == N)
    {
        if (!match_rule(p, NP, ART) && !match_rule(p, NP, ADJ, ART) && !match_rule(p, NP, ADJ))
        {
            auto np = make_shared<syntax_node>(NP);
            np->sons.push_back(p);
            p->father = np;
            push_back_np(np);
        }
    }
    else if (p->token.type == PREP)
    {
        if (!match_rule(p, VP, V)) //&& !match_rule(p, VP, BE))
        {
            while (!stack.empty() && !match_rule(p, VP, NP, V) &&
                   !match_rule(p, VP, NP, VP) &&
                   !match_rule(p, VP, NP, BE) && stack.back()->token.type == VP)
            {
                auto p = stack.back();
                stack.pop_back();
                for (auto &s : p->sons)
                    stack.push_back(s);
            }
            if (stack.empty()) {
                stack.push_back(p);
                return;
            }
            if (stack.back()->token.type != VP)
            {
                auto temp = stack.back();
                stack.pop_back();
                if (temp->token.type == NP)
                    push_back_np(temp);
                else
                    push_down(temp);
                stack.push_back(p);
            }
        }
    }
    else if (p->token.type == ADJ)
    {
        if (!match_rule(p, VP, BE) && !match_rule(p, VP, NOT, BE))
        {
            stack.push_back(p);
        }
    }
    else if (p->token.type == OF)
    {
        while (!stack.empty() && stack.back()->token.type != NP)
        {
            auto p = stack.back();
            stack.pop_back();
            for (auto &s : p->sons)
                stack.push_back(s);
        }
        if (stack.empty()) return;
        stack.push_back(p);
    }
}
void parser::push_back_np(shared_ptr<syntax_node> &np)
{
    if (!match_rule(np, NP, OF, NP) && !match_rule(np, VP, V) &&
        !match_rule(np, VP, PREP, BE) && !match_rule(np, VP, BE) &&
        !match_rule(np, VP, PREP, NOT, BE))
    {
        if (stack.size() == 0)
        {
            stack.push_back(np);
            return;
        }
        while (!stack.empty() && !match_rule(np, VP, PREP, NP, V) &&
               !match_rule(np, VP, PREP, NP, VP) &&
               !match_rule(np, VP, PREP, NP, BE) && stack.back()->token.type == VP)
        {
            auto p = stack.back();
            stack.pop_back();
            for (auto &s : p->sons)
                stack.push_back(s);
        }
        if (stack.empty()) {
            stack.push_back(np);
            return;
        }
        if (stack.back()->token.type != VP)
        {
            auto temp = stack.back();
            stack.pop_back();
            if (temp->token.type == NP)
                push_back_np(temp);
            else
                push_down(temp);
            if (!match_rule(np, VP, VP))
                stack.push_back(np);
        }
    }
}
void parser::push_back_vp(shared_ptr<syntax_node> &vp)
{
    if (!match_rule(vp, NP, WHICH, NP) && !match_rule(vp, VP, NOT, V) &&
        !match_rule(vp, VP, MUST) && !match_rule(vp, VP, NOT, MUST))
    {
        stack.push_back(vp);
    }
}

//语言成分分析
bool parser::match_rule(shared_ptr<syntax_node> &p, uint8_t match_to, uint8_t last)
{
    int i = stack.size();
    if (i > 0 && stack[i - 1]->token.type == last)
    {
        auto temp = make_shared<syntax_node>(match_to);
        //改变树的关系
        temp->sons.push_back(stack[i - 1]);
        stack[i - 1]->father = temp;
        temp->sons.push_back(p);
        p->father = temp;
        stack.pop_back();
        if (match_to == NP)
            push_back_np(temp);
        else if (match_to == VP)
            push_back_vp(temp);

        return true;
    }
    return false;
}
bool parser::match_rule(shared_ptr<syntax_node> &p, uint8_t match_to, uint8_t last, uint8_t last_last)
{
    int i = stack.size();
    if (i > 1 && stack[i - 1]->token.type == last && stack[i - 2]->token.type == last_last)
    {
        auto temp = make_shared<syntax_node>(match_to);
        temp->sons.push_back(stack[i - 2]);
        stack[i - 2]->father = temp;
        temp->sons.push_back(stack[i - 1]);
        stack[i - 1]->father = temp;
        temp->sons.push_back(p);
        p->father = temp;
        stack.pop_back();
        stack.pop_back();
        if (match_to == NP)
            push_back_np(temp);
        else if (match_to == VP)
            push_back_vp(temp);
        return true;
    }
    return false;
}
bool parser::match_rule(shared_ptr<syntax_node> &p, uint8_t match_to, uint8_t last, uint8_t last_last, uint8_t last_last_last)
{
    int i = stack.size();
    if (i > 2 && stack[i - 1]->token.type == last && stack[i - 2]->token.type == last_last && stack[i - 3]->token.type == last_last_last)
    {
        auto temp = make_shared<syntax_node>(match_to);
        temp->sons.push_back(stack[i - 3]);
        stack[i - 3]->father = temp;
        temp->sons.push_back(stack[i - 2]);
        stack[i - 2]->father = temp;
        temp->sons.push_back(stack[i - 1]);
        stack[i - 1]->father = temp;
        temp->sons.push_back(p);
        p->father = temp;
        stack.pop_back();
        stack.pop_back();
        stack.pop_back();
        if (match_to == NP)
            push_back_np(temp);
        else if (match_to == VP)
            push_back_vp(temp);
        return true;
    }
    return false;
}

//递归寻找动词
shared_ptr<syntax_node> parser::find_v(const shared_ptr<syntax_node> &vp)
{
    if (!vp) return nullptr;
    for (auto &son : vp->sons)
    {
        if ((son->token.type == V || son->token.type == BE) && son->token.value != "do")
            return son;
        else if (son->token.type == VP)
        {
            shared_ptr<syntax_node> temp = find_v(son);
            if (temp != nullptr)
                return temp;
        }
    }
    return nullptr;
}
//递归寻找名词
shared_ptr<syntax_node> parser::find_n(const shared_ptr<syntax_node> &np)
{
    if (!np) return nullptr;
    if (np->sons.size() == 1 && np->sons[0]->token.type == N)   //最后一个词语并且是动词
        return np->sons[0];
    else if (is_match_rule(np, ART, N) || is_match_rule(np, ADJ, N))    //满足冠词+名词
        return np->sons[1];
    else if (is_match_rule(np, ART, ADJ, N))    //满足冠词+形容词+名词
        return np->sons[2];
    else if (is_match_rule(np, NP, OF, NP))     //满足名词短语+名词短语 ->继续递归寻找
        return find_n(np->sons[2]);//为什么不是np->sons[0]
    else if (is_match_rule(np, NP, WHICH, VP))  //满足名词短语+which+动词短语->递归寻找
        return find_n(np->sons[0]);
    else
    {
        LOG_ERROR("Find n ERROR");
        return nullptr;
    }
}

//树形结构下规范化命令形式
_home::Instruction parser::get_task_instruction()
{
    _home::Instruction instr;
    if (!root || root->sons.empty() || !root->sons[0]) {
        instr.syntaxValid = false;
        instr.validationMessage = "empty natural-language task tree";
        return instr;
    }
    auto v = find_v(root->sons[0]); //找到一个动词或动词词组
    if (v == nullptr)   //不存在动词，退出
        return instr;

    //后续分析动作，连接动词词组（goto\putdown\takeout\pickup,特指连用的动词词组）
    string behave = "";
    if (is_match_rule(v->father, V, PREP))
    {
        behave = v->token.value + v->father->sons[1]->token.value;
        v = v->father;
    }
    else
        behave = v->token.value;    // give/open/close

    auto father = v->father; //动词为基准往前找

    while (father != nullptr && father->father != nullptr)  //前支的子支遍历
    {
        if (behave == "give" && is_match_rule(father, V, NP) && is_match_rule(father->father, VP, NP)) //寻找give动词对象且句子结构满足 动宾/动宾短语
        {
            instr.behave = behave;
            instr.conditionY = get_object_condition(father->sons[1]);   //目标物体2
            instr.conditionX = get_object_condition(father->father->sons[1]); //目标物体1
            return instr;
        }
        else if (is_match_rule(father, V, NP) || is_match_rule(father, VP, NP) || is_match_rule(father, VP, NP, PREP, NP) ) //句子结构为 动宾/动宾短语/动VP宾（sth of sth）
        {
            break;
        }
        else if (is_match_rule(father, V, NP, PREP) || is_match_rule(father, V, NP, PREP, NP)) //句子结构为 动宾介(pick sth up/ take sth out)/动V宾（sth of sth）
        {
            if (behave == "put" && (father->sons[2]->token.value == "near" || father->sons[2]->token.value == "nextto"))
                behave = "puton";
            else if (behave != "give")  //其他类型动作合成（pickup/putdown/putin/takeout/pickup）
                behave += father->sons[2]->token.value;
            break;
        }
        else
            father = father->father;
    }
    if (father == nullptr)
    {
        LOG_ERROR("task parse error");
        instr.syntaxValid = false;
        instr.validationMessage = "task tree has no argument phrase";
        return instr;
    }
    instr.behave = behave;
    instr.conditionX = get_object_condition(father->sons[1]);
    if (father->sons.size() == 4)
    {
        instr.isUseY = true;
        instr.conditionY = get_object_condition(father->sons[3]);
    }
    return instr;
}
_home::Instruction parser::get_info_instruction()
{
    _home::Instruction instr;
    if (!root) {
        instr.syntaxValid = false;
        instr.validationMessage = "empty natural-language info tree";
        return instr;
    }
    if (is_match_rule(root, NP, VP))
    {
        instr.conditionX = get_object_condition(root->sons[0]);
        auto be = find_v(root->sons[1]);
        if (be == nullptr || be->token.type != BE)
        {
            LOG_ERROR("Info should contain be verb");
            return instr;
        }
        auto father = be->father;
        if (is_match_rule(father, BE, ADJ))//be ...
            instr.behave = father->sons[1]->token.value;
        else if (is_match_rule(father, BE, NOT, ADJ))//be not ...
        {
            instr.behave = father->sons[2]->token.value == "closed" ? "opened" : "closed";
        }
        else if (is_match_rule(father, BE, PREP, NP))//be on the...
        {
            instr.behave = father->sons[1]->token.value;
            instr.isUseY = true;
            instr.conditionY = get_object_condition(father->sons[2]);
        }
        else if (is_match_rule(father, BE, NOT, PREP, NP))// be not on
        {
            instr.behave = father->sons[2]->token.value;
            instr.isUseY = true;
            instr.conditionY = get_object_condition(father->sons[3]);
        }
    }
    else if (is_match_rule(root, THERE, VP) || is_match_rule(root, THERE, VP, ADJ) || is_match_rule(root, THERE, VP, VP)) //there is.../there is
    {
        auto be = find_v(root->sons[1]);
        if (be == nullptr)
        {
            LOG_ERROR("Info should contain be verb");
            return instr;
        }
        auto father = be->father;
        if (is_match_rule(father, BE, NP))
        {
            auto cond = get_object_condition(father->sons[1], false);
            if (cond.color == "opened" || cond.color == "closed")
            {
                instr.behave = cond.color;
                cond.color = "";
                instr.conditionX = cond;
            }
        }
        else if (is_match_rule(father, BE, NP, PREP, NP))
        {
            instr.behave = father->sons[2]->token.value;
            instr.conditionX = get_object_condition(father->sons[1]);
            instr.isUseY = true;
            instr.conditionY = get_object_condition(father->sons[3]);
        }
        if (root->sons.size() >= 3)
        {
            auto thr = root->sons[2];
            if (thr->token.type == ADJ)
                instr.behave = thr->token.value;
            else if (thr->token.type == VP && is_match_rule(thr, BE, ADJ))
                instr.behave = thr->sons[1]->token.value;
        }
    }

    if (instr.behave == "on" && instr.isUseY && instr.conditionY.sort == "plate")
    {
        instr.behave = "plate";
        instr.isUseY = false;
        instr.conditionY = _home::Condition();
    }
    else if (instr.behave == "nextto")
        instr.behave = "on";
    return instr;
}
_home::Condition parser::get_object_condition(const shared_ptr<syntax_node> &np, bool is_check_color)
{
    _home::Condition cond;
    if (!np) return cond;
    string color;
    if (np->sons.size() == 1 && np->sons[0]->token.type == N)
        cond.sort = np->sons[0]->token.value;
    else if (is_match_rule(np, ART, N))
        cond.sort = np->sons[1]->token.value;
    else if (is_match_rule(np, ADJ, N))
    {
        color = np->sons[0]->token.value;
        cond.sort = np->sons[1]->token.value;
    }
    else if (is_match_rule(np, ART, ADJ, N))
    {
        color = np->sons[1]->token.value;
        cond.sort = np->sons[2]->token.value;
    }
    else if (is_match_rule(np, NP, OF, NP))
    {
        auto n = find_n(np->sons[2]);
        return n ? get_object_condition(n->father) : cond;
    }
    else if (is_match_rule(np, NP, WHICH, VP))
    {
        auto n = find_n(np->sons[0]);
        if (!n) return cond;
        auto co = get_object_condition(n->father);
        if (co.color == "" && is_match_rule(np->sons[2], BE, ADJ))
        {
            auto color = np->sons[2]->sons[1]->token.value;
            if (color_set.find(color) != color_set.end())
                co.color = color;
        }
        return co;
    }
    if (is_check_color && color != "" && color_set.find(color) == color_set.end())
    {
        LOG_ERROR("%s color do not regist", color.c_str());
    }
    cond.color = color;
    if (cond.sort == "me")
        cond.sort = "human";
    return cond;
}

ostream &operator<<(ostream &os, const token &t)
{
    return os << "<" << define_map[t.type] << ":" << t.value << ">";
}

ostream &operator<<(ostream &os, const shared_ptr<syntax_node> &p)
{
    if (!p) return os << "<null syntax node>\n";
    static int layer = 0;
    for (int o = 0; o < layer; o++)
        os << "  ";
    os << p->token;
    os << '\n';
    layer++;
    for (int a = 0; a < p->sons.size(); a++)
    {
        os << p->sons[a];
    }
    layer--;
    return os;
}

// 清理静态状态的函数实现
void parser::clear_static_state()
{
    // 清理静态的define_map
    define_map.clear();

    // 重新初始化define_map
    define_map.emplace(S, "S");
    define_map.emplace(NP, "NP");
    define_map.emplace(VP, "VP");
    define_map.emplace(N, "N");
    define_map.emplace(ADJ, "ADJ");
    define_map.emplace(ART, "ART");
    define_map.emplace(PREP, "PREP");
    define_map.emplace(V, "V");
    define_map.emplace(BE, "BE");
    define_map.emplace(THERE, "THERE");
    define_map.emplace(NOT, "NOT");
    define_map.emplace(MUST, "MUST");
    define_map.emplace(WHICH, "WHICH");
    define_map.emplace(OF, "OF");
}
