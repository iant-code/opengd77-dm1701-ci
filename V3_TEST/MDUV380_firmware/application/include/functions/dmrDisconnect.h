/*
 * Copyright (C) 2025 OpenGD77 contributors
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES ARE DISCLAIMED.
 */
#ifndef _OPENGD77_DMR_DISCONNECT_H_
#define _OPENGD77_DMR_DISCONNECT_H_

#include <stdbool.h>
#include <stdint.h>

// "Hard disconnect" for a busy dynamic DMR talkgroup: transmits a brief group call to TG 4000
// (the BrandMeister universal disconnect/unlink-all ID), which tells the hotspot to drop the link
// so it stops firehosing that talkgroup at you.
//
// Why this can't be a fire-immediately button: a simplex hotspot is half-duplex, so while it's
// transmitting the busy TG down to the radio it physically cannot hear the radio transmit back --
// and the radio won't key up over a call it's decoding either. On a busy TG (e.g. TG91 Worldwide)
// the hotspot transmits almost continuously, so the disconnect can only get out in the short gap
// between overs, which is too brief for a human to hit with a manual PTT tap. So this ARMS a
// pending send and auto-fires it the instant the channel goes clear, retrying across successive
// gaps until it gets out (or a timeout gives up). See dmrDisconnect.c for the state machine.

void dmrDisconnectArm(void);       // arm (or, if already armed/running, cancel). DMR mode only.
void dmrDisconnectAbort(void);     // cancel and restore, wherever we are in the sequence.
bool dmrDisconnectIsActive(void);  // true while armed or transmitting.

// Called every main-loop iteration from applicationMain.c with the loop's button state, so it can
// inject/release BUTTON_PTT the same way the VOX path does -- reusing the whole proven DMR TX
// chain rather than driving the HR-C6000 directly. currentMenuNumber is menuSystemGetCurrentMenuNumber().
void dmrDisconnectTick(int currentMenuNumber, uint32_t *buttonsInOut, int *buttonEventInOut);

#endif /* _OPENGD77_DMR_DISCONNECT_H_ */
