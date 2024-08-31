#pragma once
#include "cvt.h"
#include <bits/stdc++.h>
using namespace std;


template <typename T>
void flip_inplace(T arr, uint32_t num) {
    reverse(arr, arr+num);
}


// input:  flipped --> normal
void RISCV_input_flip(bool *input_data, int bit_width){
    // flip all array
    flip_inplace(input_data, bit_width);
    
    // then flip each registers
    for(int reg_idx=0; reg_idx < 32+24; reg_idx++)
        flip_inplace(&input_data[reg_idx*32], 32);

    // finally flip the last 2 bits
    flip_inplace(&input_data[1796], 2);
}


// output: normal --> flipped， 
void RISCV_output_flip(bool *output_data, int bit_width){
    // first flip each registers
    for(int reg_idx=0; reg_idx < 32+24; reg_idx++)
        flip_inplace(&output_data[reg_idx*32], 32);

    // then flip the last 2 bits
    flip_inplace(&output_data[1796], 2);

    // finally flip all array
    flip_inplace(output_data, bit_width);
}