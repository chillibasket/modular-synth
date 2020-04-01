/* 
 * Modular Synth - 8-step Sequencer
 * Simon Bluett (hello@chillibasket.com)
 * 1st April 2020
 * Version: 1.0
 * (C) 2020, GNU General Public Licence v3.0
 */


/* ------------------------------
 * DEFINE ALL CONSTANTS
 * ------------------------------*/
// Shift Register Pin Mapping
#define SR_DP 2 	// Data Pin
#define SR_CL 4 	// Clock Pin
#define SR_LT 3 	// Latch Pin

// Gate/trigger Pin Mapping
#define GATE_A 11
#define GATE_B 13

// Button Pin Mapping
#define BT_MENU 12
#define BT_STP_1 6
#define BT_STP_2 7
#define BT_STP_3 8
#define BT_STP_4 9
#define BT_STP_5 A0
#define BT_STP_6 A1
#define BT_STP_7 A2
#define BT_STP_8 A3
#define SW_STEPS 10

// Analogue inputs
#define VOLT_A A7
#define VOLT_B A6

// Clock input
#define CLOCK_IN 5

// Timing definitions
#define DEBOUNCE_TIME 50
#define MENU_TIMEOUT 25000
#define SYNC_PERIOD 500
#define TRIGGER_TIME 20
#define DISPLAY_REFRESH 100

/* ------------------------------
 * OLED DISPLAY CLASS
 * ------------------------------*/
#include <TimerOne.h>
#include <Wire.h>
#include <U8g2lib.h>
U8G2_SSD1306_128X64_NONAME_2_HW_I2C display(U8G2_R0);


/* ------------------------------
 * DEFINE THE MENU STRUCTURE
 * ------------------------------*/

struct MenuItem {
	char name[16]; 		// Text to display for menu item
	uint8_t current; 	// Index of heading which relates to item: 0=main menu, 1=sequencer settings, 2=custom input
	int8_t child; 		// Index of heading to go to when pressing ENTER button
	char action; 		// Name of the action which should be done when pressing ENTER button
	
	// For all boolean flags, pack them together into one byte
	struct {
		unsigned select:1; 		// If TRUE, The "action" will only be called when item is selected
		unsigned enabled:1; 	// Show or hide the menu item
	};
};

// Add all of the menu items and their respective actions
#define MENU_ITEMS 16
//                              Item Name,Cu,Ch, Act,  slct, enbld
MenuItem menu[] = { {       "Sequencer A", 0, 1, 'A', false,  true},
                    {       "Sequencer B", 0, 1, 'B', false,  true},
                    {             "Pause", 1, 1, 'p', false,  true},
                    {      "Step Forward", 1, 1, 'f', false, false},
                    {     "Step Backward", 1, 1, 'b', false, false},
                    {              "Stop", 1, 1, 'e', false,  true},
                        {"Tempo bpm: 100", 1, 1, 't',  true,  true},
                        {"No. Steps:   4", 1, 1, 's',  true,  true},
                        {"No. Beats:   4", 1, 1, 'l',  true,  true},
                        {"Gate Len:  100", 1, 1, 'g',  true,  true},
                        {"Clock in:   No", 1, 1, 'c',  true,  true},
                    {      "Custom Input", 1, 2,   0, false,  true},
                        {  "New Sequence", 2, 2, 'S', false,  true},
                        {    "New Rhythm", 2, 2, 'R', false,  true},
                        {    "Play both!", 2, 2, 'P', false,  true},
                    {        "Stop A & B", 0, 0, 'E', false,  true}
                  };


/* ------------------------------
 * DEFINE RUNTIME VARIABLS
 * ------------------------------*/
 
// Structure for main runtime variables
struct RuntimeVars {
	int8_t head; 		// Current menu page (heading)
	uint8_t item; 		// Current menu item
	unsigned long menuTimer;

	// Package variables into 1 single Byte
	struct {
		unsigned menu:1;		// If menu is currently open
		unsigned selected:1; 	// If menu item is selected
		unsigned seqSel:1;  	// Select which sequencer is being modified in the menu
		unsigned bothSeq:1; 	// If both sequencers are active
		volatile unsigned update:1; // Whether to update the OLED display
		unsigned mode:3; 		// Current mode; 0=off, 1=normal, 2=sequence, 3=rhythm, 4=custom
	};
};

// Instantiate the variables
RuntimeVars runtime = {0, 0, 0, false, false, false, true, true, 1};

// Structure for sequencer state variables
struct SequencerState {
	volatile int8_t current;
	uint8_t total;
	uint16_t tempo;
	uint8_t beats;
	volatile unsigned long timerNote;
	volatile unsigned long timerGate;
	struct {
		unsigned active:1; 		// Whether current sequencer is playing/paused
		unsigned clockin:1; 	// Whether it is using the clock input to proceed to next note
		unsigned customRhy:1; 	// If the rhythm has been customised
		unsigned customSeq:1; 	// If the sequence has been customised
		unsigned overide:4; 	// Play a custom note using the buttons
	};
};

// Instantiate variables for each sequencer
SequencerState seq[] = {{0, 4, 100, 4, 0, 0, true, false, false, false, 0},
                        {0, 4, 100, 4, 0, 0, true, false, false, false, 0}};

// Structure for sequence data
struct SavedSequence {
	struct {
		unsigned step:3;
		unsigned length:13;
	};
	uint8_t gate;
};

#define LEN_MAX 8192
#define GATE_MAX 256
#define SEQ_MAX 32
SavedSequence sequence[SEQ_MAX];


/* ------------------------------
 * SETUP GLOBAL RUNTIME VARIABLES
 * ------------------------------*/

// Time keeping
unsigned long startTime = 0;
int totalTime = 0;
unsigned int voltRead = 0;

// Clock input
bool clockState = true;

// Button management
bool buttonState[9] = {1,1,1,1,1,1,1,1,1};
unsigned long debounce[9] = {0,0,0,0,0,0,0,0,0};



/******************************************************
 * SETUP FUNCTION - this is run once at startup
 ******************************************************/
void setup() {

#ifdef DEBUG
	// Begin serial communication for debugging
	Serial.begin(115200);
	delay(1000);
	Serial.println(F("---- Starting Sequencer ----"));
#endif //DEBUG

	// Setup buttons and switches as inputs
	pinMode(BT_MENU, INPUT_PULLUP);
	pinMode(BT_STP_1, INPUT_PULLUP);
	pinMode(BT_STP_2, INPUT_PULLUP);
	pinMode(BT_STP_3, INPUT_PULLUP);
	pinMode(BT_STP_4, INPUT_PULLUP);
	pinMode(BT_STP_5, INPUT_PULLUP);
	pinMode(BT_STP_6, INPUT_PULLUP);
	pinMode(BT_STP_7, INPUT_PULLUP);
	pinMode(BT_STP_8, INPUT_PULLUP);
	pinMode(SW_STEPS, INPUT_PULLUP);

	// Setup clock input
	pinMode(CLOCK_IN, INPUT_PULLUP);

	// Set shift register pins
	pinMode(SR_DP, OUTPUT);
	pinMode(SR_CL, OUTPUT);  
	pinMode(SR_LT, OUTPUT);

	// Gate pins
	pinMode(GATE_A, OUTPUT);  
	pinMode(GATE_B, OUTPUT);
	digitalWrite(GATE_A, LOW);
	digitalWrite(GATE_B, LOW);

	// Start OLED display
	display.begin();

	// Check the state of the step select switch
	bool stepState = digitalRead(SW_STEPS);
	switchStepMode(stepState);

	// Set up timer
	Timer1.initialize(20000); 	// Every 0.02 seconds, Frequency = 50Hz
	Timer1.attachInterrupt(updateStepTimer);
}


