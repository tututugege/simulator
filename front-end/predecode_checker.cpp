#include "predecode_checker.h"

void predecode_checker_top(struct predecode_checker_in *in, struct predecode_checker_out *out) {
    // correct dir prediction
    for(int i = 0; i < FETCH_WIDTH; i++) {
        switch(in->predecode_type[i]) {
            case PREDECODE_NON_BRANCH:
                out->predict_dir_corrected[i] = false;
                break;
            case PREDECODE_DIRECT_JUMP_NO_JAL:
                out->predict_dir_corrected[i] = in->predict_dir[i];
                break;
            case PREDECODE_JALR:
                out->predict_dir_corrected[i] = true;
                break;
            case PREDECODE_JAL:
                out->predict_dir_corrected[i] = true;
                break;
            default:
                assert(0); // should not reach here
                break;
        }
    }

    // found the first taken pc
    int ft_index = -1;
    for(int i = 0; i < FETCH_WIDTH; i++) {
        if(out->predict_dir_corrected[i] == true) {
            ft_index = i;
            break;
        }
    }

    out->predict_next_fetch_address_corrected = in->predict_next_fetch_address;

    if(ft_index != -1) {
        if(in->predecode_type[ft_index] == PREDECODE_DIRECT_JUMP_NO_JAL || in->predecode_type[ft_index] == PREDECODE_JAL) {
            out->predict_next_fetch_address_corrected = in->predecode_target_address[ft_index];
        }    
    }

    out->predecode_flush_enable = in->predict_next_fetch_address != out->predict_next_fetch_address_corrected;
}