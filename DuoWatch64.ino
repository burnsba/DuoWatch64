/* Copyright 2019 Ben Burns
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.    See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.    If not, see <https://www.gnu.org/licenses/>.
 */
 
/*
 * Reads controller data signal line in round robin fashion.
 * If one controller is unplugged, the line won't be read any more.
 * Plug it into the console and reset the arduino to start over.
 * 
 * Send packet data format is 8 bytes.
 * ['>'][controller_id][':'][controller_data (32 bits)]['|']
 * controller_id is ascii, '1' or '2' (or '3' or '4' I guess)
 * 
 * This is designed to be used with an Arduino Uno.
 */

#define SERIAL_BAUDRATE 250000

//#define DEBUG_SERIAL_PRINT

// Will read the input line continuously until a high value
// has been read at least this many times. This number should
// be larger than the most possible number of 1's from a
// communication query (40 bits).
// The line will be read two or three times per 1us, this
// is used to wait for the long high signal the N64 console
// drives between communications.
#define WAIT_FRAME_HIGH_COUNT 320

// If the timer counts past this while trying to read the controller
// state, assume the read failed. (see pre-scalar for how long this is)
#define TIMER_COUNT_CUTOFF_GOOD_READ 6000

// Number of times to fail reading before stop trying.
#define FAIL_STOP_COUNT 12

#define CONTROLLER_1_PIN_NUMBER 2
#define CONTROLLER_1_PIN (PIND & 0x04) 
#define CONTROLLER_1_PIN_STATE ((PIND & 0x04) >> 2)

#define CONTROLLER_1_PIN_TO_B0() ((PIND & 0x04)>>2)
#define CONTROLLER_1_PIN_TO_B1() ((PIND & 0x04)>>1)
#define CONTROLLER_1_PIN_TO_B2()    (PIND & 0x04)
#define CONTROLLER_1_PIN_TO_B3() ((PIND & 0x04)<<1)
#define CONTROLLER_1_PIN_TO_B4() ((PIND & 0x04)<<2)
#define CONTROLLER_1_PIN_TO_B5() ((PIND & 0x04)<<3)
#define CONTROLLER_1_PIN_TO_B6() ((PIND & 0x04)<<4)
#define CONTROLLER_1_PIN_TO_B7() ((PIND & 0x04)<<5)

#define CONTROLLER_2_PIN_NUMBER 3
#define CONTROLLER_2_PIN (PIND & 0x08) 
#define CONTROLLER_2_PIN_STATE ((PIND & 0x08) >> 3)

#define CONTROLLER_2_PIN_TO_B0() ((PIND & 0x08)>>3)
#define CONTROLLER_2_PIN_TO_B1() ((PIND & 0x08)>>2)
#define CONTROLLER_2_PIN_TO_B2() ((PIND & 0x08)>>1)
#define CONTROLLER_2_PIN_TO_B3()    (PIND & 0x08)
#define CONTROLLER_2_PIN_TO_B4() ((PIND & 0x08)<<1)
#define CONTROLLER_2_PIN_TO_B5() ((PIND & 0x08)<<2)
#define CONTROLLER_2_PIN_TO_B6() ((PIND & 0x08)<<3)
#define CONTROLLER_2_PIN_TO_B7() ((PIND & 0x08)<<4)

// nop for 32 instructions.
// @ 16 MHz = 2us
#define WAIT30() asm volatile("\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop");
#define WAIT32()    asm volatile("\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop")

#define WAIT() WAIT30()

// Read controller data into this buffer.
unsigned char stateBufferController[4];

// Flag to disable/enable reading controller 1.
unsigned char controller_1_enabled = 1;

// Flag to disable/enable reading controller 2.
unsigned char controller_2_enabled = 1;

// temp variable to read timer1
unsigned int timer1_count;

// "high count", counter used to keep track of 
// pin state when waiting for next frame.
unsigned int hc = 0;

// Number of controller state packets sent each main loop iteration.
int send_count = 0;

// Number of times failed to read controller 1.
int controller_1_failcount = 0;

// Number of times failed to read controller 2.
int controller_2_failcount = 0;

void setup()
{
    pinMode(CONTROLLER_1_PIN_NUMBER, INPUT);
    pinMode(CONTROLLER_2_PIN_NUMBER, INPUT);
    Serial.begin(SERIAL_BAUDRATE);

    noInterrupts();
    // Clear everything in timer1
    TCCR1A = 0;
    TCCR1B = 0;
    TCNT1  = 0;

    // don't output compare
    OCR1A = 0;

    // CTC mode
    TCCR1B |= (1 << WGM12); 

    // prescale = /64
    TCCR1B |= 0x04;
  
    // no ISR enable
  
    interrupts();
}

inline void wait_for_next_frame_controller_1()
{
    hc = 0;
    do
    {
        if (CONTROLLER_1_PIN > 0)
            hc++;
    } while (hc < WAIT_FRAME_HIGH_COUNT);
}