/******************************************************
 * Switch between one 8-STEP or two 4-STEP sequencers
 ******************************************************/
void switchStepMode(bool state) {

#ifdef DEBUG
	Serial.print(F("Change Single/Dual Seq. Mode: ")); Serial.println(!state);
#endif //DEBUG

	noInterrupts();
	seq[0].overide = 0;
	seq[1].overide = 0;

	// Reset both sequencers
	if (seq[0].current > 0) seq[0].current = 0;
	if (seq[1].current > 0) seq[1].current = 0;

	// Total number of steps and beats
	if (!state) {
		seq[0].total = 8;
		seq[0].beats = 8;
	} else {
		seq[0].total = 4;
		seq[0].beats = 4;
		seq[1].total = 4;
		seq[1].beats = 4;
	}

	// Turn off any custom sequences
	seq[0].customRhy = false;
	seq[1].customRhy = false;

	// Turn off any custom rhythms
	seq[0].customSeq = false;
	seq[1].customSeq = false;

	// Turn on/off sequencer B
	runtime.bothSeq = state;
	runtime.seqSel = false;
	runtime.selected = false;
	runtime.update = true;
	runtime.mode = 1;
	if (seq[1].current >= 0) seq[1].active = state;

	// Reset all the saved data to the default
	for (uint8_t i = 0; i < SEQ_MAX; i++) {
		
		// For active steps in sequencer A
		if (runtime.bothSeq && i < 4) {
			sequence[i].step = i;
			sequence[i].length = 2048;
			sequence[i].gate = 255;

		// If only sequencer A is active (not B)
		} else if (!runtime.bothSeq && i < 8) {
			sequence[i].step = i;
			sequence[i].length = 1024;
			sequence[i].gate = 255;

		// For active steps in sequencer B
		} else if (runtime.bothSeq && (i >= (SEQ_MAX / 2)) && (i < (SEQ_MAX / 2) + 4)) {
			sequence[i].step = i - (SEQ_MAX / 2);
			sequence[i].length = 2048;
			sequence[i].gate = 255;
		
		// Else for all the inactive steps
		} else {
			sequence[i].step = 0;
			sequence[i].length = 0;
			sequence[i].gate = 0;
		}
	}

	// Update menu options
	if (state) {
		changeMenuValues(true);
		runtime.head = 0;
		runtime.item = 0;
	} else {
		runtime.seqSel = false;
		changeMenuValues(false);
		runtime.head = 1;
		runtime.item = 0;
	}

	// Update outputs to reflect the changes
	playCurrentNotes();

	// Update note timings
	unsigned long initialTime = millis();
	calculateNoteTiming(0, initialTime);
	if (runtime.bothSeq && seq[1].active) calculateNoteTiming(1, initialTime); 

	interrupts();
}


/******************************************************
 * PLAY NOTES CURRENTLY IN SEQUENCE
 ******************************************************/
void playCurrentNotes() {
	if (runtime.bothSeq && seq[0].current >= 0 && seq[1].current >= 0) updateShiftRegister(sequence[seq[0].current].step + 1, sequence[seq[1].current + (SEQ_MAX / 2)].step + 1);
	else if (runtime.bothSeq && seq[0].current < 0 && seq[1].current >= 0) updateShiftRegister(0, sequence[seq[1].current + (SEQ_MAX / 2)].step + 1);
	else if (seq[0].current >= 0) updateShiftRegister(sequence[seq[0].current].step + 1, 0);
	else updateShiftRegister(0, 0);
	if (!runtime.menu) runtime.update = true;
}


/******************************************************
 * UPDATE THE OUTPUT SHIFT REGISTER
 ******************************************************/
void updateShiftRegister(int seq1, int seq2) {

	if (seq[0].overide != 0) seq1 = seq[0].overide;
	if (seq[1].overide != 0) seq2 = seq[1].overide;

	byte bitsToSend = 0;

	if (seq1 > 0 && seq1 < 5) {
		bitWrite(bitsToSend, seq1 - 1, 1);
	} else if (seq1 > 4 && seq1 < 9 && seq2 == 0) {
		bitWrite(bitsToSend, seq1 - 1, 1);
	}

	if (seq2 > 0 && seq2 < 5) {
		bitWrite(bitsToSend, seq2 + 3, 1);
	}

	// Shift out the bits to the shift register
	digitalWrite(SR_LT, LOW);
	shiftOut(SR_DP, SR_CL, MSBFIRST, bitsToSend);
	digitalWrite(SR_LT, HIGH);
}


/******************************************************
 * CALCULATE TIMING FOR NEXT NOTE IN SEQUENCE
 ******************************************************/
void calculateNoteTiming(uint8_t device, unsigned long initialTime) {

	if (!seq[device].clockin) {
		uint8_t offset =  0;
		if (device == 1) offset = SEQ_MAX / 2;

		// Calculate the note time
		unsigned long noteLength = int((60000.0 / seq[device].tempo) * ((sequence[seq[device].current + offset].length + 1) / float(LEN_MAX)) * seq[device].beats);

		// Update gate and trigger length timers
		seq[device].timerGate = initialTime + int(noteLength * (sequence[seq[device].current + offset].gate + 1) / float(GATE_MAX)) - 1;
		
		// Update note length timer
		seq[device].timerNote = initialTime + noteLength - 1;

	} else seq[device].timerNote = 0;
	
	if (device == 0) {
		digitalWrite(GATE_A, HIGH);
		if (!runtime.bothSeq) {
			digitalWrite(GATE_B, HIGH);
			seq[1].timerGate = initialTime + TRIGGER_TIME;
		}
	} else {
		digitalWrite(GATE_B, HIGH);
	}
}


/******************************************************
 * FINISH RECORDING CUSTOM STEP SEQUENCE
 ******************************************************/
void finishSequenceRec() {

	// Turn off the gate
	if (!runtime.seqSel) digitalWrite(GATE_A, LOW);
	else digitalWrite(GATE_B, LOW);

	noInterrupts();
	runtime.mode = 1;
	seq[runtime.seqSel].current = 0;

	// Now update all the rhythm and gate timings
	uint8_t offset = 0;
	if (runtime.seqSel) offset = SEQ_MAX / 2;
	for (uint8_t j = 0; j < seq[runtime.seqSel].total; j++) {
		sequence[j + offset].length = round(LEN_MAX / float(seq[runtime.seqSel].total)) - 1;
		sequence[j + offset].gate = (GATE_MAX / 2) - 1;
	}

	seq[runtime.seqSel].active = true;
	calculateNoteTiming(runtime.seqSel, millis());
	interrupts();

	sprintf(menu[7].name, "No. Steps: %3u", seq[runtime.seqSel].total);
}


