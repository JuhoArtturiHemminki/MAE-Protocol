#include <stdint.h>
#include <stdlib.h>

#define SHIFT 16
#define SCALE (1ULL << SHIFT)
#define MASK_64 ((1ULL << 60) - 1)
#define BLOCK_SIZE 64

typedef struct {
    uint64_t reg_rx;
    uint64_t reg_rwa;
    uint64_t reg_rwb;
    uint64_t eta;
    uint64_t c_coeff;
    uint64_t init_rx;
    uint64_t init_rwa;
    uint64_t init_rwb;
} MAEContext;

#ifdef _WIN32
__declspec(dllexport) MAEContext* MAE_CreateContext() {
#else
MAEContext* MAE_CreateContext() {
#endif
    MAEContext* ctx = (MAEContext*)malloc(sizeof(MAEContext));
    if (!ctx) return NULL;
    ctx->init_rx = (uint64_t)(0.1 * SCALE);
    ctx->init_rwa = (uint64_t)(0.6 * SCALE);
    ctx->init_rwb = (uint64_t)(0.4 * SCALE);
    ctx->reg_rx = ctx->init_rx;
    ctx->reg_rwa = ctx->init_rwa;
    ctx->reg_rwb = ctx->init_rwb;
    ctx->eta = (uint64_t)(0.005 * SCALE);
    ctx->c_coeff = (uint64_t)(0.5 * SCALE);
    return ctx;
}

static void reset_context_to_init(MAEContext* ctx) {
    ctx->reg_rx = ctx->init_rx;
    ctx->reg_rwa = ctx->init_rwa;
    ctx->reg_rwb = ctx->init_rwb;
}

#ifdef _WIN32
__declspec(dllexport) void MAE_EncodeLive(MAEContext* ctx, const uint8_t* input_bits, uint64_t* output_stream, uint64_t total_length, uint64_t* structural_keys, uint64_t* final_rwa, uint64_t* final_rwb) {
#else
void MAE_EncodeLive(MAEContext* ctx, const uint8_t* input_bits, uint64_t* output_stream, uint64_t total_length, uint64_t* structural_keys, uint64_t* final_rwa, uint64_t* final_rwb) {
#endif
    uint64_t num_blocks = total_length / BLOCK_SIZE;
    for (uint64_t b = 0; b < num_blocks; b++) {
        reset_context_to_init(ctx);
        uint64_t block_start = b * BLOCK_SIZE;
        for (uint64_t i = 0; i < BLOCK_SIZE; i++) {
            uint64_t idx = block_start + i;
            uint64_t bit = input_bits[idx];
            uint64_t x_w_b = (ctx->reg_rx * ctx->reg_rwb) >> SHIFT;
            uint64_t mod = (ctx->c_coeff * (((x_w_b * x_w_b) >> SHIFT) * x_w_b) >> SHIFT) >> SHIFT;
            uint64_t new_rx = ((ctx->reg_rx * ctx->reg_rwa) >> SHIFT) + mod + (bit << SHIFT);
            new_rx &= MASK_64;
            uint64_t delta = ((ctx->eta * ((new_rx * ctx->reg_rx) >> SHIFT)) >> SHIFT);
            ctx->reg_rwa = (ctx->reg_rwa + delta) & MASK_64;
            ctx->reg_rwb = (ctx->reg_rwb - delta) & MASK_64;
            output_stream[idx] = ctx->reg_rx;
            ctx->reg_rx = new_rx;
        }
        structural_keys[b] = ctx->reg_rx;
        final_rwa[b] = ctx->reg_rwa;
        final_rwb[b] = ctx->reg_rwb;
    }
}

#ifdef _WIN32
__declspec(dllexport) void MAE_DecodeLive(MAEContext* ctx, const uint64_t* data_stream, uint8_t* output_bits, uint64_t total_length, const uint64_t* structural_keys, const uint64_t* final_rwa, const uint64_t* final_rwb) {
#else
void MAE_DecodeLive(MAEContext* ctx, const uint64_t* data_stream, uint8_t* output_bits, uint64_t total_length, const uint64_t* structural_keys, const uint64_t* final_rwa, const uint64_t* final_rwb) {
#endif
    uint64_t num_blocks = total_length / BLOCK_SIZE;
    for (uint64_t b = 0; b < num_blocks; b++) {
        uint64_t block_start = b * BLOCK_SIZE;
        uint64_t next_rx = structural_keys[b];
        ctx->reg_rwa = final_rwa[b];
        ctx->reg_rwb = final_rwb[b];
        for (int64_t i = BLOCK_SIZE - 1; i >= 0; i--) {
            uint64_t idx = block_start + i;
            uint64_t target_x = data_stream[idx];
            uint64_t delta = ((ctx->eta * ((next_rx * target_x) >> SHIFT)) >> SHIFT);
            ctx->reg_rwa = (ctx->reg_rwa + MASK_64 + 1 - delta) & MASK_64;
            ctx->reg_rwb = (ctx->reg_rwb + delta) & MASK_64;
            uint64_t x_w_b = (target_x * ctx->reg_rwb) >> SHIFT;
            uint64_t mod = (ctx->c_coeff * (((x_w_b * x_w_b) >> SHIFT) * x_w_b) >> SHIFT) >> SHIFT;
            uint64_t pred_rx_0 = (((target_x * ctx->reg_rwa) >> SHIFT) + mod + (0 << SHIFT)) & MASK_64;
            uint64_t pred_rx_1 = (((target_x * ctx->reg_rwa) >> SHIFT) + mod + (1 << SHIFT)) & MASK_64;
            int64_t err_0 = (int64_t)pred_rx_0 - (int64_t)next_rx;
            int64_t err_1 = (int64_t)pred_rx_1 - (int64_t)next_rx;
            if (err_0 < 0) err_0 = -err_0;
            if (err_1 < 0) err_1 = -err_1;
            if (err_0 < err_1) {
                output_bits[idx] = 0;
            } else {
                output_bits[idx] = 1;
            }
            next_rx = target_x;
        }
    }
}

#ifdef _WIN32
__declspec(dllexport) void MAE_DestroyContext(MAEContext* ctx) {
#else
void MAE_DestroyContext(MAEContext* ctx) {
#endif
    if (ctx) free(ctx);
}
