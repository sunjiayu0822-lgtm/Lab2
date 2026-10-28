#include "defuser.h"

#include <inttypes.h>
#include <stdio.h>

#define MAX_FIELDS 8

/*
 * 把 values 中的 count 个整数写成一行“空格分隔的无符号十进制整数”，存入 out。
 * 成功返回 0；count 为 0（还没有实现）、snprintf 出错或结果被截断时返回非零。
 */
static int write_answer(char *out, size_t cap, const uint32_t *values, size_t count) {
    size_t used = 0;
    for (size_t i = 0; i < count; ++i) {
        int size = snprintf(out + used, cap - used, "%s%" PRIu32, i ? " " : "", values[i]);
        if (size < 0 || (size_t)size >= cap - used) return 1;
        used += (size_t)size;
    }
    return count == 0;
}

/* 32 位循环移位，对应 x86 的 ror/rol。 */
static uint32_t ror32(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }
static uint32_t rol32(uint32_t x, unsigned n) { return (x << n) | (x >> (32 - n)); }

/*
 * phase_01 -> p1_compute(challenge)。
 * low  = (uint32_t)challenge
 * mid  = (uint32_t)(challenge >> 16)
 * high = (uint32_t)(challenge >> 32)
 * p1_step1(high, mid)  => A = (5*high) ^ (mid + 0xa1ba)
 * p1_step0(low, high)  => B = (high + 0xf7e9) ^ low
 * p1_step2(B, A)       => (3*B + A) ^ 0x86a9
 */
int solve_phase_1(uint64_t challenge, char *out, size_t cap) {
    uint32_t values[MAX_FIELDS] = {0};
    uint32_t low = (uint32_t)challenge;
    uint32_t mid = (uint32_t)(challenge >> 16);
    uint32_t high = (uint32_t)(challenge >> 32);
    uint32_t a = (high * 5) ^ (mid + 0xa1ba);
    uint32_t b = (high + 0xf7e9) ^ low;
    values[0] = (b * 3 + a) ^ 0x86a9;
    return write_answer(out, cap, values, 1);
}

/*
 * phase_02 -> 六个数的递推。
 * v[0] = ((challenge >> 38) & 0xff) * 2 + 8
 * v[i] = (v[i-1] + i) * 5 - ((challenge >> (23+i)) & 0x1f),  i = 1..5
 */
int solve_phase_2(uint64_t challenge, char *out, size_t cap) {
    uint32_t values[MAX_FIELDS] = {0};
    values[0] = ((uint32_t)(challenge >> 38) & 0xff) * 2 + 8;
    for (unsigned i = 1; i < 6; ++i) {
        values[i] = (values[i - 1] + i) * 5 - ((uint32_t)(challenge >> (23 + i)) & 0x1f);
    }
    return write_answer(out, cap, values, 6);
}

/*
 * phase_03 -> switch 跳转表 p3_dispatch(idx, mix)。
 * idx = challenge & 7
 * mix = (uint32_t)(challenge >> 3) ^ (uint32_t)(challenge >> 32)
 */
static uint32_t p3_dispatch(uint32_t idx, uint32_t x) {
    switch (idx) {
        case 0: return x * 5 + 0x24ac7;
        case 1: return (x * 4) ^ 0xb3fc;
        case 2: return x * 5 + 0x3c2fd;
        case 3: return x * 3 + 0xb1f0;
        case 4: return x * 5 + 0x4c1b;
        case 5: return (x ^ 0xfa58) + (x >> 2);
        case 6: return (x * 2) ^ 0xa400;
        default: return (x ^ 0xfff1) + (x >> 1); /* idx == 7 */
    }
}

int solve_phase_3(uint64_t challenge, char *out, size_t cap) {
    uint32_t values[MAX_FIELDS] = {0};
    uint32_t idx = (uint32_t)challenge & 7;
    uint32_t mix = ((uint32_t)(challenge >> 3)) ^ ((uint32_t)(challenge >> 32));
    values[0] = idx;
    values[1] = p3_dispatch(idx, mix);
    return write_answer(out, cap, values, 2);
}

/*
 * phase_04 -> 二分递归 p4_walk(target, 0, 127, mix)。
 * target = challenge & 0x7f
 * mix    = (uint32_t)(challenge >> 7) ^ (uint32_t)(challenge >> 32)
 */
static uint32_t p4_walk(uint32_t target, uint32_t low, uint32_t high, uint32_t mix) {
    uint32_t middle = low + (high - low) / 2;
    if (middle == target) return (middle + 0x96) ^ mix;
    if (target < middle) {
        return (p4_walk(target, low, middle - 1, mix) * 5 ^ (middle + mix)) ^ 0xe;
    }
    return (p4_walk(target, middle + 1, high, mix) * 5 ^ (middle + mix)) ^ 0x5;
}

int solve_phase_4(uint64_t challenge, char *out, size_t cap) {
    uint32_t values[MAX_FIELDS] = {0};
    uint32_t target = (uint32_t)challenge & 0x7f;
    uint32_t mix = ((uint32_t)(challenge >> 7)) ^ ((uint32_t)(challenge >> 32));
    values[0] = p4_walk(target, 0, 127, mix);
    return write_answer(out, cap, values, 1);
}

/*
 * phase_05 -> 链表遍历 p5_traverse(challenge, &tag)。
 * 节点表按 .rodata 中的实际内存顺序排布（0x405ba0 起，每项 16 字节）。
 * 入口指针表 0x405b60 指向 8 个起始节点。
 */
