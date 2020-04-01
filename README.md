# modular-synth
Programming and schematics for a DIY modular synthesiser

## 8-step Sequencer
This module is an Arduino-controlled sequencer, which can be programmed with 8 steps outputting different control voltages. The sequencer has the following features:
1. Using a toggle switch, the module can be used as one 8-step sequencer or two 4-step sequencers.
1. Each sequencer also has a 5V gate CV output.
1. The sequencer can be controlled from an internal clock, and the tempo and rhythm of the sequencer can be modified.
1. The order in which each step is played can be modified (up to 32 steps total in the sequence).
1. The gate length can be modified as a fraction of the note length.
1. The module also has a clock input, which can be used to increment the sequencer to the next step.