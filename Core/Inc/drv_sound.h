#ifndef __DRV_SOUND_H__
#define __DRV_SOUND_H__

#include <stdint.h>

#define TX_FIFO_SIZE    2048  // Size in Bytes (1024 16-bit audio stereo samples)

/* Bare-Metal Audio Control API */
void BareMetal_Audio_Init(void);
void BareMetal_Audio_SetFrequency(uint32_t samplerate);
void BareMetal_Audio_Start(uint16_t *buffer_ptr, uint16_t size);
void BareMetal_Audio_Stop(void);

#endif /* __DRV_SOUND_H__ */