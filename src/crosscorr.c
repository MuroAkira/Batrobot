#include "crosscorr.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fftw3.h>

struct xcorr_ctx {
    int N;
    double fs, hpf;
    int hpf_bin;

    fftwf_complex *call_in, *call_out;
    fftwf_complex *rec_in,  *rec_out;
    fftwf_complex *mix_in,  *mix_out;
    fftwf_complex *hil_in,  *hil_out;

    fftwf_plan p_call_fwd;
    fftwf_plan p_rec_fwd;
    fftwf_plan p_mix_inv;
    fftwf_plan p_hil_inv;
};

static inline void conj_mul(const fftwf_complex a, const fftwf_complex b, fftwf_complex y)
{
    /* y = conj(a) * b */
    float ar=a[0], ai=a[1], br=b[0], bi=b[1];
    y[0] = ar*br + ai*bi;
    y[1] = -ai*br + ar*bi;
}

xcorr_ctx_t* xcorr_create(int N, double fs_hz, double hpf_hz)
{
    if (N <= 0) return NULL;

    xcorr_ctx_t* c = (xcorr_ctx_t*)calloc(1, sizeof(*c));
    if (!c) return NULL;

    c->N = N;
    c->fs = fs_hz;
    c->hpf = hpf_hz;

    c->hpf_bin = (int)ceil((hpf_hz * (double)N) / fs_hz);
    if (c->hpf_bin < 0) c->hpf_bin = 0;
    if (c->hpf_bin > N/2) c->hpf_bin = N/2;

    size_t sz = (size_t)N;
    c->call_in  = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex)*sz);
    c->call_out = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex)*sz);
    c->rec_in   = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex)*sz);
    c->rec_out  = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex)*sz);
    c->mix_in   = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex)*sz);
    c->mix_out  = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex)*sz);
    c->hil_in   = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex)*sz);
    c->hil_out  = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex)*sz);

    if (!c->call_in || !c->call_out || !c->rec_in || !c->rec_out ||
        !c->mix_in  || !c->mix_out  || !c->hil_in || !c->hil_out) {
        xcorr_destroy(c);
        return NULL;
    }

    c->p_call_fwd = fftwf_plan_dft_1d(N, c->call_in, c->call_out, FFTW_FORWARD, FFTW_ESTIMATE);
    c->p_rec_fwd  = fftwf_plan_dft_1d(N, c->rec_in,  c->rec_out,  FFTW_FORWARD, FFTW_ESTIMATE);
    c->p_mix_inv  = fftwf_plan_dft_1d(N, c->mix_in,  c->mix_out,  FFTW_BACKWARD, FFTW_ESTIMATE);
    c->p_hil_inv  = fftwf_plan_dft_1d(N, c->hil_in,  c->hil_out,  FFTW_BACKWARD, FFTW_ESTIMATE);

    if (!c->p_call_fwd || !c->p_rec_fwd || !c->p_mix_inv || !c->p_hil_inv) {
        xcorr_destroy(c);
        return NULL;
    }

    return c;
}

void xcorr_destroy(xcorr_ctx_t* c)
{
    if (!c) return;

    if (c->p_call_fwd) fftwf_destroy_plan(c->p_call_fwd);
    if (c->p_rec_fwd)  fftwf_destroy_plan(c->p_rec_fwd);
    if (c->p_mix_inv)  fftwf_destroy_plan(c->p_mix_inv);
    if (c->p_hil_inv)  fftwf_destroy_plan(c->p_hil_inv);

    if (c->call_in)  fftwf_free(c->call_in);
    if (c->call_out) fftwf_free(c->call_out);
    if (c->rec_in)   fftwf_free(c->rec_in);
    if (c->rec_out)  fftwf_free(c->rec_out);
    if (c->mix_in)   fftwf_free(c->mix_in);
    if (c->mix_out)  fftwf_free(c->mix_out);
    if (c->hil_in)   fftwf_free(c->hil_in);
    if (c->hil_out)  fftwf_free(c->hil_out);

    free(c);
}

int xcorr_set_call_time(xcorr_ctx_t* c, const float* call_time_N)
{
    if (!c || !call_time_N) return -1;

    for (int i=0;i<c->N;i++) {
        c->call_in[i][0] = call_time_N[i];
        c->call_in[i][1] = 0.0f;
    }
    fftwf_execute(c->p_call_fwd);
    return 0;
}

int xcorr_run_envelope(xcorr_ctx_t* c, const float* rec_time_N, float* env_out_N)
{
    if (!c || !rec_time_N || !env_out_N) return -1;

    const int N = c->N;

    for (int i=0;i<N;i++) {
        c->rec_in[i][0] = rec_time_N[i];
        c->rec_in[i][1] = 0.0f;
    }
    fftwf_execute(c->p_rec_fwd);

    const int h = c->hpf_bin;

    for (int k=0;k<N;k++) {
        int pass = (k >= h) && (k <= N - h);

        fftwf_complex spc;
        if (!pass) { spc[0]=0.0f; spc[1]=0.0f; }
        else { spc[0]=c->rec_out[k][0]; spc[1]=c->rec_out[k][1]; }

        fftwf_complex mixk;
        conj_mul(c->call_out[k], spc, mixk);

        c->mix_in[k][0] = mixk[0];
        c->mix_in[k][1] = mixk[1];

        /* Goと同じ Hilbertペア生成 */
        if (k <= N/2) {
            c->hil_in[k][0] =  mixk[1];
            c->hil_in[k][1] = -mixk[0];
        } else {
            c->hil_in[k][0] = -mixk[1];
            c->hil_in[k][1] =  mixk[0];
        }
    }

    fftwf_execute(c->p_mix_inv);
    fftwf_execute(c->p_hil_inv);

    /* FFTWの逆変換は 1/N が掛からないので正規化 */
    const float invN = 1.0f / (float)N;
    for (int i=0;i<N;i++) {
        float I = c->mix_out[i][0] * invN;
        float Q = c->hil_out[i][0] * invN;
        env_out_N[i] = sqrtf(I*I + Q*Q);
    }

    return 0;
}

size_t xcorr_argmax_range(const float* x, size_t n, size_t i0, size_t i1)
{
    if (!x || n == 0) return 0;
    if (i0 >= n) i0 = n-1;
    if (i1 > n) i1 = n;
    if (i1 <= i0+1) i1 = i0+1;

    size_t mi = i0;
    float mv = x[i0];
    for (size_t i=i0+1;i<i1;i++) {
        if (x[i] > mv) { mv = x[i]; mi = i; }
    }
    return mi;
}
