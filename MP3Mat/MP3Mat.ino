
// ******************************* INFORMATION ***************************//

// ***********************************************************************//
// "MP3Mat" sketch for Lilypad MP3 Player
// Stefanie Noell
// me@stefanienoell.com

// April 2020
// Version 1.1
// - Bug fix: corrected filenumber after reading files in directory
// - removed constant ERROR_WINDOW and hard coded voltage divider values for directory input
// - increased delay between voltage divider readings
// - removed settings of initial volume as it gets overwritten straight away 
// - Bug fix: change initial value of target_vol_range to a value out of used range but within limit of the byte datatype


// Tries to find five directories, whose names start with "1" - "5".
// If a directory is chosen via UI, it reads files within it and starts playing MP3s until the last one is finished.
// If no directory is chosen it stops playing.
// Goes into sleep mode after set period of time where no input has been made and no MP3 has been played.
// Wakes up when change at any input is detected.

// Uses four inputs:
// 1) Analog - Voltage divider circuit (range 0-5) to choose directory from which to play Mp3 files("0" = no directory chosen)
// 2) Analog - Voltage divider circuit (range 0-5) to adjust volume
// 3) Digital - Switch to play previous track
// 4) Digital - Switch to play next track


// Code dealing with the voltage divider reading was adapted from
// http://forum.arduino.cc/index.php?topic=20125.0
// as published by Doug LaRue in November 2008 under Creative Commons Attribution-Noncommercial-Share Alike 3.0


// Code dealing with sleep mode was adapted from 
// http://playground.arduino.cc/Learning/ArduinoSleepCode
// Copyright (C) 2006 MacSimski 2006-12-30 
// Copyright (C) 2007 D. Cuartielles 2007-07-08 - Mexico DF


// ***********************************************************************//


// ******************************* TODOS *********************************//

// ***********************************************************************//

// - separate directory and volume voltage readings to use different number ranges

// ***********************************************************************//



// ******************************* LIBRARIES *****************************//

// ***********************************************************************//
#include <avr/sleep.h>
#include <avr/interrupt.h>

// Uses the SdFat library by William Greiman, which is supplied
// with this archive, or download from http://code.google.com/p/sdfatlib/

// Uses the SFEMP3Shield library by Bill Porter, which is supplied
// with this archive, or download from http://www.billporter.info/

#include <SPI.h>            // To talk to the SD card and MP3 chip
#include <SdFat.h>          // SD card file system
#include <SFEMP3Shield.h>   // MP3 decoder chip


// Create library objects:
SFEMP3Shield MP3player;
SdFat sd;

// ***********************************************************************//



// ******************************* CONSTANTS *****************************//

// ***********************************************************************//

// Set DEBUG = true if you'd like status messages sent
// to the serial port. Note that this will take over trigger
// inputs 4 and 5. (You can leave triggers connected to 4 and 5
// and still use the serial port, as long as you're careful to
// NOT ground the triggers while you're using the serial port).

const boolean DEBUG = false;

// INPUTS
const byte SET_DIRECTORY_PIN = A5;   // voltage divider circuit input connected to ANALOG pin 5 (T3 on Lilypad MP3 with SJ1 cut/disconnected!)
const byte SET_VOLUME_PIN = A0;   // voltage divider circuit input connected to ANALOG pin 0 (T1 on Lilypad MP3)
const byte NEXT_TRACK_SWITCH_PIN = A4; // switch circuit input connected to ANALOG pin 4 (T2 on Lilypad MP3)
const byte PREVIOUS_TRACK_SWITCH_PIN = 1; // switch circuit input connected to DIGITAL pin 1 (T4 on Lilypad MP3)

// OUTPUTS
const byte SD_CS = 9;     // Chip Select for SD card
const int EN_GPIO1 = A2;  // Amp enable + MIDI/MP3 mode select

// Delay between voltage divider readings
const int BUTTONDELAY = 1000;

// Max number of files to be read per directory (more files = more memory needed!)
const byte MAX_NUM_FILES = 20;

// Delay after which sleep mode is turned on if there is no interaction and no mp3 playing
const int SLEEP_DELAY = 20000; // in ms

// ***********************************************************************//



// ******************************* VARIABLES *****************************//

// ***********************************************************************//

SdFile file;
byte result;

// We'll store the five directories and up to MAX_NUM_FILES filenames as arrays of characters.
// "Short" (8.3) filenames are used, followed by a null character.
char dirname[5][13];
char filename[MAX_NUM_FILES][13] = {};

// store the index of the last chosen directory as well as index of playing file
byte last_dir = 0;   // previous chosen directory/trigger (0 = none)
byte last_file = 0;  // previous (playing) file (0 = none)

