#ifndef CROSSCORR_H
#define CROSSCORR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xcorr_ctx xcorr_ctx_t;

xcorr_ctx_t* xcorr_create(int N, double fs_hz, double hpf_hz);
void xcorr_destroy(xcorr_ctx_t* c);

/* 参照信号（時間領域, N点）をセットして内部でFFTして保持 */
int xcorr_set_call_time(xcorr_ctx_t* c, const float* call_time_N);

/* 受信信号（時間領域, N点）から相互相関エンベロープを計算 */
int xcorr_run_envelope(xcorr_ctx_t* c, const float* rec_time_N, float* env_out_N);

/* 配列の最大値インデックス */
size_t xcorr_argmax_range(const float* x, size_t n, size_t i0, size_t i1);

#ifdef __cplusplus
}
#endif

#endif
