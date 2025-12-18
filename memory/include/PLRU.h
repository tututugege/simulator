#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define NUM_SETS    4        // Set数量
#define NUM_WAYS    4        // Way数量 (关联度)
#define TREE_BITS   (NUM_WAYS - 1) // PLRU树所需的比特数 (N-1)


// 模拟 Verilog 的 Input 端口
typedef struct {
    bool     rst_n;             // Active low reset
    bool     req_valid;         // 请求有效
    uint32_t set_idx;           // 目标 Set 索引
    
    // Update/Access 信号
    bool     update_en;         // 是否更新PLRU状态 (Hit 或 Refill时)
    uint32_t access_way;        // 命中的 Way 或 分配的 Way
    
    // Pending-Aware 核心信号
    // Bitmask: 1 表示该 Way 正在处理 Miss (Pending), 0 表示可用
    uint32_t pending_mask;      
} input_signals_t;

// 模拟 Verilog 的 Output 端口
typedef struct {
    uint32_t victim_way;        // 组合逻辑计算出的 Victim
    bool     all_pending;       // 如果所有 Way 都 Pending，无法替换
} output_signals_t;

// 模拟 寄存器 (Registers / Flip-Flops)
typedef struct {
    // 每一个 Set 都有一个 PLRU 树状态
    // 对于 4-Way, 需要 3 bits. 我们用 uint8_t 存储
    uint8_t plru_tree[NUM_SETS]; 
} state_t;

// 全局时钟计数器
uint64_t g_clk = 0;

// =========================================================
// 核心逻辑函数 (Core Logic)
// =========================================================

// [辅助函数] 打印二进制
void print_bin(uint32_t n, int bits) {
    for (int i = bits - 1; i >= 0; i--) {
        printf("%d", (n >> i) & 1);
    }
}

/**
 * 组合逻辑 (Combinational Logic)
 * 对应 Verilog: always_comb / assign
 * 功能：
 * 1. 根据当前 PLRU 状态和 Pending Mask 计算 Victim Way。
 * 2. 如果 update_en 有效，计算 PLRU 树的 Next State。
 */