inline void wait_for_next_frame_controller_2()
{
    hc = 0;
    do
    {
        if (CONTROLLER_2_PIN > 0)
            hc++;
    } while (hc < WAIT_FRAME_HIGH_COUNT);
}

inline void read_controller_1_pin_to_min_buffer()
{
    wait_for_next_frame_controller_1();

    // Should now be in the dead time after controller input sent,
    // this is about 1600 us of the line being held high.

    // I'll be dropping the first nine bits read, because we
    // don't really care about the console request+stop bit.
  
    /*..........................*/ while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[0] = CONTROLLER_1_PIN_TO_B0();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[0] = CONTROLLER_1_PIN_TO_B0();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[0] = CONTROLLER_1_PIN_TO_B0();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[0] = CONTROLLER_1_PIN_TO_B0();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[0] = CONTROLLER_1_PIN_TO_B0();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[0] = CONTROLLER_1_PIN_TO_B0();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[0] = CONTROLLER_1_PIN_TO_B0();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[0] = CONTROLLER_1_PIN_TO_B0();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[0] = CONTROLLER_1_PIN_TO_B0();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[0] = CONTROLLER_1_PIN_TO_B0();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[0] |= CONTROLLER_1_PIN_TO_B1();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[0] |= CONTROLLER_1_PIN_TO_B2();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[0] |= CONTROLLER_1_PIN_TO_B3();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[0] |= CONTROLLER_1_PIN_TO_B4();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[0] |= CONTROLLER_1_PIN_TO_B5();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[0] |= CONTROLLER_1_PIN_TO_B6();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[0] |= CONTROLLER_1_PIN_TO_B7();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[1] = CONTROLLER_1_PIN_TO_B0();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[1] |= CONTROLLER_1_PIN_TO_B1();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[1] |= CONTROLLER_1_PIN_TO_B2();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[1] |= CONTROLLER_1_PIN_TO_B3();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[1] |= CONTROLLER_1_PIN_TO_B4();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[1] |= CONTROLLER_1_PIN_TO_B5();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[1] |= CONTROLLER_1_PIN_TO_B6();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[1] |= CONTROLLER_1_PIN_TO_B7();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[2] = CONTROLLER_1_PIN_TO_B0();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[2] |= CONTROLLER_1_PIN_TO_B1();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[2] |= CONTROLLER_1_PIN_TO_B2();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[2] |= CONTROLLER_1_PIN_TO_B3();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[2] |= CONTROLLER_1_PIN_TO_B4();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[2] |= CONTROLLER_1_PIN_TO_B5();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[2] |= CONTROLLER_1_PIN_TO_B6();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[2] |= CONTROLLER_1_PIN_TO_B7();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[3] = CONTROLLER_1_PIN_TO_B0();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[3] |= CONTROLLER_1_PIN_TO_B1();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[3] |= CONTROLLER_1_PIN_TO_B2();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[3] |= CONTROLLER_1_PIN_TO_B3();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[3] |= CONTROLLER_1_PIN_TO_B4();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[3] |= CONTROLLER_1_PIN_TO_B5();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[3] |= CONTROLLER_1_PIN_TO_B6();
    while (CONTROLLER_1_PIN == 0); while (CONTROLLER_1_PIN > 0); WAIT(); stateBufferController[3] |= CONTROLLER_1_PIN_TO_B7();
}

inline void read_controller_2_pin_to_min_buffer()
{
    // This should be called right after reading the controller 1 input,
    // so should be in the dead time between frames.

    // I'll be dropping the first nine bits read, because we
    // don't really care about the console request+stop bit.

    wait_for_next_frame_controller_2();

    /*..........................*/ while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[0] = CONTROLLER_2_PIN_TO_B0();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[0] = CONTROLLER_2_PIN_TO_B0();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[0] = CONTROLLER_2_PIN_TO_B0();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[0] = CONTROLLER_2_PIN_TO_B0();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[0] = CONTROLLER_2_PIN_TO_B0();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[0] = CONTROLLER_2_PIN_TO_B0();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[0] = CONTROLLER_2_PIN_TO_B0();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[0] = CONTROLLER_2_PIN_TO_B0();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[0] = CONTROLLER_2_PIN_TO_B0();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[0] = CONTROLLER_2_PIN_TO_B0();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[0] |= CONTROLLER_2_PIN_TO_B1();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[0] |= CONTROLLER_2_PIN_TO_B2();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[0] |= CONTROLLER_2_PIN_TO_B3();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[0] |= CONTROLLER_2_PIN_TO_B4();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[0] |= CONTROLLER_2_PIN_TO_B5();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[0] |= CONTROLLER_2_PIN_TO_B6();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[0] |= CONTROLLER_2_PIN_TO_B7();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[1] = CONTROLLER_2_PIN_TO_B0();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[1] |= CONTROLLER_2_PIN_TO_B1();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[1] |= CONTROLLER_2_PIN_TO_B2();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[1] |= CONTROLLER_2_PIN_TO_B3();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[1] |= CONTROLLER_2_PIN_TO_B4();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[1] |= CONTROLLER_2_PIN_TO_B5();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[1] |= CONTROLLER_2_PIN_TO_B6();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[1] |= CONTROLLER_2_PIN_TO_B7();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[2] = CONTROLLER_2_PIN_TO_B0();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[2] |= CONTROLLER_2_PIN_TO_B1();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[2] |= CONTROLLER_2_PIN_TO_B2();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[2] |= CONTROLLER_2_PIN_TO_B3();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[2] |= CONTROLLER_2_PIN_TO_B4();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[2] |= CONTROLLER_2_PIN_TO_B5();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[2] |= CONTROLLER_2_PIN_TO_B6();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[2] |= CONTROLLER_2_PIN_TO_B7();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[3] = CONTROLLER_2_PIN_TO_B0();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[3] |= CONTROLLER_2_PIN_TO_B1();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[3] |= CONTROLLER_2_PIN_TO_B2();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[3] |= CONTROLLER_2_PIN_TO_B3();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[3] |= CONTROLLER_2_PIN_TO_B4();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[3] |= CONTROLLER_2_PIN_TO_B5();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[3] |= CONTROLLER_2_PIN_TO_B6();
    while (CONTROLLER_2_PIN == 0); while (CONTROLLER_2_PIN > 0); WAIT(); stateBufferController[3] |= CONTROLLER_2_PIN_TO_B7();
}

