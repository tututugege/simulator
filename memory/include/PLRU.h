#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

// =========================================================
// 参数配置 (Configurable Parameters)
// =========================================================
// 修改这里可以改变关联度 (必须是 2 的幂: 4, 8, 16, 32...)
#define NUM_WAYS    8          
#define NUM_SETS    4          

// 自动计算 PLRU 树所需的位数 (N Ways 需要 N-1 bits)
#define TREE_BITS   (NUM_WAYS - 1)

// 选择存储类型，如果 TREE_BITS > 64，需要修改这里
typedef uint64_t tree_storage_t; 

// =========================================================
// 数据结构定义
// =========================================================

typedef struct {
    bool            rst_n;
    bool            req_valid;
    uint32_t        set_idx;
    
    // Update 接口
    bool            update_en;
    uint32_t        access_way;
    
    // Pending Mask: 每一位对应一个 Way (1=Pending/Busy)
    // 使用 uint64_t 以支持最多 64 Ways
    uint64_t        pending_mask; 
} input_signals_t;

typedef struct {
    uint32_t        victim_way;
    bool            all_pending;
} output_signals_t;

typedef struct {
    // 每个 Set 一个 PLRU 树
    tree_storage_t  plru_tree[NUM_SETS]; 
} state_t;

// =========================================================
// 辅助函数 (Helper Logic)
// =========================================================

// 检查某个范围内的 Way 是否全部被 Pending 掩盖
// 对应硬件: 局部 AND 门聚合逻辑
bool is_range_blocked(uint64_t mask, int start_way, int num_ways) {
    uint64_t range_mask;
    if (num_ways >= 64) range_mask = ~0ULL;
    else range_mask = (1ULL << num_ways) - 1;
    
    range_mask <<= start_way;
    
    // 检查 mask 中对应的位是否全为 1
    return (mask & range_mask) == range_mask;
}

// 获取树中某一位的状态
int get_bit(tree_storage_t tree, int index) {
    return (tree >> index) & 1;
}

// 设置树中某一位的状态
void set_bit(tree_storage_t *tree, int index, int val) {
    if (val) *tree |= (1ULL << index);
    else     *tree &= ~(1ULL << index);
}

// =========================================================
// 组合逻辑 (Combinational Logic)
// =========================================================
/**
 * eval_comb:
 * 1. 遍历 PLRU 树寻找 Victim (Pending-Aware)
 * 2. 计算 Update 后的新树状态
 */
void eval_comb(const input_signals_t *in, const state_t *curr, state_t *next, output_signals_t *out) {
    // Latch 预防：默认下一态等于当前态
    *next = *curr;
    
    // 复位逻辑预处理 (实际复位在 seq 块，但 comb 需反映复位值)
    if (!in->rst_n) {
        memset(next->plru_tree, 0, sizeof(next->plru_tree));
        out->victim_way = 0;
        out->all_pending = false;
        return;
    }

    tree_storage_t tree = curr->plru_tree[in->set_idx];

    // ---------------------------------------------------------
    // 1. Victim Selection (通用 N-Way 树遍历)
    // ---------------------------------------------------------
    // 我们从 Root (node 0) 开始，向下遍历直到叶子节点 (Way)
    // 对应硬件：多级 Mux 级联选择器
    
    int current_node = 0;
    int current_scope = NUM_WAYS; // 当前节点覆盖的 Way 数量
    int base_way = 0;             // 当前节点覆盖的起始 Way
    bool traverse_valid = true;

    // 循环次数 = log2(NUM_WAYS). 
    // 对于 8-way, scope 变化: 8 -> 4 -> 2 -> 1 (结束)
    while (current_scope > 1) {
        int half_scope = current_scope / 2;
        
        // 左子树范围: [base_way, base_way + half_scope - 1]
        // 右子树范围: [base_way + half_scope, base_way + current_scope - 1]
        
        bool left_blocked = is_range_blocked(in->pending_mask, base_way, half_scope);
        bool right_blocked = is_range_blocked(in->pending_mask, base_way + half_scope, half_scope);

        int plru_dir = get_bit(tree, current_node); // 0=Left, 1=Right (指向 Victim 候选)
        int go_right = 0;

        if (left_blocked && right_blocked) {
            traverse_valid = false;
            break; // 全部堵死
        } else if (left_blocked) {
            go_right = 1; // 左边堵死，被迫去右边
        } else if (right_blocked) {
            go_right = 0; // 右边堵死，被迫去左边
        } else {
            go_right = plru_dir; // 均未堵死，遵循 PLRU 算法
        }

        // 状态转移到下一层
        if (go_right) {
            current_node = 2 * current_node + 2; // Right Child Index
            base_way += half_scope;
        } else {
            current_node = 2 * current_node + 1; // Left Child Index
        }
        current_scope = half_scope;
    }

    if (!traverse_valid) {
        out->all_pending = true;
        out->victim_way = 0; // Invalid
    } else {
        out->all_pending = false;
        out->victim_way = base_way;
    }

    // ---------------------------------------------------------
    // 2. PLRU Update Logic (通用 N-Way 路径更新)
    // ---------------------------------------------------------
    if (in->req_valid && in->update_en) {
        tree_storage_t new_tree = tree;
        uint32_t way = in->access_way;
        
        // 从 Root 开始更新直到叶子
        // 规则：访问了某一边，就将节点指向另一边 (Point Away)
        
        int u_node = 0;
        int u_scope = NUM_WAYS;
        int u_base = 0;

        while (u_scope > 1) {
            int u_half = u_scope / 2;
            int split_point = u_base + u_half;

            if (way < split_point) {
                // Access 落在左子树 -> 节点应指向右 (1)
                set_bit(&new_tree, u_node, 1);
                // 下一步进入左子节点
                u_node = 2 * u_node + 1;
                // u_base 不变
            } else {
                // Access 落在右子树 -> 节点应指向左 (0)
                set_bit(&new_tree, u_node, 0);
                // 下一步进入右子节点
                u_node = 2 * u_node + 2;
                u_base += u_half;
            }
            u_scope = u_half;
        }
        
        next->plru_tree[in->set_idx] = new_tree;
    }
}

