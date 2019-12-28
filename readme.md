This is a project to monitor Nintendo 64 controller activity and forward the controller state to a host computer over USB. The distinguishing characteristic of this project is that two controller lines can be monitored at the same time. This is implemented on an Arduino Duo.

# Hardware

You'll need

- Nintendo 64 console
- One or two controllers
- A sacrifical extension cord for each controller
- An Arduino Duo.

In order for this to work, the data line of the controller needs to be connected to the Arduino. I took a controller extension cord and cut it in half. There should be three wires inside, ground, power, and data. The ground and data lines are the only thing needed, but for completeness, I attached a wire to each. I soldered the three parts together (each half of the extension cord + new wire) for each wire and taped over everything. Now connect controller 1 data line to Arduino pin 2, controller 2 data line to Arduino pin 3, and both controller ground lines should connect to the board ground.

# Controller communication protocol

The Nintendo 64 console sends a "read" message to the controller consisting of 8 bits and a stop bit. Each bit starts with the falling edge of the signal and lasts for 4 microseconds. A logical zero is indicated by the line being held low for 3 microseconds, then high for 1 microsecond. A logical one is indicated by the reverse, 1us low then 3us high.

The controller then (immediately) responds with 32 bits to indicate the controller state and then a stop bit. State is indicated by:

- [byte:bit]
- [0:0] a button
- [0:1] b button
- [0:2] z button
- [0:3] start button
- [0:4] d pad up
- [0:5] d pad down
- [0:6] d pad left
- [0:7] d pad right
- [1:0] unused
- [1:1] unused
- [1:2] left shoulder
- [1:3] right shoulder
- [1:4] c up
- [1:5] c down
- [1:6] c left
- [1:7] c right
- [2] analog stick x position
- [3] analog stick y position

Note that the analog position byte is read in reverse order. Also note that the range is only one byte. Zero is netrual, positive is right/up, negative is left/down.

After these 41 bits (~164 us), the line is held high for close to 1ms. I guess the controller is only read once per framerate.

# Software overview

Each data line is read in round robin fashion. The read starts by waiting for the next frame. Each of the 32 controller bits is read into a 32 bit buffer (4 bytes). After the line is read, this should be in the dead time between reads (~1 frame), this is when transmition over the serial port occurs. There's a simply protocol, each update to the host pc is an 8 byte packet. It starts with `'>'` followed by ASCII controller port (`'1'` or `'2'`), followed by `':'`. Next comes the raw 4 byte state of the controller, and finally a close indicator, `'|'`. That should have completed before the next frame, and the Duo then goes on to repeat this process for the next controller.

If there is no controller connected on one of the ports, or if a controller gets disconnected, the firmware will stop trying to read after too many failures. To enable reading again, hit the reset button on the Arduino board.

# Limitations

There's some input delay, on the order of 100ms +/- 100ms (I don't have the tools to measure this). It's not *too* bad but just enough to be noticeable. I spent a long time trying to change how state was reading, adjusting delays, trying to handle different things in ISRs, but the current implementation is the best I can get with the Arduino Duo. There may be better techniques that I'm not aware of that can handle two input lines better on the Duo, but I think a faster microprocessor is needed to really handle this with no latency.