inline void sendPacket(char controller) {
    Serial.write('>'); // 62
    Serial.write(controller);
    Serial.write(':'); // 58
    Serial.write(stateBufferController, 4);
    Serial.write('|'); // 124
}

inline void sendDebugPacket(char controller) {
    Serial.write('>'); // 62
    Serial.write(controller);
    Serial.write(':'); // 58
    for (int i=0; i<4; i++) {
          Serial.print((stateBufferController[i] & 0x01) ? '1' : '0');
          Serial.print((stateBufferController[i] & 0x02) ? '1' : '0');
          Serial.print((stateBufferController[i] & 0x04) ? '1' : '0');
          Serial.print((stateBufferController[i] & 0x08) ? '1' : '0');
          Serial.print((stateBufferController[i] & 0x10) ? '1' : '0');
          Serial.print((stateBufferController[i] & 0x20) ? '1' : '0');
          Serial.print((stateBufferController[i] & 0x40) ? '1' : '0');
          Serial.print((stateBufferController[i] & 0x80) ? '1' : '0');
    }
    Serial.write('|'); // 124
}

// Not sure what's causing it, but some interrupts sometimes hit, causing
// the read to timeout or fail. Disabling interrupts gives a smooth read.

void loop()
{
    // Main loop will read the controller. If it timesout, increment fail counter.
    // If it fails too many times, stop trying to read.
    //
    // If all controllers fail to read, reset the fail indicators to keep trying
    // until at least one succeeds. Otherwise, it will just disable them all
    // and never read anything again. This might not be a bad thing, but (at
    // least with interrupts disabled) the controller can sometimes fail to
    // read when it's still connected.
    send_count = 0;

    if (controller_1_enabled) {
    
        noInterrupts();
        TCNT1 = 0;
        read_controller_1_pin_to_min_buffer();
        timer1_count = TCNT1;
        interrupts();
        
        if (timer1_count > 0 && timer1_count > TIMER_COUNT_CUTOFF_GOOD_READ)
        {
            controller_1_failcount++;
            if (controller_1_failcount > FAIL_STOP_COUNT)
            {
                controller_1_enabled = 0;
            }
        }
        else
        {
            #ifdef DEBUG_SERIAL_PRINT
            sendDebugPacket('1');
            #else
            sendPacket('1');
            #endif
    
            send_count++;
            controller_1_failcount = 0;
        }
        
        #ifdef DEBUG_SERIAL_PRINT
        Serial.print("TCNT1: ");
        Serial.print(timer1_count);
        Serial.print("\n");
        #endif
    }

    if (controller_2_enabled) {

        noInterrupts();
        TCNT1 = 0;
        read_controller_2_pin_to_min_buffer();
        timer1_count = TCNT1;
        interrupts();
        
        if (timer1_count > 0 && timer1_count > TIMER_COUNT_CUTOFF_GOOD_READ)
        {
            controller_2_failcount++;
            if (controller_2_failcount > FAIL_STOP_COUNT)
            {
                controller_2_enabled = 0;
            }
        }
        else
        {
            #ifdef DEBUG_SERIAL_PRINT
            sendDebugPacket('2');
            #else
            sendPacket('2');
            #endif
    
            send_count++;
            controller_2_failcount = 0;
        }
        
        #ifdef DEBUG_SERIAL_PRINT
        Serial.print("TCNT1: ");
        Serial.print(timer1_count);
        Serial.print("\n");
        #endif
    }

    if (send_count == 0) {
        controller_1_enabled = 1;
        controller_2_enabled = 1;
    }

    #ifdef DEBUG_SERIAL_PRINT
    Serial.print("\n\n");
    #endif
}