byte volume = 50; // actual volume value
byte target_vol = 50; // volume we want to fade to
byte target_vol_range = 255;  // previous volume trigger ( range of 0 to 5);

boolean nextTrackSwitchOn = false;
boolean previousTrackSwitchOn = false;

long buttonLastChecked = 0; // variable to limit the voltage divider triggers getting checked every cycle

long sleepCounter = 0;  // counts up to SLEEP_DELAY and then turn on sleep mode

// ***********************************************************************//



// ******************************* SETUP *********************************//

// ***********************************************************************//
void setup()
{

  // =======================================================================//
  // ! Initialize the serial port                                           //
  // =======================================================================//
  // (The 'F' stores constant strings in flash memory to save RAM)
  if (DEBUG)
  {
    Serial.begin(9600);
    Serial.println(F("Lilypad MP3 Player trigger sketch"));
  }

  // =======================================================================//
  // ! Initialize switch buttons as inputs                                  //
  // =======================================================================//
  pinMode(NEXT_TRACK_SWITCH_PIN, INPUT);
  pinMode(PREVIOUS_TRACK_SWITCH_PIN, INPUT);
  // and turn on internal pullup resistors (IS THIS NEEDED?? WHAT DOES IT DO???)
  digitalWrite(NEXT_TRACK_SWITCH_PIN, HIGH);
  digitalWrite(PREVIOUS_TRACK_SWITCH_PIN, HIGH);

  // =======================================================================//
  // ! Initialize amplifier chip                                           //
  // ======================================================================//
  // The board uses a single I/O pin to select the
  // mode the MP3 chip will start up in (MP3 or MIDI),
  // and to enable/disable the amplifier chip:
  
  pinMode(EN_GPIO1,OUTPUT);
  digitalWrite(EN_GPIO1,LOW);  // MP3 mode / amp off


  // =======================================================================//
  // ! Initialize the SD card                                               //
  // =======================================================================//
  // SS = pin 9, half speed at first
  if (DEBUG) Serial.print(F("initialize SD card... "));

  result = sd.begin(SD_CS, SPI_HALF_SPEED); // 1 for success

  if (result != 1) // Problem initializing the SD card
  {
    if (DEBUG) Serial.println(F("error, halting"));
  }
  else if (DEBUG) Serial.println(F("success!"));

  // =======================================================================//
  // ! Start up the MP3 library                                             //
  // =======================================================================//
  if (DEBUG) Serial.print(F("initialize MP3 chip... "));

  result = MP3player.begin(); // 0 or 6 for success

  // Check the result, see the library readme for error codes.
  if ((result != 0) && (result != 6)) // Problem starting up
  {
    if (DEBUG)
    {
      Serial.print(F("error code "));
      Serial.print(result);
      Serial.println(F(", halting."));
    }

  }
  else if (DEBUG) Serial.println(F("success!"));

  // =======================================================================//
  // ! Read directories                                                     //
  // =======================================================================//
  readRootDirnames();


  // =======================================================================//
  // ! Turn on the amplifier chip                                           //
  // =======================================================================//
  digitalWrite(EN_GPIO1,HIGH);

  delay(2);
} // /setup()
// ***********************************************************************//



// ******************************* LOOP **********************************//