/******************************************************
 * FINISH RECORDING CUSTOM RHYTHM AND STEP SEQUENCE
 ******************************************************/
void finishRecording() {
	unsigned long currentTime = startTime + totalTime;

	// Turn off the gate
	if (!runtime.seqSel) digitalWrite(GATE_A, LOW);
	else digitalWrite(GATE_B, LOW);

	seq[runtime.seqSel].current++;

	// Save the timings of the previous step
	uint8_t offset = 0;
	if (runtime.seqSel) offset = SEQ_MAX / 2;
	recordStepTimings(offset, currentTime);

	// If some steps haven't been filled, set them to zero
	while (seq[runtime.seqSel].current < seq[runtime.seqSel].total - 1) {
		seq[runtime.seqSel].current++;
		sequence[seq[runtime.seqSel].current + offset - 1].gate = 0;
		sequence[seq[runtime.seqSel].current + offset - 1].length = 0;
	}

	// If it doesn't add up, readjust the lengths of all the steps until it does
	addUpLengths(offset);

	noInterrupts();
	runtime.mode = 1;
	seq[runtime.seqSel].current = 0;

	seq[runtime.seqSel].active = true;
	calculateNoteTiming(runtime.seqSel, millis());
	interrupts();

	sprintf(menu[7].name, "No. Steps: %3u", seq[runtime.seqSel].total);
}


/******************************************************
 * CALCULATE CUSTOM NOTE AND GATE LENGTHS 
 ******************************************************/
void recordStepTimings(uint8_t offset, unsigned long currentTime) {
	sequence[seq[runtime.seqSel].current + offset - 1].length = round((currentTime - seq[runtime.seqSel].timerNote) * LEN_MAX / totalTime);
	if (seq[runtime.seqSel].timerGate == 0) sequence[seq[runtime.seqSel].current + offset - 1].gate = 0;
	else if (seq[runtime.seqSel].timerGate <= seq[runtime.seqSel].timerNote) sequence[seq[runtime.seqSel].current + offset - 1].gate = GATE_MAX - 1;
	else sequence[seq[runtime.seqSel].current + offset - 1].gate = round((seq[runtime.seqSel].timerGate - seq[runtime.seqSel].timerNote) * (GATE_MAX - 1) / float(currentTime - seq[runtime.seqSel].timerNote));
	if (sequence[seq[runtime.seqSel].current + offset - 1].gate < 50) sequence[seq[runtime.seqSel].current + offset - 1].gate = 50;
}


/******************************************************
 * ENSURE NOTE LENGTHS ADD UP PROPERLY
 ******************************************************/
void addUpLengths(uint8_t offset) {
	// Ensure that the rhythm adds up properly
	unsigned int sum = 0;
	for (uint8_t j = 0; j < seq[runtime.seqSel].total; j++) sum += sequence[j + offset].length;

	uint8_t j = 0;
	while (abs(sum - (LEN_MAX - 1)) != 0) {
		if (sum < LEN_MAX) {
			if (sequence[j + offset].length < LEN_MAX - 1) {
				sequence[j + offset].length++;
				sum++;
			}
		} else {
			if (sequence[j + offset].length > 0) {
				sequence[j + offset].length--;
				sum--;
			}
		}
		j++;

		if (j >= seq[runtime.seqSel].total) j = 0;
	}
}


/******************************************************
 * UPDATE THE MENU TEXT TO REFLECT SELECTED SEQUENCER
 ******************************************************/
void changeMenuValues(bool seqSelect) {

	runtime.head = 1;
	runtime.item = 0;
	runtime.seqSel = seqSelect;
	runtime.update = true;

	uint8_t i = 0;
	if (seqSelect) i = 1;

	// If clock input is active
	if (seq[i].clockin) {
		menu[6].enabled = false; 	// tempo
		menu[8].enabled = false; 	// beats
		menu[13].enabled = false; 	// rhythm input
		menu[14].enabled = false; 	// both input
		strcpy(menu[10].name, "Clock in:  Yes");
		
	// Else if clock input is not active
	} else {
		menu[6].enabled = true; 	// tempo
		menu[8].enabled = true; 	// beats
		menu[13].enabled = true; 	// rhythm input
		menu[14].enabled = true; 	// both input
		strcpy(menu[10].name, "Clock in:   No");
	}

	// If a custom rhythm has been inputted
	if (seq[i].customRhy && !seq[i].clockin) {
		menu[7].select = false; 	// steps
		menu[9].enabled = true; 	// gate
		menu[10].select = false; 	// clock in
		strcpy(menu[13].name, "Reset Rhythm");

	// Else if the rhythm is the default
	} else {
		menu[7].select = true; 		// steps
		menu[9].enabled = true; 	// gate
		menu[10].select = true; 	// clock in
		strcpy(menu[13].name, "New Rhythm");
	}

	// If a custom sequence has been inputted
	if (seq[i].customSeq) {
		menu[7].select = false; 	// steps
		strcpy(menu[12].name, "Reset Sequence");

	// Else if sequence is default	
	} else {
		strcpy(menu[12].name, "New Sequence");
	}

	// No matter whether they are active or not, update all the text for the menu items
	sprintf(menu[6].name, "Tempo bpm: %3u", seq[i].tempo);
	sprintf(menu[7].name, "No. Steps: %3u", seq[i].total);
	sprintf(menu[8].name, "No. Beats: %3u", seq[i].beats);
	if (i == 0) sprintf(menu[9].name, "Gate Len:  %3u", (uint8_t) round(sequence[0].gate * 100 / (GATE_MAX - 1)));
	else sprintf(menu[9].name, "Gate Len:  %3u", (uint8_t) round(sequence[SEQ_MAX / 2].gate * 100 / (GATE_MAX - 1)));

	// If both sequence and rhythm are custom
	if (seq[i].customRhy && seq[i].customSeq) {
		strcpy(menu[14].name, "Reset Both!");
	} else {
		strcpy(menu[14].name, "Play Both!");
	}

	// If sequencer is active
	if (seq[i].active) {

		menu[3].enabled = false; 	// step forward
		menu[4].enabled = false; 	// step backward
		menu[5].enabled = true; 	// stop
		strcpy(menu[2].name, "Pause");

	// Else if sequencer is paused
	} else if (seq[i].current >= 0) {

		menu[3].enabled = true; 	// step forward
		menu[4].enabled = true; 	// step backward
		menu[5].enabled = true; 	// stop
		strcpy(menu[2].name, "Play");

	// Else if the sequencer is stopped
	} else {

		menu[3].enabled = false; 	// step forward
		menu[4].enabled = false; 	// step backward
		menu[5].enabled = false; 	// stop
		strcpy(menu[2].name, "Play");
	}
}


