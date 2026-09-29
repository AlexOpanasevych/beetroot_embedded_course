/*
 * PIR motion alarm — the ISR + binary-semaphore "sleeps at 0% CPU until
 * woken" pattern from module6.5's emergency-stop task, reused here for the
 * PIR input instead of a panic button. On wake it runs the buzzer/LED
 * "bark" reaction locally (fast path, no queue round-trip needed for the
 * physical reaction), then reports the event into sensor_hub's queue so the
 * web dashboard picks it up too.
 */
#pragma once

// Configures the PIR input (with its GPIO ISR), buzzer and LED-eyes outputs,
// and starts the task that waits on the motion semaphore.
void pir_alarm_init(void);