// ***********************************************************************//
void loop()
{

  volatile boolean detectedInteraction = false;

  if ( buttonLastChecked == 0 ) // see if this is the first time checking the buttons
    buttonLastChecked = millis() + BUTTONDELAY; // force a check this cycle

  // =======================================================================//
  // ! Check voltage divider inputs                                         //
  // =======================================================================//
  if ( millis() - buttonLastChecked > BUTTONDELAY ) { // make sure a reasonable delay passed
    int buttNum;

    // ========================== DIRECTORY ================================//
    // check if the directory switch is triggered
    if ( buttNum = buttonPushed(SET_DIRECTORY_PIN)) {

      // try to start playing from new directory if trigger is new
      if (buttNum != last_dir) {
        if (DEBUG) {
          Serial.print(F("DIRECTORY ")); Serial.print(buttNum); Serial.println(F(" was triggered."));
        }
        last_dir = buttNum;
        detectedInteraction = true;

        // Do we have a dirname for this trigger?
        if (!dirname[buttNum - 1])
        {
          if (DEBUG)
            Serial.println(F("no directory for that trigger"));
        }
        else // We do have a directory for this trigger!
        {

          if (DEBUG)
          {
            Serial.print(F("got new trigger with directory "));
            Serial.println(buttNum);
          }

          // If a file is already playing, stop the playback before playing the new file.
          if (MP3player.isPlaying())
          {
            stopPlayback();
          }

          // read filenames of triggered directory ()
          readFilenames(buttNum);

        }
      }
      else {
        // not a new button was trigger, but trigger is still on

        // Play next track if music is not playing
        if (!MP3player.isPlaying())
        {
          playNextTrack();
        }

      }

    } else {
      // no directory triggered at all
      if (DEBUG) Serial.println(F("No DIRECTORY triggered at all."));
      if (MP3player.isPlaying())
      {
        stopPlayback();
        detectedInteraction = true;
      }
      last_dir = 0;
      last_file = 0;
    }

    // ========================== VOLUME ================================//
    // check if the volume voltage divider has changed

    if ( buttNum = buttonPushed(SET_VOLUME_PIN)) {

      // change volume if trigger is new
      if (buttNum != target_vol_range) {

        if (DEBUG) {
          Serial.println(F("// ***************** VOLUME *******************//"));
          Serial.print(F("Volume Range ")); Serial.print(buttNum); Serial.println(F(" was triggered."));
        }

        target_vol_range = buttNum;
        setTargetVolumeByRange(buttNum);

        detectedInteraction = true;

      }

    }

    buttonLastChecked = millis(); // reset the lastChecked value
  }

  // =======================================================================//
  // ! Check switch inputs                                                  //
  // =======================================================================//
  int previousTrackSwitchState = digitalRead(PREVIOUS_TRACK_SWITCH_PIN);
  int nextTrackSwitchState = digitalRead(NEXT_TRACK_SWITCH_PIN);

  // play new track if PREVIOUS switch has been triggered
  // only do this if debug == false as trigger is used by serial monitoring as well otherwise!
  if (!DEBUG  && last_dir > 0 && previousTrackSwitchState == LOW && !previousTrackSwitchOn) {
    if (DEBUG) Serial.println(F("// ***************** play PREVIOUS track triggered *******************//"));
    if (MP3player.currentPosition() >= 3000) { // has played more than 3 seconds of track
      // go to beginning of track
      //MP3player.skipTo(0); // !PROBLEM: skipping doesn't reset position to 0!
      // so basically, play same song again
      stopPlayback();
      playMp3File(last_file);
    } else {
      playPreviousTrack();
    }
    previousTrackSwitchOn = true;
  } else {
    previousTrackSwitchOn = false;
  }

  // play new track if NEXT switch has been triggered
  if (last_dir > 0 && nextTrackSwitchState == LOW && !nextTrackSwitchOn) {

    if (DEBUG) Serial.println(F("// ***************** play NEXT track triggered *******************//"));
    playNextTrack();
    nextTrackSwitchOn = true;
  } else {
    nextTrackSwitchOn = false;
  }

  if (nextTrackSwitchState == LOW) {
    detectedInteraction = true;
  }

  // =======================================================================//
  // ! Handle sleep mode                                                    //
  // =======================================================================//
  if (!detectedInteraction && !MP3player.isPlaying()){;
    
    if (millis() - sleepCounter >= SLEEP_DELAY){
      //Serial.print(F(" ++++++++ Sleep Counter ++ :")); Serial.print(sleepCounter);
      //Serial.print(F(" ++++++++ Sleep DELAY ++ :")); Serial.println(SLEEP_DELAY);
      if (DEBUG) Serial.println(F("// ******** GO TO SLEEP NOW !!!! *******//"));
     
      sleepCounter = millis();
      sleepNow();
    }
    
  }else{
    sleepCounter = millis();
  }

  
  // =======================================================================//
  // ! Adjust volume                                                        //
  // =======================================================================//
  adjustVolume();

}// loop()
// ***********************************************************************//



// =======================================================================//
// ! MP3 Helper Functions                                                 //
// =======================================================================//
void stopPlayback() {

  if (DEBUG) Serial.println(F("stopping playback"));

  MP3player.stopTrack();

}

void playNextTrack() {

  if ( (last_file + 1) <= MAX_NUM_FILES && filename[last_file] != "") {
    MP3player.stopTrack();
    last_file++;
    playMp3File(last_file);
  }

}

void playPreviousTrack() {

  if ( (last_file - 1) >= 0 && filename[last_file - 2] != "") {
    MP3player.stopTrack();
    last_file--;
    playMp3File(last_file);
  }

}

