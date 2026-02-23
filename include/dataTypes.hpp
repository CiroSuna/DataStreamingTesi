#ifndef DATA_TYPES_H
#define DATA_TYPES_H

struct data{
    int curr_value{};
    int original_value{};
    data(int _x) : curr_value{_x}, original_value{_x} {}
    data() : curr_value{}, original_value{} {}
};

#endif