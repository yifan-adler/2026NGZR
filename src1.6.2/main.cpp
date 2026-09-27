/*
 * File: main.cpp
 * Author : ShiQiao Chen(陈世侨)
 * Affiliation: WuHan University of Technology
 */
#include "rdfw.hpp"
#include <exception>
#include <iostream>

int main(int argc, char **argv)
{
    auto rdfw = make_shared<_home::RDFW>();
    rdfw->Init(argc, argv);
    try {
        rdfw->Run();
    } catch (const std::exception& error) {
        std::cerr << "#(RDFW): Platform connection closed: "
                  << error.what() << std::endl;
    }
    return 0;
}
