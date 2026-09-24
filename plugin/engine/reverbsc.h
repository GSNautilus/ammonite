/*
Copyright (c) 2023 Electrosmith, Corp, Sean Costello, Istvan Varga, Paul Batchelor

Use of this source code is governed by the LGPL V2.1
license that can be found in the LICENSE file or at
https://opensource.org/license/lgpl-2-1/
*/
/* Ammonite plugin: a copy of DaisySP-LGPL's ReverbSc (license:
 * LICENSE-reverbsc.txt beside this file), changed only to
 *  - live in namespace ammonite (the plugin does not build DaisySP-LGPL),
 *  - hold its delay lines up to 192 kHz (the original: ~52 kHz),
 *  - refuse a rate whose lines would not fit (the original's check let the
 *    last line run past the buffer).
 * The processing is untouched: at 48 kHz it is bit-identical. */

#pragma once
#ifndef AMMONITE_REVERBSC_H
#define AMMONITE_REVERBSC_H

// The original's 98936 (48 kHz), times 4: 192 kHz. Init lays the lines out
// at float offsets that count their BYTES (kept, so the memory layout and
// the sound stay as on the hardware), which is why it is this large.
#define AMMONITE_REVERBSC_MAX_SIZE (98936 * 4)

namespace ammonite
{
/**Delay line for internal reverb use
*/
typedef struct
{
    int    write_pos;         /**< write position */
    int    buffer_size;       /**< buffer size */
    int    read_pos;          /**< read position */
    int    read_pos_frac;     /**< fractional component of read pos */
    int    read_pos_frac_inc; /**< increment for fractional */
    int    dummy;             /**<  dummy var */
    int    seed_val;          /**< randseed */
    int    rand_line_cnt;     /**< number of random lines */
    float  filter_state;      /**< state of filter */
    float *buf;               /**< buffer ptr */
} ReverbScDl;

/** Stereo Reverb */
class ReverbSc
{
  public:
    ReverbSc() {}
    ~ReverbSc() {}
    /** Initializes the reverb module, and sets the sample_rate at which the Process function will be called.
        Returns 0 if all good, or 1 if the delay lines do not fit (above 192 kHz):
        then Process must not be called.
    */
    int Init(float sample_rate);

    /** Process the input through the reverb, and updates values of out1, and out2 with the new processed signal.
    */
    int Process(const float &in1, const float &in2, float *out1, float *out2);

    /** controls the reverb time. reverb tail becomes infinite when set to 1.0
        \param fb - sets reverb time. range: 0.0 to 1.0
    */
    inline void SetFeedback(const float &fb) { feedback_ = fb; }
    /** controls the internal dampening filter's cutoff frequency.
        \param freq - low pass frequency. range: 0.0 to sample_rate / 2
    */
    inline void SetLpFreq(const float &freq) { lpfreq_ = freq; }

  private:
    void       NextRandomLineseg(ReverbScDl *lp, int n);
    int        InitDelayLine(ReverbScDl *lp, int n);
    float      feedback_, lpfreq_;
    float      i_sample_rate_, i_pitch_mod_, i_skip_init_;
    float      sample_rate_;
    float      damp_fact_;
    float      prv_lpfreq_;
    int        init_done_;
    ReverbScDl delay_lines_[8];
    float      aux_[AMMONITE_REVERBSC_MAX_SIZE];
};


} // namespace ammonite
#endif