/******************************************************
 * PERFORM ANY ACTIONS SELECTED IN THE MENU
 ******************************************************/
void performMenuAction(char action, int button) {
	
	uint8_t i = 0;
	if (runtime.bothSeq && runtime.seqSel) i = 1;

	switch (action) {

		// Sequencer A selected
		case 'A':
			changeMenuValues(false);
			break;

		case 'B':
			changeMenuValues(true);
			break;

		// 'p' = Play/Pause
		case 'p':
			{
				// If we are currently playing
				if (seq[i].active) {
					noInterrupts();
					seq[i].active = false;
					interrupts();
					strcpy(menu[2].name, "Play");
					menu[3].enabled = true;
					menu[4].enabled = true;
				} else {
					strcpy(menu[2].name, "Pause");
					strcpy(menu[15].name, "Stop A & B");
					menu[3].enabled = false;
					menu[4].enabled = false;
					menu[5].enabled = true;
					
					seq[i].active = true;

					if (!seq[i].clockin) { 
						// If we are close to the initial beat in the other sequencer, try to synchronise them...
						unsigned long initialTime = millis();
						noInterrupts();

						if (seq[i].current < 0) seq[i].current = 0;
						if (seq[i].current == 0) {
							uint8_t otherSeq = 0;
							if (i == 0) otherSeq = 1;
							
							if ((seq[otherSeq].current == seq[otherSeq].total - 1) && (seq[otherSeq].timerNote - initialTime < SYNC_PERIOD)) {
								initialTime = seq[otherSeq].timerNote;
							} else if (seq[otherSeq].current == 0) {
								int period = int((60000.0 / seq[otherSeq].tempo) * ((sequence[seq[otherSeq].current].length + 1) / float(LEN_MAX)) * seq[otherSeq].beats) - 1;
								if (initialTime - seq[otherSeq].timerNote + period < SYNC_PERIOD) {
									initialTime = seq[otherSeq].timerNote - period;
								}
							}
						}

						calculateNoteTiming(i, initialTime);
					}

					playCurrentNotes();
					interrupts();
				}
			}
			break;

		// 'f' = One step forward
		case 'f':
			{
				noInterrupts();
				seq[i].current++;
				if (seq[i].current >= seq[i].total) seq[i].current = 0;
				playCurrentNotes();
				interrupts();
			}
			break;

		// 'b' = One step backward
		case 'b':
			{
				noInterrupts();
				seq[i].current--;
				if (seq[i].current < 0) seq[i].current = seq[i].total - 1;
				playCurrentNotes();
				interrupts();
			}
			break;

		// 'e' = Stop
		case 'e':
			{
				runtime.item = 0;
				strcpy(menu[2].name, "Play");
				if (seq[abs(i - 1)].current == -1) strcpy(menu[15].name, "Start A & B");
				menu[3].enabled = false;
				menu[4].enabled = false;
				menu[5].enabled = false;

				noInterrupts();
				seq[i].active = false;
				seq[i].current = -1;
				playCurrentNotes();
				interrupts();
			}
			break;

		// 'E' = Start/stop both sequencers
		case 'E':
			{
				// Stop
				if (seq[0].current != -1 || seq[1].current != -1) {
					strcpy(menu[2].name, "Play");
					menu[3].enabled = false;
					menu[4].enabled = false;
					menu[5].enabled = false;
					strcpy(menu[15].name, "Start A & B");

					noInterrupts();
					seq[0].active = false;
					seq[0].current = -1;
					seq[1].active = false;
					seq[1].current = -1;
					playCurrentNotes();
					interrupts();
				// Else start	
				} else {
					strcpy(menu[2].name, "Pause");
					menu[3].enabled = false;
					menu[4].enabled = false;
					menu[5].enabled = true;
					strcpy(menu[15].name, "Stop A & B");

					noInterrupts();
					seq[0].active = true;
					seq[0].current = 0;
					seq[1].active = true;
					seq[1].current = 0;
					unsigned long initialTime = millis();
					calculateNoteTiming(0, initialTime);
					calculateNoteTiming(1, initialTime);

					playCurrentNotes();
					interrupts();
				}
			}

		// 't' = Tempo
		case 't':
			{
				noInterrupts();
				if (button == 1) seq[i].tempo += 10;
				else if (button == 2) seq[i].tempo -= 10;
				if (seq[i].tempo > 600) seq[i].tempo = 600;
				else if (seq[i].tempo < 10) seq[i].tempo = 10;
				interrupts();
				sprintf(menu[6].name, "Tempo bpm: %3u", seq[i].tempo);
			}
			break;

		// 's' = Change steps
		case 's':
			{
				if (!seq[i].customSeq && !seq[i].customRhy) {
					noInterrupts();
					if (button == 1) seq[i].total++;
					else if (button == 2) seq[i].total--;

					// Make sure total remains within bounds
					if (runtime.bothSeq && (seq[i].total > SEQ_MAX / 2)) seq[i].total = SEQ_MAX / 2;
					else if (!runtime.bothSeq && seq[i].total > SEQ_MAX) seq[i].total = SEQ_MAX;
					else if (seq[i].total < 1) seq[i].total = 1;

					// Update the sequence to include the additional step
					else {
						uint8_t offset = 0, divisor = 4;
						if (i == 1) offset = SEQ_MAX / 2;
						if (!runtime.bothSeq) divisor = 8;

						for (uint8_t j = 0; j < seq[i].total; j++) {
							sequence[j + offset].step = j % divisor;
							sequence[j + offset].length = round(LEN_MAX / float(seq[i].total)) - 1;
							sequence[j + offset].gate = sequence[offset].gate;
						}
					}
					interrupts();
					sprintf(menu[7].name, "No. Steps: %3u", seq[i].total);
				}
			}
			break;

		// 'l' = no of beats (length)
		case 'l':
			{
				noInterrupts();
				if (button == 1) {
					if (seq[i].beats < 100) seq[i].beats++;
				} else if (button == 2) {
					if (seq[i].beats > 1) seq[i].beats--;
				}
				interrupts();
				sprintf(menu[8].name, "No. Beats: %3u", seq[i].beats);
			}
			break;

		// 'g' = gate length
		case 'g':
			{
				// Retrieve value
				uint8_t offset = 0;
				if (i == 1) offset = SEQ_MAX / 2;
				uint8_t gateVal = sequence[offset].gate;

				noInterrupts();
				// Update value
				if (button == 1) {
					if (gateVal < GATE_MAX - 1) gateVal += 5;
				} else if (button == 2) {
					if (gateVal > 0) gateVal -= 5;
				}

				// Save value back into sequencer array
				for (uint8_t j = 0; j < seq[i].total; j++) {
					sequence[j + offset].gate = gateVal;
				}
				interrupts();

				sprintf(menu[9].name, "Gate Len:  %3u", (uint8_t) round(gateVal * 100 / (GATE_MAX - 1)));
			}
			break;

		// 'c' = clock input
		case 'c':
			{
				if (!seq[i].customRhy) {
					// Turn off
					if (seq[i].clockin) {
						noInterrupts();
						seq[i].clockin = false;
						seq[i].timerNote = millis() + 499;
						interrupts();
						menu[6].enabled = true; 	// tempo
						menu[8].enabled = true; 	// beats
						menu[13].enabled = true; 	// rhythm input
						menu[14].enabled = true; 	// both input
						strcpy(menu[10].name, "Clock in:   No");

					// Turn on
					} else {
						noInterrupts();
						seq[i].clockin = true;
						seq[i].timerNote = 0;
						interrupts();
						menu[6].enabled = false; 	// tempo
						menu[8].enabled = false; 	// beats
						menu[13].enabled = false; 	// rhythm input
						menu[14].enabled = false; 	// both input
						strcpy(menu[10].name, "Clock in:  Yes");
					}

					// Update the current selected item
					runtime.item = 0;
					for (uint8_t i = 0; i < MENU_ITEMS; i++) {
						if (menu[i].current == runtime.head && menu[i].enabled) {
							if (i < 10) runtime.item++;
							else break;
						}
					}
				}
			}
			break;

		// 'S' = custom sequence input
		case 'S':
			{
				// If a custom sequence is already active, reset it
				if (seq[i].customSeq) {
					seq[i].customSeq = false;
					strcpy(menu[12].name, "New Sequence");
					strcpy(menu[14].name, "Play Both!");
					uint8_t offset = 0, divisor = 4;
					if (i == 1) offset = SEQ_MAX / 2;
					if (!runtime.bothSeq) divisor = 8;

					noInterrupts();
					for (uint8_t j = 0; j < seq[i].total; j++) {
						sequence[j + offset].step = j % divisor;
					}
					interrupts();

				// Otherwise, set program into sequence recording mode
				} else {
					seq[i].customSeq = true;
					strcpy(menu[12].name, "Reset Sequence");
					if (seq[i].customRhy) strcpy(menu[14].name, "Reset Both!");

					noInterrupts();
					runtime.mode = 2;
					runtime.menu = false;
					runtime.menuTimer = 0;
					seq[i].current = -1;
					seq[i].total = 0;
					seq[i].active = false;
					playCurrentNotes();
					interrupts();
				}
			}
			break;

		// 'R' = custom rhythm input
		case 'R':
			{
				// If a custom rhythm is already active, reset it
				if (seq[i].customRhy) {
					seq[i].customRhy = false;
					uint8_t offset = 0;
					if (i == 1) offset = SEQ_MAX / 2;
					strcpy(menu[13].name, "New Rhythm");
					strcpy(menu[14].name, "Play Both!");
					menu[7].select = true; 	// steps

					noInterrupts();
					for (uint8_t j = 0; j < seq[i].total; j++) {
						sequence[j + offset].length = round(LEN_MAX / float(seq[i].total)) - 1;
						sequence[j + offset].gate = (GATE_MAX / 2) - 1;
					}
					interrupts();

				// Otherwise, set program into sequence recording mode
				} else {
					seq[i].customRhy = true;
					strcpy(menu[13].name, "Reset Rhythm");
					if (seq[i].customSeq) strcpy(menu[14].name, "Reset Both!");
					menu[7].select = false; 	// steps

					noInterrupts();
					runtime.mode = 3;
					runtime.menu = false;
					runtime.menuTimer = 0;
					startTime = 0;
					seq[i].current = -1;
					seq[i].active = false;
					totalTime = int(60000.0 / seq[i].tempo * seq[i].beats);
					playCurrentNotes();
					interrupts();
				}
			}
			break;

		// 'P' = play both input
		case 'P':
			{
				// If both a custom sequence and rhythm are already active, reset them
				if (seq[i].customSeq && seq[i].customRhy) {
					performMenuAction('S', button);
					performMenuAction('R', button);
					strcpy(menu[14].name, "Play Both!");

				// Otherwise, set program into sequence and rhythm recording mode
				} else {
					seq[i].customSeq = true;
					seq[i].customRhy = true;
					strcpy(menu[12].name, "Reset Sequence");
					strcpy(menu[13].name, "Reset Rhythm");
					strcpy(menu[14].name, "Reset Both!");
					menu[7].select = false; 	// steps
					
					noInterrupts();
					runtime.mode = 4;
					runtime.menu = false;
					runtime.menuTimer = 0;
					startTime = 0;
					seq[i].current = -1;
					seq[i].total = 0;
					seq[i].active = false;
					totalTime = int(60000.0 / seq[i].tempo * seq[i].beats);
					playCurrentNotes();
					interrupts();
				}
			}
			break;
	}
	
}


