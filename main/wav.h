#ifndef __WAV_H__
#define __WAV_H__

#include "stdint.h"

namespace WAV
{
  struct Header
  {
    // RIFF header
    char riff[4] = {'R', 'I', 'F', 'F'};
    uint32_t file_size = UINT32_MAX; // Max it out
    uint8_t format[4] = {'W', 'A', 'V', 'E'};

    // fmt subchunk
    char fmt[4] = {'f', 'm', 't', ' '};
    uint32_t subchunk_size = 16; // Fixed to 16
    uint16_t audio_format = RIFF_FORMAT_LPCM; // 1
    uint16_t channels = 2;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align = 4; // Assuming 16 bit PCM
    uint16_t bits_per_sample = 16;  // Assuming 16 bit PCM

    // data subchunk
    char data[4] = {'d', 'a', 't', 'a'};
    uint32_t data_size = UINT32_MAX; // Max it out

    Header(uint32_t sample_rate = 48000)
    {
      // Update sample rate dependent terms
      this->sample_rate = sample_rate;
      this->byte_rate = 4 * sample_rate;
    }

    private:
      static constexpr int RIFF_FORMAT_LPCM = 1;
  };
}

#endif