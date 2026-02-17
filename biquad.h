#ifndef BIQUAD_H
#define BIQUAD_H

typedef struct {
	double a0, a1, a2, a3, a4;
	double x1, x2, y1, y2;
} Biquad;

void bq_reset(struct Biquad *bq);

void 
bq_peaking(
		struct Biquad *bq,
		float db_gain,
		float f0,
		float fs,
		float q);

void
bq_lowshelf(
		struct Biquad *bq,
		float db_gain,
		float f0,
		float fs,
		float q);

void
bq_highshelf(
		struct Biquad *bq,
		float db_gain,
		float f0,
		float fs,
		float q);

void
bq_lowpass(
		struct Biquad *bq,
		float f0,
		float fs,
		float q);

void
bq_highpass(
		struct Biquad *bq,
		float f0,
		float fs,
		float q);

static inline float
bq_process(struct Biquad *bq, float x)
{
	float y = bq->a0 * x + bq->a1 * bq->x1 + bq->a2 * bq->x2
		- bq->a3 * bq->y1 - bq->a4 * bq->y2;
	bq->x2 = bq->x1; bq->x1 = x;
	bq->y2 = bq->y1; bq->y1 = y;
	return y;
}

#endif
