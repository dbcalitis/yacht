#include "biquad.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>


// amplitude over time -> amplitude over frequency -> amplitude over time
// decouple the frequencies
// undo the frequency
// go backwards

// high, middle, low pass; add them

// sample of audio -> ff to do changes -> amplitude over time

void bq_reset(Biquad *bq) { memset(&bq->x1, 0, 4 * sizeof(double)); }

/*** AUDIO EQ COOKBOOK ***/
void
bq_peaking(
		Biquad *bq,
		float db_gain,
		float f0,
		float fs,
		float q)
{
	const float A = powf(10.0f, db_gain / 40.0f);
	const float w0 = 2 * M_PI * f0 / fs;
	const float sn = sinf(w0), cs = cosf(w0);
	const float alpha = sn / (2.0f * q);

	const float alpha_A = alpha * A;
	const float alpha_dA = alpha / A;

	const float b0 = 1.0f + alpha_A;
	const float b1 = -2.0f * cs;
	const float b2 = 1.0f - alpha_A;
	const float a0 = 1.0f + alpha_dA;
	const float a1 = b1;
	const float a2 = 1.0f - alpha_dA;

	bq->a0 = b0 / a0;
	bq->a1 = b1 / a0;
	bq->a2 = b2 / a0;
	bq->a3 = a1 / a0;
	bq->a4 = a2 / a0;
	bq_reset(bq);
}

void
bq_lowshelf(
		Biquad *bq,
		float db_gain,
		float f0,
		float fs,
		float q)
{
	const float A = powf(10.0f, db_gain / 40.0f);
	const float w0 = 2.0f * M_PI * f0 / fs;
	const float sn = sinf(w0), cs = cosf(w0);
	const float alpha = sn / (2.0f * q);

	const float beta = 2.0f * sqrtf(A) * alpha;

	const float b0 = A * ((A + 1.0f) - (A - 1.0f) * cs + beta);
	const float b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cs);
	const float b2 = A * ((A + 1.0f) - (A - 1.0f) * cs - beta);
	const float a0 = (A + 1.0f) + (A - 1.0f) * cs + beta;
	const float a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cs);
	const float a2 = (A + 1.0f) + (A - 1.0f) * cs - beta;

	bq->a0 = b0 / a0;
	bq->a1 = b1 / a0;
	bq->a2 = b2 / a0;
	bq->a3 = a1 / a0;
	bq->a4 = a2 / a0;
	bq_reset(bq);
}

void
bq_highshelf(
		Biquad *bq,
		float db_gain,
		float f0,
		float fs,
		float q)
{
	const float A = powf(10.0f, db_gain / 40.0f);
	const float w0 = 2.0f * M_PI * f0 / fs;
	const float sn = sinf(w0), cs = cosf(w0);
	const float alpha = sn / (2.0f * q);

	const float beta = 2 * sqrtf(A) * alpha;

	const float b0 = A * ((A + 1.0f) + (A - 1.0f) * cs + beta);
	const float b1 = 2.0f * A * ((A - 1.0f) + (A + 1.0f) * cs);
	const float b2 = A * ((A + 1.0f) + (A - 1.0f) * cs - beta);
	const float a0 = (A + 1.0f) - (A - 1.0f) * cs + beta;
	const float a1 = -2.0f * ((A - 1.0f) - (A + 1.0f) * cs);
	const float a2 = (A + 1.0f) - (A - 1.0f) * cs - beta;

	bq->a0 = b0 / a0;
	bq->a1 = b1 / a0;
	bq->a2 = b2 / a0;
	bq->a3 = a1 / a0;
	bq->a4 = a2 / a0;
	bq_reset(bq);
}

void
bq_lowpass(
		Biquad *bq,
		float f0,
		float fs,
		float q)
{
	const float w0 = 2.0f * M_PI * f0 / fs;
	const float sn = sinf(w0), cs = cosf(w0);
	const float alpha = sn / (2.0f * q);

	const float b0 = (1.0f - cs) / 2.0f;
	const float b1 = 1.0f - cs;
	const float b2 = (1.0f - cs) / 2.0f;
	const float a0 = 1.0f + alpha;
	const float a1 = -2.0f * cs;
	const float a2 = 1.0f - alpha;

	bq->a0 = b0 / a0;
	bq->a1 = b1 / a0;
	bq->a2 = b2 / a0;
	bq->a3 = a1 / a0;
	bq->a4 = a2 / a0;
	bq_reset(bq);
}

void
bq_highpass(
		Biquad *bq,
		float f0,
		float fs,
		float q)
{
	const float w0 = 2.0f * M_PI * f0 / fs;
	const float sn = sinf(w0), cs = cosf(w0);
	const float alpha = sn / (2.0f * q);

	const float b0 = (1.0f + cs) / 2.0f;
	const float b1 = -(1.0f + cs);
	const float b2 = (1.0f + cs) / 2.0f;
	const float a0 = 1.0f + alpha;
	const float a1 = -2.0f * cs;
	const float a2 = 1.0f - alpha;

	bq->a0 = b0 / a0;
	bq->a1 = b1 / a0;
	bq->a2 = b2 / a0;
	bq->a3 = a1 / a0;
	bq->a4 = a2 / a0;
	bq_reset(bq);
}

void
bq_update(
        Biquad (*bqs)[2],
        BiquadInfo * bq_info, 
        int num_bq,
        int channels,
        int fs)
{
	for (int b = 0; b < num_bq/*_num_filters*/; b++)
	{
		for (uint8_t c = 0; c < channels; c++)
		{
			switch (bq_info[b].type)
			{
			case BQ_PEAKING:
				bq_peaking(&bqs[b][c],
						bq_info[b].args[2],
						bq_info[b].args[0],
						(float) fs,
						bq_info[b].args[1]);
				break;
			case BQ_LOWSHELF:
				bq_lowshelf(&bqs[b][c],
						bq_info[b].args[2],
						bq_info[b].args[0],
						(float) fs,
						bq_info[b].args[1]);
				break;
			case BQ_HIGHSHELF:
				bq_highshelf(&bqs[b][c],
						bq_info[b].args[2],
						bq_info[b].args[0],
						(float) fs,
						bq_info[b].args[1]);
				break;
			case BQ_LOWPASS:
				bq_lowpass(&bqs[b][c],
						bq_info[b].args[0],
						(float) fs,
						bq_info[b].args[1]);
				break;
			case BQ_HIGHPASS:
				bq_highpass(&bqs[b][c],
						bq_info[b].args[0],
						(float) fs,
						bq_info[b].args[1]);
				break;
			default:
				break;
			}
		}
	}
}