/******************************************************
 * BUTTON MANAGEMENT FUNCTION
 ******************************************************/
void buttonPress(int button, bool value) {
	unsigned long currentTime = millis();

	// If button state has changed, and debounce timer has expired
	if (buttonState[button] != value && debounce[button] < currentTime) {

#ifdef DEBUG
		Serial.print(F("Button: ")); Serial.print(button);
		Serial.print(F(", Value: ")); Serial.println(value);
#endif

		// If button is being pressed
		if (value == 0) {

			// Update button state and initiate debounce timer
			buttonState[button] = value;
			debounce[button] = currentTime + DEBOUNCE_TIME;

			// If this is the MENU button
			if (button == 8) {

				// Finish recording the sequence
				if (runtime.mode == 2) {
					finishSequenceRec();

				// Start sequence
				} else if (runtime.mode == 3 || runtime.mode == 4) {
					seq[runtime.seqSel].current++;
					if (seq[runtime.seqSel].current == 0) startTime = currentTime;
					if (runtime.mode == 4) sequence[seq[runtime.seqSel].current].step = 0;
					seq[runtime.seqSel].timerNote = currentTime;
					seq[runtime.seqSel].timerGate = 0;
					sequence[seq[runtime.seqSel].current].gate = 0;

				// Normal menu function
				} else {
					runtime.menu = !runtime.menu;
					runtime.selected = false;
					if (runtime.bothSeq) runtime.head = 0;
					else runtime.head = 1;
					runtime.item = 0;
					if (runtime.menu) {
						seq[0].overide = 0;
						seq[1].overide = 0;
						runtime.menuTimer = currentTime + MENU_TIMEOUT;
					} else {
						runtime.menuTimer = 0;
					}
				}

			// Else if the MENU is open
			} else if (runtime.menu) {

				runtime.menuTimer = currentTime + MENU_TIMEOUT;

				// Figure out what the number of the current item is, and if there is a next item
				uint8_t itemIndex = 0;
				uint8_t count = 0;
				for (uint8_t i = 0; i < MENU_ITEMS; i++) {
					if (menu[i].current == runtime.head && menu[i].enabled) {
						if (count == runtime.item) itemIndex = i;
						count++;
					}
				}

				// Enter button
				if (button == 3) {

					// If the current item has the ability to be selected
					if (menu[itemIndex].select) {
						runtime.selected = !runtime.selected;
					} else {

						// Undertake the action required by the menu item
						if (menu[itemIndex].action != 0) {
							performMenuAction(menu[itemIndex].action, button);
						}

						// Update the menu level if required
						if (runtime.head != menu[itemIndex].child) {
							runtime.head = menu[itemIndex].child;
							runtime.item = 0;
						}
					}

				// Up Arrow button	
				} else if (button == 1) {

					// If an item is selected, modify that item's value
					if (runtime.selected && menu[itemIndex].action != 0) {
						performMenuAction(menu[itemIndex].action, button);
					
					// Otherwise move up to the previous item
					} else if (!runtime.selected) {
						if (runtime.item > 0) runtime.item--;
					}

				// Down Arrow button
				} else if (button == 2) {

					// If an item is selected, modify that item's value
					if (runtime.selected) {
						if (menu[itemIndex].action != 0) performMenuAction(menu[itemIndex].action, button);
					// Otherwise move down to the next item
					} else if (runtime.item < count - 1) runtime.item++;

				// Back button
				} else if (button == 0) {

					// Unselect any highlight items
					if (runtime.selected) runtime.selected = false;

					// Otherwise move back one menu level
					else {
						// If we were in the top level menu already, exit the menu
						if (runtime.head == 0 || (!runtime.bothSeq && runtime.head == 1)) {
							runtime.menu = false;
						}

						runtime.head--;
						runtime.item = 0;
					}
				}

			// Not in menu
			} else {

				// Play a single note
				if (runtime.mode == 1) {
					noInterrupts();
					if (!runtime.bothSeq || button < 4) {
						int value = int(analogRead(VOLT_A) * 4.89);
						voltRead = value;
						runtime.seqSel = false;
						seq[0].overide = button + 1;
						digitalWrite(GATE_A, HIGH);
					} else if (button >= 4) {
						int value = int(analogRead(VOLT_B) * 4.89);
						voltRead = value;
						runtime.seqSel = true;
						seq[1].overide = button - 3;
						digitalWrite(GATE_B, HIGH);
					}
					playCurrentNotes();
					interrupts();

				// Record step sequence
				} else if (runtime.mode >= 2 && runtime.mode <= 4) {
					noInterrupts();

					if ((!runtime.seqSel && (button < 4 || !runtime.bothSeq)) || (runtime.seqSel && button > 3 && button < 8)) {
						
						// Finish recording if the memory is already filled
						if (runtime.mode == 3 && seq[runtime.seqSel].current + 1 >= seq[runtime.seqSel].total) finishRecording();
						else if (runtime.mode == 4 && ((runtime.bothSeq && seq[runtime.seqSel].current + 1 >= (SEQ_MAX / 2)) || (!runtime.bothSeq && seq[0].current + 1 >= SEQ_MAX))) finishRecording();

						seq[runtime.seqSel].current++;

						if (seq[runtime.seqSel].current == 0) startTime = currentTime;

						uint8_t offset = 0;
						if (runtime.seqSel) offset = SEQ_MAX / 2;

						// If we are recording the step sequence, do so now
						if (runtime.mode == 2 || runtime.mode == 4) {
							seq[runtime.seqSel].total++;
							if (!runtime.seqSel) sequence[seq[runtime.seqSel].current + offset].step = button;
							else sequence[seq[runtime.seqSel].current + offset].step = button - 4;
						}

						// If we are recording the rhythm, record the start now
						if (runtime.mode == 3 || runtime.mode == 4) {
							
							// Save the timings of the previous step
							if (seq[runtime.seqSel].current > 0) {
								recordStepTimings(offset, currentTime);
							}

							seq[runtime.seqSel].timerNote = currentTime;
							seq[runtime.seqSel].timerGate = currentTime;
						}

						// Update the notes currently being played
						playCurrentNotes();

						// Turn on the gate - don't worry about the trigger?
						if (!runtime.seqSel) digitalWrite(GATE_A, HIGH);
						else digitalWrite(GATE_B, HIGH);

						// If we have reached max number of notes
						if (runtime.mode == 2 && ((runtime.bothSeq && seq[runtime.seqSel].total >= (SEQ_MAX / 2)) || (!runtime.bothSeq && seq[0].total >= SEQ_MAX))) finishSequenceRec();
					}

					interrupts();
				}

			}

		// If button has been released
		} else {

			// Add debounce timeout
			if (debounce[button] == 0) debounce[button] = currentTime + DEBOUNCE_TIME;
			else {
				// Update button state
				buttonState[button] = value;

				// Stop playing a single note and resume sequencer
				if (runtime.mode == 1 && !runtime.menu && button != 8) {

					uint8_t startIndex = 0, stopIndex = 8, device = 0;
					if (runtime.bothSeq) {
						if (button < 4) stopIndex = 4;
						else {
							device = 1;
							startIndex = 4;
							stopIndex = 8;
						}
					}

					// Check if any other buttons are currently pressed
					bool otherActive = false;
					for (uint8_t j = startIndex; j < stopIndex; j++) {
						if (buttonState[j] == 0) otherActive = true;
					}

					if (!otherActive) {
						if (startIndex == 0) digitalWrite(GATE_A, LOW);
						else digitalWrite(GATE_B, LOW);
						seq[device].overide = 0;
						if (!seq[device].clockin) {
							seq[device].timerNote = currentTime + 499;
							seq[device].timerGate = currentTime + 499;
						}
					}
				
				// If recording rhythm
				} else if (runtime.mode == 3 || runtime.mode == 4) {
					seq[runtime.seqSel].timerGate = currentTime - DEBOUNCE_TIME;
				}
			}
		}

		runtime.update = true;

	} else if (value == 0 && debounce[button] != 0 && debounce[button] < currentTime) {
		debounce[button] = 0;
	}
}