struct p5_node { uint32_t value, tag; int next; };

static const struct p5_node p5_nodes[12] = {
    {0x7baf, 0x0541, 1},   /* 0: 0x405ba0 */
    {0x868e, 0x0968, 9},   /* 1: 0x405bb0 */
    {0x9446, 0x0304, -1},  /* 2: 0x405bc0 (NULL) */
    {0x9296, 0x0181, 11},  /* 3: 0x405bd0 */
    {0x4642, 0x0a88, 3},   /* 4: 0x405be0 */
    {0x2dfe, 0x0755, 6},   /* 5: 0x405bf0 */
    {0x5b6c, 0x003f, 8},   /* 6: 0x405c00 */
    {0xeca4, 0x0674, 4},   /* 7: 0x405c10 */
    {0xb143, 0x0249, 7},   /* 8: 0x405c20 */
    {0x54c3, 0x0b86, 5},   /* 9: 0x405c30 */
    {0xb860, 0x0837, 2},   /* 10: 0x405c40 */
    {0xec72, 0x0450, 10},  /* 11: 0x405c50 */
};

static const int p5_entry[8] = {0, 1, 9, 5, 6, 8, 7, 4};

int solve_phase_5(uint64_t challenge, char *out, size_t cap) {
    uint32_t values[MAX_FIELDS] = {0};
    int node = p5_entry[(uint32_t)challenge & 7];
    uint32_t count = (uint32_t)(challenge >> 3) & 3;
    uint32_t mix = ((uint32_t)(challenge >> 6)) ^ ((uint32_t)(challenge >> 32));
    uint32_t acc = (3 * (mix ^ 0x4388) + p5_nodes[node].value) ^ p5_nodes[node].tag;
    uint32_t tag = p5_nodes[node].tag;
    for (uint32_t i = 0; i < count; ++i) {
        node = p5_nodes[node].next;
        acc = (acc * 3 + p5_nodes[node].value) ^ p5_nodes[node].tag;
        tag = p5_nodes[node].tag;
    }
    values[0] = acc;
    values[1] = tag;
    return write_answer(out, cap, values, 2);
}

/*
 * phase_06 -> 函数指针表间接调用 p6_dispatch(challenge)。
 * mix = (uint32_t)(challenge >> 6) ^ (uint32_t)(challenge >> 32)
 * r1  = p6_ops[challenge & 7](mix) ^ 0x9ac9
 * 结果 = p6_ops[(challenge >> 3) & 7](r1) + 0x8ce4
 */
static uint32_t p6_op_0(uint32_t x) { return ror32(x, 2); }
static uint32_t p6_op_1(uint32_t x) { return ((x << 7) + 0x2cf4) ^ (x * 7); }
static uint32_t p6_op_2(uint32_t x) { return rol32(x, 3) ^ 0x4eec; }
static uint32_t p6_op_3(uint32_t x) { return (x + 0xfbc3) ^ (x >> 5); }
static uint32_t p6_op_4(uint32_t x) { return (x ^ 0x2fd4) + (x << 7); }
static uint32_t p6_op_5(uint32_t x) { return ((x >> 5) ^ x) + 0x9cdb; }

static uint32_t (*const p6_ops[8])(uint32_t) = {
    p6_op_2, p6_op_1, p6_op_3, p6_op_5,
    p6_op_2, p6_op_4, p6_op_0, p6_op_5,
};

int solve_phase_6(uint64_t challenge, char *out, size_t cap) {
    uint32_t values[MAX_FIELDS] = {0};
    uint32_t mix = ((uint32_t)(challenge >> 6)) ^ ((uint32_t)(challenge >> 32));
    uint32_t r1 = p6_ops[(uint32_t)challenge & 7](mix) ^ 0x9ac9;
    values[0] = p6_ops[((uint32_t)challenge >> 3) & 7](r1) + 0x8ce4;
    return write_answer(out, cap, values, 1);
}

/*
 * secret -> secret_gate + secret_phase。
 * 第七行 = 32 个任意非 NUL 字符 + "OPEN" + 三个无符号整数。
 * secret_gate 只检查偏移 32..35 是否为 "OPEN"，前 32 字节自由填充；
 * secret_phase 用 challenge 和 context（六个阶段答案的 transcript_context）计算三个整数。
 */
static uint32_t secret_mix(uint32_t x) {
    return (3 * x + 0xc904) ^ 0xa9b6;
}

int solve_secret(uint64_t challenge, uint64_t context, char *out, size_t cap) {
    uint32_t cl = (uint32_t)challenge;
    uint32_t ch = (uint32_t)(challenge >> 32);
    uint32_t xl = (uint32_t)context;
    uint32_t xh = (uint32_t)(context >> 32);
    uint32_t k = xl & 3;
    uint32_t e = ch ^ cl; /* (challenge >> 32) ^ (uint32_t)challenge */
    uint32_t x = xh ^ xl; /* (context >> 32) ^ (uint32_t)context   */
    uint32_t first = k;
    uint32_t second = secret_mix(e ^ x) ^ ((k * 0xc904) ^ (uint32_t)(context >> 16));
    uint32_t third = second ^ (x + k + 0xa9b6 + (e >> 1));

    int size = snprintf(out, cap, "%32sOPEN %" PRIu32 " %" PRIu32 " %" PRIu32,
                        "", first, second, third);
    return size < 0 || (size_t)size >= cap;
}
