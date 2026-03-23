#pragma once
#include <vector>

class Chunk {
public:
    int id;
    std::vector<char> data;

    Chunk(int id, std::vector<char> data);
};