/******************************************************
 * TEST STATE OF ALL THE BUTTONS
 ******************************************************/
void checkButtons() {
	buttonPress(0, digitalRead(BT_STP_1));
	buttonPress(1, digitalRead(BT_STP_2));
	buttonPress(2, digitalRead(BT_STP_3));
	buttonPress(3, digitalRead(BT_STP_4));
	buttonPress(4, digitalRead(BT_STP_5));
	buttonPress(5, digitalRead(BT_STP_6));
	buttonPress(6, digitalRead(BT_STP_7));
	buttonPress(7, digitalRead(BT_STP_8));
	buttonPress(8, digitalRead(BT_MENU));
}


/******************************************************
 * MAIN PROGRAM LOOP
 ******************************************************/
void loop() {

	// Check state of all the buttons
	if (runtime.mode != 3 && runtime.mode != 4) checkButtons();

	// Check the step select switch
	bool stepState = digitalRead(SW_STEPS);
	if ((stepState && !runtime.bothSeq) || (!stepState && runtime.bothSeq)) {
		switchStepMode(stepState);
	}

	unsigned long currentTime = millis();

	// If menu is active, but no input received for a while, close it
	if (runtime.menu && currentTime > runtime.menuTimer) {
		buttonPress(8, 0);
	}


	// Update the display, if required
	if (runtime.update && (runtime.menu || runtime.menuTimer < currentTime)) {
		runtime.update = false;

		// DISPLAY MENU
		// --------------------------------------------------
		if (runtime.menu) {

			char* entry1;
			char* entry2;
			char* entry3;

			// Figure out which three menu items to show
			uint8_t count = 0;
			for (uint8_t i = 0; i < MENU_ITEMS; i++) {
				if (menu[i].current == runtime.head && menu[i].enabled) {
					if (count == runtime.item - 2) entry1 = menu[i].name;
					else if (count == runtime.item - 1) entry2 = menu[i].name;
					else if (count == runtime.item) entry3 = menu[i].name;
					else if (count == runtime.item + 1) {
						entry1 = entry2;
						entry2 = entry3;
						entry3 = menu[i].name;
					} else if (runtime.item == 0 && count == runtime.item + 2) {
						entry1 = entry2;
						entry2 = entry3;
						entry3 = menu[i].name;
					}
					count++;
				}
			} 

			if (count == 2) {
				entry1 = entry2;
				entry2 = entry3;
				entry3 = NULL;
			} else if (count == 1) {
				entry1 = entry3;
				entry2 = NULL;
				entry3 = NULL;
			}

			// Draw the menu
			display.firstPage();
			do {
				display.setFont(u8g2_font_7x14_mr);
				display.setDrawColor(1);
				display.setCursor(3, 14);
				if (runtime.head == 0) display.print(F("Select A/B"));
				else if (runtime.head == 1 && !runtime.seqSel) display.print(F("Sequencer A"));
				else if (runtime.head == 1) display.print(F("Sequencer B"));
				else if (runtime.head == 2) display.print(F("Custom Input"));

				// Display the up/down arrows
				if (runtime.item - 1 > 0 && count > 3) {
					display.drawTriangle(120, 26, 123, 20, 126, 26);
				}

				if (runtime.item < count - 2 && count > 3) {
					display.drawTriangle(120, 54, 123, 60, 126, 54);
				}

				// If the current menu item is in the: 
				// First row of the screen
				if (runtime.item == 0) {
					display.drawStr(3, 45, entry2);
					display.drawStr(3, 61, entry3);
					if (runtime.selected) {
						display.drawBox(0, 16, 128, 16);
						display.setDrawColor(0);
					} else {
						display.drawFrame(0, 16, 128, 16);
					}
					display.drawStr(3, 29, entry1);

				// Third row of the screen
				} else if (runtime.item == count-1 && runtime.item > 1) {
					display.drawStr(3, 29, entry1);
					display.drawStr(3, 45, entry2);
					if (runtime.selected) {
						display.drawBox(0, 48, 128, 16);
						display.setDrawColor(0);
					} else {
						display.drawFrame(0, 48, 128, 16);
					}
					display.drawStr(3, 61, entry3);

				// Second row of the screen
				} else {
					display.drawStr(3, 29, entry1);
					display.drawStr(3, 61, entry3);
					if (runtime.selected) {
						display.drawBox(0, 32, 128, 16);
						display.setDrawColor(0);
					} else {
						display.drawFrame(0, 32, 128, 16);
					}
					display.drawStr(3, 45, entry2);
				}

			} while (display.nextPage());


		// DISPLAY STATUS SCREEN
		// --------------------------------------------------
		} else if (runtime.mode == 1) {

			// Draw the status screen
			display.firstPage();
			do {
				display.setFont(u8g2_font_7x14_mr);
				display.setDrawColor(1);

				// Draw details of Sequencer A
				display.setCursor(9, 14);
				display.print(F("[A]"));

				// Playing icon
				if (seq[0].active && seq[0].current >= 0) {
					display.drawTriangle(37, 4, 46, 9, 37, 14);
				// Paused icon
				} else if (!seq[0].active && seq[0].current >= 0) {
					display.drawBox(37, 4, 4, 10);
					display.drawBox(43, 4, 4, 10);
				// Stopped icon
				} else if (seq[0].current < 0) {
					display.drawBox(37, 4, 9, 10);
				}

				char entry1[8];

				if (seq[0].overide == 0) {
					// Current Tempo
					sprintf(entry1, "%3u bpm", seq[0].tempo);
					display.drawStr(3, 32, entry1);

					// Current Step
					uint8_t timeline = 55;
					if (!runtime.bothSeq) timeline = 125;

					sprintf(entry1, "%2u / %u", seq[0].current + 1, seq[0].total);
					display.drawStr(3, 50, entry1);
					uint16_t counter = 0;
					for (uint8_t j = 0; j < seq[0].total; j++) {
						display.drawBox(int(counter / float(LEN_MAX) * timeline) + 1, 61, 1, 3);
						display.drawBox(int(counter / float(LEN_MAX) * timeline) + 1, 63, int((sequence[j].length / float(LEN_MAX)) * timeline * (sequence[j].gate / float(GATE_MAX - 1))), 1);
						if (seq[0].current == j) display.drawBox(int(counter / float(LEN_MAX) * timeline), 59, 3, 5);
						counter = counter + sequence[j].length;
					}

				// If a note is being played manually
				} else {
					// Read voltage of output
					if (!runtime.seqSel) {
						int value = int(analogRead(VOLT_A) * 4.89);
						voltRead = (3 * voltRead + value) / 4;

						sprintf(entry1, "%1.1u.%.3u V", voltRead / 1000, voltRead % 1000);
						display.drawStr(3, 50, entry1);

						uint8_t closestNote = round(voltRead * 0.012);
						display.drawBox(0, 63, 3, 1);
						display.drawBox(28, 62, 3, 2);
						display.drawBox(56, 63, 3, 1);
						display.drawBox(28 + int(((voltRead * 0.012) - closestNote) * 54), 59, 1, 5);

						// Current Note
						sprintf(entry1, "[%u] N:%u", seq[0].overide, closestNote);
					} else sprintf(entry1, "[%u] --", seq[0].overide);

					display.drawStr(3, 32, entry1);

					runtime.update = true;
					runtime.menuTimer = currentTime + DISPLAY_REFRESH;
				}

				// Draw details of Sequencer B
				if (runtime.bothSeq) {
					display.drawBox(63, 16, 1, 48);

					display.setCursor(78, 14);
					display.print(F("[B]"));

					// Playing icon
					if (seq[1].active && seq[1].current >= 0) {
						display.drawTriangle(106, 4, 115, 9, 106, 14);
					// Paused icon
					} else if (!seq[1].active && seq[1].current >= 0) {
						display.drawBox(106, 4, 4, 10);
						display.drawBox(112, 4, 4, 10);
					// Stopped icon
					} else if (seq[1].current < 0) {
						display.drawBox(106, 4, 9, 10);
					}

					if (seq[1].overide == 0) {
					// Current Tempo
					sprintf(entry1, "%3u bpm", seq[1].tempo);
					display.drawStr(72, 32, entry1);

					// Current Step
					sprintf(entry1, "%2u / %u", seq[1].current + 1, seq[1].total);
					display.drawStr(72, 50, entry1);
					uint16_t counter = 0;
					for (uint8_t j = 0; j < seq[1].total; j++) {
						display.drawBox(int(counter / float(LEN_MAX) * 55) + 70, 61, 1, 3);
						display.drawBox(int(counter / float(LEN_MAX) * 55) + 70, 63, int((sequence[j + (SEQ_MAX / 2)].length / float(LEN_MAX)) * 55 * (sequence[j + (SEQ_MAX / 2)].gate / float(GATE_MAX - 1))), 1);
						if (seq[1].current == j) display.drawBox(int(counter / float(LEN_MAX) * 55) + 69, 59, 3, 5);
						counter = counter + sequence[j + (SEQ_MAX / 2)].length;
					}
					// If a note is being played manually
					} else {
						// Read voltage of output
						if (runtime.seqSel) {
							int value = int(analogRead(VOLT_B) * 4.89);
							voltRead = (3 * voltRead + value) / 4;

							sprintf(entry1, "%1.1u.%.3u V", voltRead / 1000, voltRead % 1000);
							display.drawStr(72, 50, entry1);

							uint8_t closestNote = round(voltRead * 0.012);
							display.drawBox(69, 63, 3, 1);
							display.drawBox(97, 62, 3, 2);
							display.drawBox(125, 63, 3, 1);
							display.drawBox(97 + int(((voltRead * 0.012) - closestNote) * 54), 59, 1, 5);

							// Current Note
							sprintf(entry1, "[%u] N:%u", seq[1].overide, closestNote);
						} else sprintf(entry1, "[%u] --", seq[1].overide);

						display.drawStr(72, 32, entry1);

						runtime.update = true;
						runtime.menuTimer = currentTime + DISPLAY_REFRESH;
					}
				}

			} while (display.nextPage());
		

		// DISPLAY CUSTOM SEQUENCE RECORDING SCREEN
		// --------------------------------------------------
		} else if (runtime.mode == 2) {

			// Draw the status screen
			display.firstPage();
			do {
				display.setFont(u8g2_font_7x14_mr);
				display.setDrawColor(1);
				
				// Draw details of Sequencer A
				display.setCursor(0, 14);
				display.print(F("Play new Sequence"));

				char entry1[16];

				// Current Step
				if (runtime.bothSeq) sprintf(entry1, "Step: %2u (Max 16)", seq[runtime.seqSel].total);
				else sprintf(entry1, "Step: %2u (Max 32)", seq[runtime.seqSel].total);
				display.drawFrame(0, 20, 128, 22);
				display.drawStr(4, 36, entry1);

				display.setCursor(0, 63);
				display.print(F("Press MENU to exit"));

			} while (display.nextPage());
		
		// DISPLAY CUSTOM RHYTHM/BOTH RECORDING SCREEN
		// --------------------------------------------------
		} else if (runtime.mode == 3 || runtime.mode == 4) {

			// Draw the status screen
			display.firstPage();
			do {
				display.setFont(u8g2_font_7x14_mr);
				display.setDrawColor(1);
				
				// Draw details of Sequencer A
				display.setCursor(0, 14);
				if (runtime.mode == 3) display.print(F("Play new Rhythm"));
				else display.print(F("Play Both!"));

				char entry1[16];

				if (seq[runtime.seqSel].current == -1) {
					display.setCursor(0, 36);
					display.print(F("Press any BUTTON"));
					display.setCursor(0, 63);
					display.print(F("MENU = Rest"));
				} else {
					if (runtime.mode == 3) sprintf(entry1, "Step: %2u / %2u", seq[runtime.seqSel].current + 1, seq[runtime.seqSel].total);
					else if (runtime.bothSeq) sprintf(entry1, "Step: %2u (Max 16)", seq[runtime.seqSel].total);
					else sprintf(entry1, "Step: %2u (Max 32)", seq[runtime.seqSel].total);
					display.drawFrame(0, 20, 128, 22);
					display.drawStr(4, 36, entry1);

					// Display amount of time left
					for (uint8_t j = 0; j < seq[0].beats; j++) {
						display.drawBox((j * 125 / (seq[0].beats - 1)) + 1, 62, 1, 2);
					}
					display.drawBox(int((currentTime - startTime) * 125 / totalTime), 59, 3, 5);

					runtime.update = true;
					runtime.menuTimer = currentTime + DISPLAY_REFRESH;
				}

			} while (display.nextPage());

		}
	}
}



