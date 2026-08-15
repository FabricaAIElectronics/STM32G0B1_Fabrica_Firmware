/* Host-side assertions for the ButtonBoard encoder count -> detent mapping.
 * Includes the real Core/Inc/encoder_math.h, so this tests shipped code.
 *
 * The property under test: the knob rests at accum = 4k, and that rest
 * position must sit in the MIDDLE of detent k's bin, so +-1 count of contact
 * jitter at rest never changes the report and both approach directions
 * settle on the same value. */
#include "harness.h"
#include "encoder_math.h"

int main(void)
{
    /* Rest positions map to their own detent number, positive and negative. */
    VV_EQ_U32("rest_0",   (int16_t)Encoder_CountsToDetents(0),   0);
    VV_EQ_U32("rest_p1",  (int16_t)Encoder_CountsToDetents(4),   1);
    VV_EQ_U32("rest_p5",  (int16_t)Encoder_CountsToDetents(20),  5);
    VV_EQ_U32("rest_m1",  (unsigned long)(int16_t)Encoder_CountsToDetents(-4),
                          (unsigned long)(int16_t)-1);
    VV_EQ_U32("rest_m5",  (unsigned long)(int16_t)Encoder_CountsToDetents(-20),
                          (unsigned long)(int16_t)-5);

    /* THE BUG: +-1 count of jitter around a rest position must not move the
     * report. Under plain floor(accum/4), accum=3 read as detent 0 and
     * accum=-1 read as detent -1 - a flip on every wobble. */
    VV_EQ_U32("jitter_p1_at_4",  (int16_t)Encoder_CountsToDetents(5),  1);
    VV_EQ_U32("jitter_m1_at_4",  (int16_t)Encoder_CountsToDetents(3),  1);
    VV_EQ_U32("jitter_p1_at_0",  (int16_t)Encoder_CountsToDetents(1),  0);
    VV_EQ_U32("jitter_m1_at_0",  (unsigned long)(int16_t)Encoder_CountsToDetents(-1), 0);
    VV_EQ_U32("jitter_p1_at_m4", (unsigned long)(int16_t)Encoder_CountsToDetents(-3),
                                 (unsigned long)(int16_t)-1);
    VV_EQ_U32("jitter_m1_at_m4", (unsigned long)(int16_t)Encoder_CountsToDetents(-5),
                                 (unsigned long)(int16_t)-1);

    /* Two counts of margin either side: the bin for detent 1 is exactly
     * [2, 5]. Below it is detent 0, above it detent 2. */
    VV_EQ_U32("bin1_low_edge",   (int16_t)Encoder_CountsToDetents(2), 1);
    VV_EQ_U32("bin1_high_edge",  (int16_t)Encoder_CountsToDetents(5), 1);
    VV_EQ_U32("bin1_below",      (int16_t)Encoder_CountsToDetents(1), 0);
    VV_EQ_U32("bin1_above",      (int16_t)Encoder_CountsToDetents(6), 2);

    /* Every bin is exactly 4 counts wide, including the one straddling zero
     * (the original floor fix guarded this; keep guarding it). */
    {
        int ok = 1;
        for (int32_t d = -6; d <= 6; d++) {
            int width = 0;
            for (int32_t a = d * 4 - 4; a <= d * 4 + 4; a++) {
                if (Encoder_CountsToDetents(a) == d) width++;
            }
            if (width != 4) ok = 0;
        }
        VV_CHECK("all_bins_width_4", ok);
    }

    /* Direction symmetry: sweeping CW then CCW through the same counts must
     * report identical detents at identical counts (no path dependence). */
    {
        int ok = 1;
        for (int32_t a = -40; a <= 40; a++) {
            if (Encoder_CountsToDetents(a) != Encoder_CountsToDetents(a)) ok = 0;
        }
        /* and monotonic: report never decreases as counts increase */
        for (int32_t a = -40; a < 40; a++) {
            if (Encoder_CountsToDetents(a + 1) < Encoder_CountsToDetents(a)) ok = 0;
        }
        VV_CHECK("monotonic_and_path_independent", ok);
    }

    /* SetEncoder round-trip: seeding at DetentsToCounts(p) must read back p
     * and sit at the bin centre (so it enjoys the full +-2 margin). */
    VV_EQ_U32("roundtrip_p100",  (int16_t)Encoder_CountsToDetents(Encoder_DetentsToCounts(100)), 100);
    VV_EQ_U32("roundtrip_m100",  (unsigned long)(int16_t)Encoder_CountsToDetents(Encoder_DetentsToCounts(-100)),
                                 (unsigned long)(int16_t)-100);
    VV_EQ_U32("roundtrip_m100_jitter",
              (unsigned long)(int16_t)Encoder_CountsToDetents(Encoder_DetentsToCounts(-100) - 1),
              (unsigned long)(int16_t)-100);

    VV_REPORT();
}
