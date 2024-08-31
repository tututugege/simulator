#pragma once
#include"cvt.h"
void RISCV_32A(bool input_data[bit_width], bool* output_data) {
	//get input data
	bool general_regs[1024];
	bool reg_csrs[32*21];
	bool instruction[32];
	bool bit_this_pc[32];
	bool bit_load_data[32];
	bool this_priviledge[2];
	copy_indice(general_regs, 0, input_data, 0, 1024);
	copy_indice(reg_csrs, 0, input_data, 1024, 32*21);
	copy_indice(instruction, 0, input_data, 1696, 32);
	copy_indice(bit_this_pc, 0, input_data, 1728, 32);
	copy_indice(bit_load_data, 0, input_data, 1760, 32);
	bool asy = input_data[1792];
	bool page_fault_inst = input_data[1793];
	bool page_fault_load = input_data[1794];
	bool page_fault_store = input_data[1795];
	copy_indice(this_priviledge, 0, input_data, 1796, 2);

	//initialize output data
	bool next_general_regs[1024];
	bool next_reg_csrs[32*21];
	bool bit_next_pc[32];
	bool bit_load_address[32];
	bool bit_store_data[32];
	bool bit_store_address[32];
	bool bit_result_tensor[32];
	bool bit_pc_4[32];
	bool next_priviledge[2];
	copy_indice(next_general_regs, 0, general_regs, 0, 1024);
	copy_indice(next_reg_csrs, 0, reg_csrs, 0, 32*21);
	init_indice(bit_next_pc, 0, 32);
	init_indice(bit_load_address, 0, 32);
	init_indice(bit_store_data, 0, 32);
	init_indice(bit_store_address, 0, 32);
	init_indice(bit_result_tensor, 0, 32);
	init_indice(bit_pc_4, 0, 32);
	copy_indice(next_priviledge, 0, this_priviledge, 0, 2);

	//pc + 4
	uint32_t number_pc_unsigned = cvt_bit_to_number_unsigned(bit_this_pc, 32);
	uint32_t number_pc_4 = number_pc_unsigned + 4;
	cvt_number_to_bit_unsigned(bit_pc_4, number_pc_4, 32);
	copy_indice(bit_next_pc, 0, bit_pc_4, 0, 32);

	//split instruction
	bool bit_op_code[7]; //25-31
	bool rd_code[5]; //20-24
	bool rs_a_code[5]; //12-16
	bool rs_b_code[5]; //7-11
	copy_indice(bit_op_code, 0, instruction, 25, 7);
	copy_indice(rd_code, 0, instruction, 20, 5);
	copy_indice(rs_a_code, 0, instruction, 12, 5);
	copy_indice(rs_b_code, 0, instruction, 7, 5);

	//准备寄存器
	uint32_t reg_d_index = cvt_bit_to_number_unsigned(rd_code, 5);
	uint32_t reg_a_index = cvt_bit_to_number_unsigned(rs_a_code, 5);
	uint32_t reg_b_index = cvt_bit_to_number_unsigned(rs_b_code, 5);
	bool	bit_reg_data_a[32];
	bool	bit_reg_data_b[32];
	copy_indice(bit_reg_data_a, 0, general_regs, 32*reg_a_index, 32);
	copy_indice(bit_reg_data_b, 0, general_regs, 32*reg_b_index, 32);


	bool bit_funct5[5];
	copy_indice(bit_funct5, 0, instruction, 0, 5);
	uint32_t number_funct5_unsigned = cvt_bit_to_number_unsigned(bit_funct5, 5);
	switch(number_funct5_unsigned) {
		case 0:{ //amoadd.w
			copy_indice(bit_load_address, 0, bit_reg_data_a, 0, 32);
			copy_indice(next_general_regs, reg_d_index*32, bit_load_data, 0, 32);
			copy_indice(bit_store_address, 0, bit_load_address, 0, 32);
			add_bit_list(bit_store_data, bit_load_data, bit_reg_data_b, 32);
			break;
		}
		case 1:{ //amoswap.w
			copy_indice(bit_load_address, 0, bit_reg_data_a, 0, 32);
			copy_indice(next_general_regs, reg_d_index*32, bit_load_data, 0, 32);
			copy_indice(bit_store_address, 0, bit_load_address, 0, 32);
			copy_indice(bit_store_data, 0, bit_reg_data_b, 0, 32);
			break;
		}
		case 2:{ //lr.w
			copy_indice(bit_load_address, 0, bit_reg_data_a, 0, 32);
			copy_indice(next_general_regs, reg_d_index*32, bit_load_data, 0, 32);
			break;
		}
		case 3:{ //sc.w
			copy_indice(bit_store_address, 0, bit_reg_data_a, 0, 32);
			copy_indice(bit_store_data, 0, bit_reg_data_b, 0, 32);
			// 默认存入成功
			init_indice(next_general_regs, reg_d_index*32, 32);
			break;
		}
		case 4:{ //amoxor.w
			copy_indice(bit_load_address, 0, bit_reg_data_a, 0, 32);
			copy_indice(next_general_regs, reg_d_index*32, bit_load_data, 0, 32);
			copy_indice(bit_store_address, 0, bit_load_address, 0, 32);
			for (int i = 0; i < 32; i++)
				bit_store_data[i] = bit_load_data[i] ^ bit_reg_data_b[i];
			break;
		}
		case 8:{ //amoor.w
			copy_indice(bit_load_address, 0, bit_reg_data_a, 0, 32);
			copy_indice(next_general_regs, reg_d_index*32, bit_load_data, 0, 32);
			copy_indice(bit_store_address, 0, bit_load_address, 0, 32);
			for (int i = 0; i < 32; i++)
				bit_store_data[i] = bit_load_data[i] | bit_reg_data_b[i];
			break;
		}
		case 12:{ //amoand.w
			copy_indice(bit_load_address, 0, bit_reg_data_a, 0, 32);
			copy_indice(next_general_regs, reg_d_index*32, bit_load_data, 0, 32);
			copy_indice(bit_store_address, 0, bit_load_address, 0, 32);
			for (int i = 0; i < 32; i++)
				bit_store_data[i] = bit_load_data[i] & bit_reg_data_b[i];
			break;
		}
		case 16:{ //amomin.w
			copy_indice(bit_load_address, 0, bit_reg_data_a, 0, 32);
			copy_indice(next_general_regs, reg_d_index*32, bit_load_data, 0, 32);
			copy_indice(bit_store_address, 0, bit_load_address, 0, 32);
			int number_temp = cvt_bit_to_number(bit_load_data, 32);
			int number_reg_data_b = cvt_bit_to_number(bit_reg_data_b, 32);
			if (number_reg_data_b < number_temp)
				copy_indice(bit_store_data, 0, bit_reg_data_b, 0, 32);
			else
				copy_indice(bit_store_data, 0, bit_load_data, 0, 32);
			break;
		}
		case 20:{ //amomax.w
			copy_indice(bit_load_address, 0, bit_reg_data_a, 0, 32);
			copy_indice(next_general_regs, reg_d_index*32, bit_load_data, 0, 32);
			copy_indice(bit_store_address, 0, bit_load_address, 0, 32);
			int number_temp = cvt_bit_to_number(bit_load_data, 32);
			int number_reg_data_b = cvt_bit_to_number(bit_reg_data_b, 32);
			if (number_reg_data_b > number_temp)
				copy_indice(bit_store_data, 0, bit_reg_data_b, 0, 32);
			else
				copy_indice(bit_store_data, 0, bit_load_data, 0, 32);
			break;
		}
		case 24:{ //amominu.w
			copy_indice(bit_load_address, 0, bit_reg_data_a, 0, 32);
			copy_indice(next_general_regs, reg_d_index*32, bit_load_data, 0, 32);
			copy_indice(bit_store_address, 0, bit_load_address, 0, 32);
			uint32_t number_temp_unsigned = cvt_bit_to_number_unsigned(bit_load_data, 32);
			uint32_t number_reg_data_b_unsigned = cvt_bit_to_number_unsigned(bit_reg_data_b, 32);
			if (number_reg_data_b_unsigned < number_temp_unsigned)
				copy_indice(bit_store_data, 0, bit_reg_data_b, 0, 32);
			else
				copy_indice(bit_store_data, 0, bit_load_data, 0, 32);
			break;
		}
		case 28:{ //amomaxu.w
			copy_indice(bit_load_address, 0, bit_reg_data_a, 0, 32);
			copy_indice(next_general_regs, reg_d_index*32, bit_load_data, 0, 32);
			copy_indice(bit_store_address, 0, bit_load_address, 0, 32);
			uint32_t number_temp_unsigned = cvt_bit_to_number_unsigned(bit_load_data, 32);
			uint32_t number_reg_data_b_unsigned = cvt_bit_to_number_unsigned(bit_reg_data_b, 32);
			if (number_reg_data_b_unsigned > number_temp_unsigned)
				copy_indice(bit_store_data, 0, bit_reg_data_b, 0, 32);
			else
				copy_indice(bit_store_data, 0, bit_load_data, 0, 32);
			break;
		}
        default: {
            break;
        }
    }

	// output data
	init_indice(next_general_regs, 0, 32);
    copy_indice(output_data, 0, next_general_regs, 0, 1024);
    copy_indice(output_data, 1024, next_reg_csrs, 0, 32*21);
	copy_indice(output_data, 1696, bit_next_pc, 0, 32);
	copy_indice(output_data, 1728, bit_load_address, 0, 32);
	copy_indice(output_data, 1760, bit_store_data, 0, 32);
	copy_indice(output_data, 1792, bit_store_address, 0, 32);
	copy_indice(output_data, 1824, next_priviledge, 0, 2);
}