/******************************************************
 * TIMER INTERUPT
 ******************************************************/
void updateStepTimer() {
	bool updateSteps = false;
	bool newClockStep = false;

	// If we are in a rhythm recording mode, check the button state
	if (runtime.mode == 3 || runtime.mode == 4) checkButtons();

	// If we are in the rhythm recording mode, check if the end of the sequence has been reached
	if ((runtime.mode == 3 || runtime.mode == 4) && startTime > 0 && startTime + totalTime < millis()) finishRecording();

	// If we need the clock, check its state
	if (seq[0].clockin || seq[1].clockin) {
		if (digitalRead(CLOCK_IN) != clockState) {
			newClockStep = clockState;
			clockState = !clockState;
		}
	}

	// If sequencer A is active
	if (seq[0].active && seq[0].current >= 0 && seq[0].overide == 0) {

		// Update note
		if ((seq[0].timerNote > 0 && seq[0].timerNote < millis()) || (seq[0].clockin && newClockStep)) {

			// Update current step
			updateSteps = true;
			seq[0].current++;
			if (seq[0].current >= seq[0].total) seq[0].current = 0;
	
			// Update note timing
			calculateNoteTiming(0, seq[0].timerNote);
			
		// Turn off gate
		} else if (seq[0].timerGate > 0 && seq[0].timerGate < millis()) {
			digitalWrite(GATE_A, LOW);
			seq[0].timerGate = 0;
		}

		// Turn off trigger
		if (!runtime.bothSeq && seq[1].timerGate > 0 && seq[1].timerGate < millis()) {
			digitalWrite(GATE_B, LOW);
			seq[1].timerGate = 0;
		}
	}

	// If sequencer B is active
	if (runtime.bothSeq && seq[1].active && seq[1].current >= 0 && seq[1].overide == 0) {

		// Update note
		if ((seq[1].timerNote > 0 && seq[1].timerNote < millis()) || (seq[1].clockin && newClockStep)) {

			// Update current step
			updateSteps = true;
			seq[1].current++;
			if (seq[1].current >= seq[1].total) seq[1].current = 0;
			
			// Update note timing
			calculateNoteTiming(1, seq[1].timerNote);
			
		// Turn off gate
		} else if (seq[1].timerGate > 0 && seq[1].timerGate < millis()) {
			digitalWrite(GATE_B, LOW);
			seq[1].timerGate = 0;
		}
	}

	if (updateSteps) playCurrentNotes();
}