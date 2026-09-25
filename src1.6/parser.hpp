/*
 * File: parser.hpp
 * Author : ShiQiao Chen(陈世侨)
 * Affiliation: WuHan University of Technology
 */
#
#pragma once

#include <string>
#include <iostream>
#include <vector>
#include <algorithm>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include "rdfw.hpp"
using namespace std;

#define S (uint8_t)255 // 句
#define NP (uint8_t)1  // 名词部分
#define VP (uint8_t)2  // 动词部分

#define N (uint8_t)3    // 名词
#define ADJ (uint8_t)4  // 形容词
#define ART (uint8_t)5  // 冠词
#define PREP (uint8_t)6 // 介词
#define V (uint8_t)7    // 动词
#define BE (uint8_t)15

#define OF (uint8_t)16
#define THERE (uint8_t)8
#define NOT (uint8_t)9
#define MUST (uint8_t)10
#define WHICH (uint8_t)13

//词语的类型和词语内容存放
struct token
{
    uint8_t type;
    string value;
    token(uint8_t type, const string &value) : type(type), value(value) {}
    token(uint8_t type) : type(type), value("") {}
    token() {}
};
//语句指针，语句又分为名词短语NP和动词短语VP....
struct syntax_node
{
    struct token token;
    shared_ptr<syntax_node> father;
    vector<shared_ptr<syntax_node>> sons;
    syntax_node(const struct token &v) : token(v) {}
};

class parser
{
public:
    bool is_must;
    bool is_not;
    bool is_every;
    vector<shared_ptr<syntax_node>> tokens; //存词语
    vector<shared_ptr<syntax_node>> stack;  //
    shared_ptr<syntax_node> root;
    unordered_map<string, uint8_t> words_map;   //词语类型查询map
    unordered_set<string> color_set;

    bool parse(const string &str);
    void words_map_initialize(const string &words_map_path = "../words.txt");   //初始化词汇表
    _home::Instruction get_task_instruction();  //task分析
    _home::Instruction get_info_instruction();  //info分析
    _home::Condition get_object_condition(const shared_ptr<syntax_node> &np, bool is_check_color = true);   //寻找condition物体
    shared_ptr<syntax_node> find_v(const shared_ptr<syntax_node> &vp);  //寻找动词
    shared_ptr<syntax_node> find_n(const shared_ptr<syntax_node> &np);  //寻找名词
    parser();
    ~parser();

private:
    bool is_last_token = false;     //最后一个词汇？？
    void clear_tree();
    void to_token(string str);  //分解词汇，过滤无关词汇
    void push_down_automata();
    void push_down(shared_ptr<syntax_node> &p);
    bool match_rule(shared_ptr<syntax_node> &p, uint8_t match_to, uint8_t last);
    bool match_rule(shared_ptr<syntax_node> &p, uint8_t match_to, uint8_t last, uint8_t last_last);
    bool match_rule(shared_ptr<syntax_node> &p, uint8_t match_to, uint8_t last, uint8_t last_last, uint8_t last_last_last);

    bool inline is_match_rule(const shared_ptr<syntax_node> &p, uint8_t t0, uint8_t t1, uint8_t t2, uint8_t t3)
    {
        if (p->sons.size() != 4)
            return false;
        auto &sons = p->sons;
        if (sons[0]->token.type == t0 && sons[1]->token.type == t1 && sons[2]->token.type == t2 && sons[3]->token.type == t3)//修改==t3
            return true;
        return false;
    }
    bool inline is_match_rule(const shared_ptr<syntax_node> &p, uint8_t t0, uint8_t t1, uint8_t t2)
    {
        if (p->sons.size() != 3)
            return false;
        auto &sons = p->sons;
        if (sons[0]->token.type == t0 && sons[1]->token.type == t1 && sons[2]->token.type == t2)
            return true;
        return false;
    }
    bool inline is_match_rule(const shared_ptr<syntax_node> &p, uint8_t t0, uint8_t t1)
    {
        if (p->sons.size() != 2)
            return false;
        auto &sons = p->sons;
        if (sons[0]->token.type == t0 && sons[1]->token.type == t1)
            return true;
        return false;
    }

    void push_back_np(shared_ptr<syntax_node> &np);
    void push_back_vp(shared_ptr<syntax_node> &vp);

    // 清理静态状态的函数
public:
    static void clear_static_state();
};