void playMp3File(byte fileNum) {

  // Play the filename associated with the index
  // (If a file is already playing, this command will fail
  //  with error #2).

  result = MP3player.playMP3(filename[fileNum - 1]);

  if (result == 0) last_file = fileNum;  // Save playing trigger

  if (DEBUG)
  {
    if (result != 0)
    {
      Serial.print(F("error "));
      Serial.print(result);
      Serial.print(F(" when trying to play track "));
    }
    else
    {
      Serial.print(F("playing "));
    }
    Serial.println(filename[fileNum - 1]);
  }
}


void setTargetVolumeByRange(byte val) {
  target_vol = map(val, 0, 5, 0, 60);
  target_vol = constrain(target_vol, 0, 60);

  if (DEBUG) {
    Serial.print(F("Setting target volume to: "));
    Serial.println(target_vol);
  }

}

void adjustVolume() {

  if (volume < target_vol) {
    volume ++;
    //    if (DEBUG) {
    //      Serial.print(F("Volume = "));
    //      Serial.println(volume);
    //    }
  } else if (volume > target_vol) {
    volume --;
    //    if (DEBUG) {
    //      Serial.print(F("Volume = "));
    //      Serial.println(volume);
    //    }
  }

  MP3player.setVolume(volume, volume);

}

// =======================================================================//
// ! Filesystem Helper Functions                                          //
// =======================================================================//
void readRootDirnames() {

  byte index;
  char tempdirname[13];

  // Now we'll access the SD card to look for directories
  if (DEBUG) Serial.println(F("reading root directory"));

  // Start at the first file in root and step through all of them:
  sd.chdir("/", true);

  while (file.openNext(sd.vwd(), O_READ))
  {
    // get dirname
    file.getFilename(tempdirname);

    // Does the filename start with char '1' through '5' and is a directory?
    if (tempdirname[0] >= '1' && tempdirname[0] <= '5' && file.isDir())
    {
      // Yes! subtract char '1' to get an index of 0 through 4.
      index = tempdirname[0] - '1';

      // Copy the data to our dirname array.
      strcpy(dirname[index], tempdirname);

      if (DEBUG) // Print out file number and name
      {
        Serial.print(F("found an entry with a leading "));
        Serial.print(index + 1);
        Serial.print(F(": "));
        Serial.println(dirname[index]);
      }
    }
    else if (DEBUG)
    {
      Serial.print(F("found an entry w/o a leading number: "));
      Serial.println(tempdirname);
    }

    file.close();
  }


  if (DEBUG)
    Serial.println(F("done reading root directory"));

  if (DEBUG) // List all the files we saved:
  {
    for (byte x = 0; x <= 4; x++)
    {
      Serial.print(F("trigger "));
      Serial.print(x + 1);
      Serial.print(F(": "));
      Serial.println(dirname[x]);
    }
  }

}

void readFilenames(byte dirNum) {
  stopPlayback();

  char tempfilename[13];
  byte index = 0;

  // delete previously stored filenames 
  memset(filename,0,sizeof(filename));

  // Now we'll access the SD card to look for directories
  if (DEBUG) {
    Serial.print(F("reading directory: "));
    Serial.println(dirname[dirNum - 1]);
  }

  // Start at the first file in directory and step through all of them:
  sd.chdir("/", true);
  if (!sd.chdir(dirname[dirNum - 1], true)) {
    if (DEBUG) Serial.println(F("chdir failed for directory "));
  }
  else {
    // directory exists, so read files
    file.close();
    file.rewind();
    while (file.openNext(sd.vwd(), O_READ))
    {

      // get filename
      file.getFilename(tempfilename);

      // Copy the data to our filename array is name doesn't start with "_"
      if (tempfilename[0] != 95) {
        strcpy(filename[index], tempfilename);
        if (DEBUG) // Print out file number and name
        {
          Serial.print(F("found a file: "));
          Serial.println(filename[index]);
        }
        index++;
      }

      file.close();
    }

  }

  if (DEBUG) {
    Serial.print(F("done reading directory "));
    Serial.println(dirname[dirNum - 1]);

    // List all the files we saved:
    for (byte x = 0; x < MAX_NUM_FILES; x++)
    {
      Serial.print(F("file "));
      Serial.print(x);
      Serial.print(F(": "));
      Serial.println(filename[x]);
    }
  }

  // start playing first file
  last_file = 0;
  playMp3File(1);
}

// =======================================================================//
// ! Read Voltage Divider input                                           //
// =======================================================================//

