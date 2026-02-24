#ifndef DATA_TYPES_H
#define DATA_TYPES_H

enum topics {
    WORKERA,
    WORKERB 
};

enum update_type{
    THREAD_INC,
    THREAD_DEC
};
struct data{
    int curr_value{};
    int original_value{};
    data(int _x) : curr_value{_x}, original_value{_x} {}
    data() : curr_value{}, original_value{} {}
};

 struct update_ms {
    update_type t {};
    int resize {};
    update_ms(update_type _t, int _resize) : t{_t}, resize{_resize} {}
    update_ms() : t{}, resize{} {}
};

#endif