void eval_comb(const input_signals_t *in, const state_t *curr, state_t *next, output_signals_t *out) {
    // 默认 Next State 保持 Current State (Latch prevention in simulation logic)
    *next = *curr;
    
    // 初始化输出
    out->victim_way = 0;
    out->all_pending = false;

    if (!in->rst_n) {
        // 复位逻辑在 seq 块处理，但组合逻辑需知道复位会清零 next
        memset(next->plru_tree, 0, sizeof(next->plru_tree));
        return;
    }

    // ---------------------------------------------------------
    // 1. Pending-Aware Victim Selection Logic (读操作)
    // ---------------------------------------------------------
    // 获取当前 Set 的 PLRU 状态 bits
    uint8_t tree = curr->plru_tree[in->set_idx];
    
    // 解构 PLRU bits (针对 4-Way)
    // Bit 0: Root (0->Left, 1->Right)
    // Bit 1: Left Child (0->Way0, 1->Way1)
    // Bit 2: Right Child (0->Way2, 1->Way3)
    int b0 = (tree >> 0) & 1;
    int b1 = (tree >> 1) & 1;
    int b2 = (tree >> 2) & 1;

    // Pending Mask 检查
    // mask & 0x3 是左子树 (Way 0,1), mask & 0xC 是右子树 (Way 2,3)
    bool left_blocked  = ((in->pending_mask & 0x3) == 0x3); 
    bool right_blocked = ((in->pending_mask & 0xC) == 0xC);

    // 决策逻辑：
    // 标准 PLRU 逻辑是根据 b0 走。
    // Pending-Aware 逻辑：如果 b0 指向的方向全被 blocked，必须翻转方向。
    
    int go_right = 0;

    if (left_blocked && right_blocked) {
        out->all_pending = true; // 无路可走
        out->victim_way = 0;     // 默认值，外部应处理 Stall
    } else if (left_blocked) {
        go_right = 1; // 左边堵死，强行向右
    } else if (right_blocked) {
        go_right = 0; // 右边堵死，强行向左
    } else {
        go_right = b0; // 都没有完全堵死，遵循 PLRU 指针（b0=0指左，b0=1指右？通常0指向MRU，这里假设bit指向Victim方向）
        // 约定：Bit=0 表示指引我们要去 Way 0/1 (Left) 找 Victim，意味着 Right 是最近刚被访问过的。
    }

    if (go_right) {
        // 进入右子树 (Ways 2,3)
        // 检查 Way 2 是否 pending
        bool w2_pending = (in->pending_mask >> 2) & 1;
        // 如果 b2 指向 Way 2，但 Way 2 pending，则选 Way 3
        if (w2_pending) out->victim_way = 3;
        else if (((in->pending_mask >> 3) & 1)) out->victim_way = 2; // Way 3 pending
        else out->victim_way = (b2 == 0) ? 2 : 3; // 标准 b2 选择
    } else {
        // 进入左子树 (Ways 0,1)
        bool w0_pending = (in->pending_mask >> 0) & 1;
        if (w0_pending) out->victim_way = 1;
        else if (((in->pending_mask >> 1) & 1)) out->victim_way = 0;
        else out->victim_way = (b1 == 0) ? 0 : 1;
    }

    // ---------------------------------------------------------
    // 2. PLRU State Update Logic (写操作)
    // ---------------------------------------------------------
    if (in->req_valid && in->update_en) {
        // 更新规则：当访问 Way X 时，将路径上的节点 bits 指向 "背离" X 的方向。
        // 这让 X 成为 MRU，指针指向其他地方（LRU候选）。
        
        uint32_t way = in->access_way;
        uint8_t new_tree = tree;

        // 更新 Root (Bit 0)
        // 如果访问 Way 0/1 (Left)，Bit 0 设为 1 (指向 Right)
        // 如果访问 Way 2/3 (Right)，Bit 0 设为 0 (指向 Left)
        if (way < 2) new_tree |= (1 << 0);
        else         new_tree &= ~(1 << 0);

        // 更新 Child Nodes
        if (way < 2) { 
            // 访问左侧，更新 Bit 1
            // 访问 Way 0 -> Bit 1 设为 1 (指 Way 1)
            // 访问 Way 1 -> Bit 1 设为 0 (指 Way 0)
            if (way == 0) new_tree |= (1 << 1);
            else          new_tree &= ~(1 << 1);
        } else {
            // 访问右侧，更新 Bit 2
            // 访问 Way 2 -> Bit 2 设为 1 (指 Way 3)
            // 访问 Way 3 -> Bit 2 设为 0 (指 Way 2)
            if (way == 2) new_tree |= (1 << 2);
            else          new_tree &= ~(1 << 2);
        }

        next->plru_tree[in->set_idx] = new_tree;
    }
}

/**
 * 时序逻辑 (Sequential Logic)
 * 对应 Verilog: always @(posedge clk or negedge rst_n)
 */
void update_seq(const input_signals_t *in, state_t *curr, const state_t *next) {
    if (!in->rst_n) {
        // 异步复位：所有 PLRU bits 归零
        memset(curr->plru_tree, 0, sizeof(curr->plru_tree));
    } else {
        // 时钟沿：更新状态
        *curr = *next;
    }
}