int buttonPushed(byte pinNum) { //  Read voltage divider
  int val = 0;         // variable to store the read value
  pinMode(pinNum, INPUT);
  digitalWrite((pinNum), HIGH); // enable the 20k internal pullup
  val = analogRead(pinNum);   // read the input pin

  if (DEBUG) {
    Serial.print(F("pinNum: "));
    Serial.print(pinNum);
    Serial.print(F(", val: "));
    Serial.println(val);
  }

  // we don't use the upper position because that is the same as the
  // all-open switch value when the internal 20K ohm pullup is enabled.
  //  if( val >= 923 and val <= 1023 ){
  //    if (DEBUG) {
  //      Serial.println(F("switch 0 pressed/triggered"));
  //    }
  //    return 0;
  //  } else

    if ( val >= (751) and val <= (980) ) { // measured: 763 - 
    if (DEBUG) {
      Serial.print(pinNum);
      Serial.println(F(": switch 1 pressed/triggered"));
    }
    return 1;
  }
  else if ( val >= (580) and val <= (750) ) { // 612-742
    if (DEBUG) {
      Serial.print(pinNum);
      Serial.println(F(": switch 2 pressed/triggered"));
    }
    return 2;
  }
  else if ( val >= (380) and val <= (560) ) { // - 560
    if (DEBUG) {
      Serial.print(pinNum);
      Serial.println(F(": switch 3 pressed/triggered"));
    }
    return 3;
  }
  else if ( val >= (241) and val <= (360) ) { // measured: 232-354
    if (DEBUG) {
      Serial.print(pinNum);
      Serial.println(F(": switch 4 pressed/triggered"));
    }
    return 4;
  }
  else if ( val >= 0 and val <= (240) )  { // measured: 0 - 240
    if (DEBUG) {
      Serial.print(pinNum);
      Serial.println(F(": switch 5 pressed/triggered"));
    }
    return 5;
  }
  else
    return 0;  // no button found to have been pushed
}

// =======================================================================//
// ! Sleep Mode and Interrupt Helper Functions                            //
// =======================================================================//
void myAttachInterrupt() {
  cli();    // switch interrupts off while messing with their settings
  //PCIFR  |= 0x02; // clear any outstanding interrupt
  PCIFR  |= 0b00000110; // clear any outstanding interrupt
  //PCICR = 0x02;         // Enable PCINT1 interrupt
  PCICR |= 0b00000110;   // Enable port C & D (PCINT1_vect & PCINT2_vect) interrupts
  PCMSK1 |= 0b00110001;  // turn on pins A0, A4 & A5
  PCMSK2 |= 0b00000011;    // turn on pins D0 & D1
  sei();
}

void myDetachInterrupt() {
  cli();    // switch interrupts off while messing with their settings
  PCIFR  |= 0; // clear any outstanding interrupt
  PCICR = 0;         // Disable PCINT1 interrupt
  PCMSK1 = 0;
  PCMSK2 = 0;
  sei();
}

ISR(PCINT1_vect) {    // Interrupt service routine. Every single PCINT8..14 (=ADC0..5) change
  // will generate an interrupt: but this will always be the same interrupt routine

 // Nothing to be done here. We only wanted to wake up from sleep.
}

ISR(PCINT2_vect){}    // Port D, PCINT16 - PCINT23 (D0-...)

void sleepNow()         // here we put the arduino to sleep
{
   
  if(DEBUG){
    Serial.println("Sleep Now!");
  }
  /* Now is the time to set the sleep mode. In the Atmega8 datasheet
   * http://www.atmel.com/dyn/resources/prod_documents/doc2486.pdf on page 35
   * there is a list of sleep modes which explains which clocks and
   * wake up sources are available in which sleep mode.
   *
   * In the avr/sleep.h file, the call names of these sleep modes are to be found:
   *
   * The 5 different modes are:
   *     SLEEP_MODE_IDLE         -the least power savings
   *     SLEEP_MODE_ADC
   *     SLEEP_MODE_PWR_SAVE
   *     SLEEP_MODE_STANDBY
   *     SLEEP_MODE_PWR_DOWN     -the most power savings
   *
   * For now, we want as much power savings as possible, so we
   * choose the according
   * sleep mode: SLEEP_MODE_PWR_DOWN
   *
   */
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);   // sleep mode is set here

  sleep_enable();          // enables the sleep bit in the mcucr register
  // so sleep is possible. just a safety pin

  /* Now it is time to enable an interrupt. We do it here so an
   * accidentally pushed interrupt button doesn't interrupt
   * our running program.
   */
  myAttachInterrupt();

  sleep_mode();            // here the device is actually put to sleep!!
  // THE PROGRAM CONTINUES FROM HERE AFTER WAKING UP

  sleep_disable();         // first thing after waking from sleep:
  // disable sleep...
  myDetachInterrupt();      // disables interrupt

  if(DEBUG){
    Serial.println("Woken up!");
  }
}
