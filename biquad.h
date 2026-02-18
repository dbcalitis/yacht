#ifndef BIQUAD_H
#define BIQUAD_H

enum FilterType : int
{
	BQ_NONE = 0,
	BQ_PEAKING = 1,
	BQ_LOWSHELF,
	BQ_HIGHSHELF,
	BQ_LOWPASS,
	BQ_HIGHPASS,
    BQ_MAX
};

typedef struct
{
	enum FilterType type;
	float args[3];
	// BQ_PEAKING, BQ_LOWSHELF, BQ_HIGHSHELF
	// 0 - frequency
	// 1 - quality
	// 2 - db_gain

	// BQ_LOW_PASS, BQ_HIGHPASS
	// 0 - frequency
	// 1 - quality
} BiquadInfo;

typedef struct {
	double a0, a1, a2, a3, a4;
	double x1, x2, y1, y2;
} Biquad;

void bq_reset(Biquad *bq);

void 
bq_peaking(
		Biquad *bq,
		float db_gain,
		float f0,
		float fs,
		float q);

void
bq_lowshelf(
		Biquad *bq,
		float db_gain,
		float f0,
		float fs,
		float q);

void
bq_highshelf(
		Biquad *bq,
		float db_gain,
		float f0,
		float fs,
		float q);

void
bq_lowpass(
		Biquad *bq,
		float f0,
		float fs,
		float q);

void
bq_highpass(
		Biquad *bq,
		float f0,
		float fs,
		float q);

static inline float
bq_process(Biquad *bq, float x)
{
	float y = bq->a0 * x + bq->a1 * bq->x1 + bq->a2 * bq->x2
		- bq->a3 * bq->y1 - bq->a4 * bq->y2;
	bq->x2 = bq->x1; bq->x1 = x;
	bq->y2 = bq->y1; bq->y1 = y;
	return y;
}

void
bq_update(
        Biquad (*bqs)[2],
        BiquadInfo * bq_info, 
        int num_bq,
        int channels,
        int fs);

#endif