// =========================================================
// 测试平台 (Testbench)
// =========================================================
int main() {
    // 实例化信号和寄存器
    input_signals_t inp = {0};
    output_signals_t outp = {0};
    state_t curr_state = {0};
    state_t next_state = {0};

    printf("--- Starting Pending-Aware PLRU Simulation ---\n");
    printf("Config: %d Sets, %d Ways\n\n", NUM_SETS, NUM_WAYS);

    // -----------------------------------------------------
    // Cycle 0: Reset
    // -----------------------------------------------------
    inp.rst_n = 0;
    eval_comb(&inp, &curr_state, &next_state, &outp);
    update_seq(&inp, &curr_state, &next_state);
    g_clk++;
    printf("[Cycle %ld] Reset applied. PLRU State: 0x%02X\n", g_clk, curr_state.plru_tree[0]);

    // 释放复位
    inp.rst_n = 1;
    inp.set_idx = 0; // 始终测试 Set 0

    // -----------------------------------------------------
    // Scenario 1: Fill Ways 0, 1, 2 (Make them MRU)
    // -----------------------------------------------------
    // 访问顺序: 0 -> 1 -> 2. 
    // 预期 PLRU 指向 Way 3.
    
    int access_seq[] = {0, 1, 2};
    for (int i = 0; i < 3; i++) {
        inp.req_valid = 1;
        inp.update_en = 1;
        inp.access_way = access_seq[i];
        inp.pending_mask = 0; // 无 pending

        eval_comb(&inp, &curr_state, &next_state, &outp);
        update_seq(&inp, &curr_state, &next_state);
        g_clk++;

        printf("[Cycle %ld] Access Way %d. Tree: ", g_clk, access_seq[i]);
        print_bin(curr_state.plru_tree[0], 3); 
        printf(" (Victim would be: %d)\n", outp.victim_way);
    }

    // 此时状态分析：
    // Way 0 访问 -> Bit0=1, Bit1=1
    // Way 1 访问 -> Bit0=1, Bit1=0
    // Way 2 访问 -> Bit0=0, Bit2=1
    // 最终 Tree 应该是 0x05 (Binary 101) -> Bit0=1(R), Bit1=0(W0), Bit2=1(W3)
    // 实际上 PLRU 逻辑：
    // 访问 Way 2 后，Bit 0 变为 0 (指向 Left: Way 0/1) ??? 
    // 等等，MRU update 逻辑是 point AWAY.
    // Access 0 -> Points to 1.
    // Access 1 -> Points to 0. (Set Left group saturated, root points Right)
    // Access 2 -> Points to 3. (Set Right group, root points Left)
    // 所以现在的 Victim 应该是 Way 0 或 1 (取决于上次 Left 内部的状态)。
    // 让我们看看代码输出。

    // -----------------------------------------------------
    // Scenario 2: Pending-Aware Test
    // -----------------------------------------------------
    printf("\n--- Scenario 2: Pending Mask Override ---\n");
    
    // 假设现在的 PLRU 逻辑指向 Way 0 作为 Victim (这是基于 LRU 的选择)
    // 我们强制 Way 0 为 Pending (比如正在 refill)，看看是否会选 Way 1 或其他。
    
    inp.req_valid = 1;
    inp.update_en = 0; // 这是一个查找 Victim 的操作，不更新状态
    inp.pending_mask = (1 << 0); // Way 0 is Pending!

    eval_comb(&inp, &curr_state, &next_state, &outp);
    // 注意：这里没有 update_seq，因为只是组合逻辑查询

    printf("[Cycle %ld+Comb] PLRU points to Way %d (Standard). Pending Mask: ", g_clk, outp.victim_way); // 这里打印的是经过修正的
    print_bin(inp.pending_mask, 4);
    printf("\n");
    
    // 为了验证，我们手动打印如果 Mask 为 0 的结果
    input_signals_t debug_inp = inp;
    debug_inp.pending_mask = 0;
    output_signals_t debug_out;
    state_t debug_next;
    eval_comb(&debug_inp, &curr_state, &debug_next, &debug_out);
    printf("    -> Without Pending Mask, Victim is: Way %d\n", debug_out.victim_way);
    printf("    -> With Pending Mask (Way 0 busy), Victim is: Way %d\n", outp.victim_way);

    // -----------------------------------------------------
    // Scenario 3: Entire Left Subtree Pending
    // -----------------------------------------------------
    printf("\n--- Scenario 3: Left Subtree (0 & 1) Pending ---\n");
    // 强制 PLRU 指向左边 (通过访问 Way 2 和 Way 3)
    inp.pending_mask = 0;
    inp.update_en = 1;
    inp.access_way = 2; eval_comb(&inp, &curr_state, &next_state, &outp); update_seq(&inp, &curr_state, &next_state);
    inp.access_way = 3; eval_comb(&inp, &curr_state, &next_state, &outp); update_seq(&inp, &curr_state, &next_state);
    
    // 此时 Bit 0 应该指向 Left (0)
    printf("Current Tree: "); print_bin(curr_state.plru_tree[0], 3); printf(" (Points to Left)\n");

    // 此时 Way 0 和 Way 1 都 Pending
    inp.update_en = 0;
    inp.pending_mask = (1<<0) | (1<<1); 
    
    eval_comb(&inp, &curr_state, &next_state, &outp);
    printf("Pending Mask: 0011. Forced Victim: Way %d (Should be 2 or 3)\n", outp.victim_way);

    return 0;
}