// =========================================================
// 时序逻辑 (Sequential Logic)
// =========================================================
void update_seq(const input_signals_t *in, state_t *curr, const state_t *next) {
    if (!in->rst_n) {
        memset(curr->plru_tree, 0, sizeof(curr->plru_tree));
    } else {
        *curr = *next;
    }
}

// =========================================================
// Testbench
// =========================================================
uint64_t g_clk = 0;

void step(input_signals_t *in, state_t *curr, state_t *next, output_signals_t *out) {
    eval_comb(in, curr, next, out);
    update_seq(in, curr, next);
    g_clk++;
}

int main() {
    input_signals_t inp = {0};
    output_signals_t outp = {0};
    state_t curr = {0};
    state_t next = {0};

    // 校验配置
    // NUM_WAYS 必须是 2 的幂
    assert((NUM_WAYS & (NUM_WAYS - 1)) == 0);

    printf("--- Configurable PLRU Simulator ---\n");
    printf("Ways: %d (Bits per tree: %d)\n", NUM_WAYS, TREE_BITS);
    printf("Sets: %d\n\n", NUM_SETS);

    // 1. Reset
    inp.rst_n = 0;
    step(&inp, &curr, &next, &outp);
    inp.rst_n = 1;
    printf("[Cycle %ld] Reset Done. Tree[0]=0x%llX\n", g_clk, (unsigned long long)curr.plru_tree[0]);

    // 2. 连续填充 Ways，观察 Victim 变化 (Basic PLRU functionality)
    // 目标：连续访问 Way 0, 1, 2... 观察树如何尽量避免覆盖它们
    printf("\n--- Test 1: Fill Sequence (MRU Update) ---\n");
    for (int i = 0; i < NUM_WAYS; i++) {
        inp.req_valid = 1;
        inp.update_en = 1;
        inp.access_way = i; 
        inp.pending_mask = 0;
        
        // 为了观察组合逻辑输出的 Victim (在 Update 之前)，我们先 eval 一次不带 update
        input_signals_t probe = inp;
        probe.update_en = 0; 
        output_signals_t probe_out;
        state_t probe_next;
        eval_comb(&probe, &curr, &probe_next, &probe_out);

        // 执行真正的 Update Step
        step(&inp, &curr, &next, &outp);

        printf("Access Way %d | Prev Victim Recommendation: %d | New Tree: 0x%llX\n", 
               i, probe_out.victim_way, (unsigned long long)curr.plru_tree[0]);
    }

    // 3. Pending-Aware 功能测试
    printf("\n--- Test 2: Pending-Aware Logic (Ways=%d) ---\n", NUM_WAYS);
    
    // 假设 Way 0 到 Way (NUM_WAYS/2 - 1) 全部 Pending (左半边全堵)
    // 正常 PLRU 可能想选左边的某个 Way，但必须被强制去右边
    
    // 重置状态
    inp.rst_n = 0; step(&inp, &curr, &next, &outp); inp.rst_n = 1;

    // 让 PLRU 指向左边 (通过大量访问右边实现)
    for (int i = NUM_WAYS/2; i < NUM_WAYS; i++) {
        inp.req_valid = 1; inp.update_en = 1; inp.access_way = i;
        step(&inp, &curr, &next, &outp);
    }
    printf("Tree initialized to point Left (Tree: 0x%llX)\n", (unsigned long long)curr.plru_tree[0]);

    // 构造 Mask: 左半边全部为 1
    uint64_t left_half_mask = (1ULL << (NUM_WAYS/2)) - 1;
    
    inp.req_valid = 1;
    inp.update_en = 0; // 仅查询 Victim
    inp.pending_mask = left_half_mask;

    eval_comb(&inp, &curr, &next, &outp);
    
    printf("Pending Mask: 0x%llX (Left half blocked)\n", (unsigned long long)inp.pending_mask);
    printf("Selected Victim: %d\n", outp.victim_way);
    
    if (outp.victim_way >= NUM_WAYS/2) {
        printf("RESULT: SUCCESS (Victim shifted to Right half)\n");
    } else {
        printf("RESULT: FAILURE (Victim stuck in blocked Left half)\n");
    }

    // 4. All Pending 测试
    printf("\n--- Test 3: All Pending ---\n");
    inp.pending_mask = ~0ULL; // 全 1
    if (NUM_WAYS < 64) inp.pending_mask = (1ULL << NUM_WAYS) - 1;

    eval_comb(&inp, &curr, &next, &outp);
    printf("Pending Mask: 0x%llX\n", (unsigned long long)inp.pending_mask);
    printf("All Pending Flag: %s\n", outp.all_pending ? "TRUE" : "FALSE");

    return 0;
}