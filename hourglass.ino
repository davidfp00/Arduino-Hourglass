#include <TM1637.h>

#include "Arduino.h"
#include "LedControl.h"
#include "Delay.h"

#define  MATRIX_A  0
#define MATRIX_B  1

// Values are 260/330/400
#define ACC_THRESHOLD_LOW 335
#define ACC_THRESHOLD_HIGH 400

// Matrix
#define PIN_DATAIN 5
#define PIN_CLK 4
#define PIN_LOAD 6

// Accelerometer
#define PIN_X A1
#define PIN_Y A2

// Pins Buttons
#define PIN_BUTTON 3
#define PIN_BUTTON2 7

#define PIN_BUZZER 14

// This takes into account how the matrixes are mounted
#define ROTATION_OFFSET 90

// in milliseconds
#define DEBOUNCE_THRESHOLD 200

#define DELAY_FRAME 100

#define MODE_HOURGLASS 0
#define MODE_SETMINUTES 1
#define MODE_SETSECONDS 2

float delayMinutes = 0;
float delaySeconds = 40;
float duracion = 30;
float delayMilisecs = (duracion/60)*1000;
int mode = MODE_HOURGLASS;
int gravity;
LedControl lc = LedControl(PIN_DATAIN, PIN_CLK, PIN_LOAD, 2);
NonBlockDelay d;
int resetCounter = 0;
bool alarmWentOff = false;
int CLK = 10;
int DIO = 11;
unsigned long pulsacion;
TM1637 tm(CLK,DIO);


/**
   Get delay between particle drops (in seconds)
*/
float getDelayDrop() {
  // since we have exactly 60 particles we don't have to multiply by 60 and then divide by the number of particles again :)
  return delayMinutes + delaySeconds/60;
}

coord getDown(int x, int y) {
  coord xy;
  xy.x = x - 1;
  xy.y = y + 1;
  return xy;
}
coord getLeft(int x, int y) {
  coord xy;
  xy.x = x - 1;
  xy.y = y;
  return xy;
}
coord getRight(int x, int y) {
  coord xy;
  xy.x = x;
  xy.y = y + 1;
  return xy;
}



bool canGoLeft(int addr, int x, int y) {
  if (x == 0) return false; // not available
  return !lc.getXY(addr, getLeft(x, y)); // you can go there if this is empty
}
bool canGoRight(int addr, int x, int y) {
  if (y == 7) return false; // not available
  return !lc.getXY(addr, getRight(x, y)); // you can go there if this is empty
}
bool canGoDown(int addr, int x, int y) {
  if (y == 7) return false; // not available
  if (x == 0) return false; // not available
  if (!canGoLeft(addr, x, y)) return false;
  if (!canGoRight(addr, x, y)) return false;
  return !lc.getXY(addr, getDown(x, y)); // you can go there if this is empty
}



void goDown(int addr, int x, int y) {
  lc.setXY(addr, x, y, false);
  lc.setXY(addr, getDown(x, y), true);
}
void goLeft(int addr, int x, int y) {
  lc.setXY(addr, x, y, false);
  lc.setXY(addr, getLeft(x, y), true);
}
void goRight(int addr, int x, int y) {
  lc.setXY(addr, x, y, false);
  lc.setXY(addr, getRight(x, y), true);
}


int countParticles(int addr) {
  int c = 0;
  for (byte y = 0; y < 8; y++) {
    for (byte x = 0; x < 8; x++) {
      if (lc.getXY(addr, x, y)) {
        c++;
      }
    }
  }
  return c;
}


bool moveParticle(int addr, int x, int y) {
  if (!lc.getXY(addr, x, y)) {
    return false;
  }

  bool can_GoLeft = canGoLeft(addr, x, y);
  bool can_GoRight = canGoRight(addr, x, y);

  if (!can_GoLeft && !can_GoRight) {
    return false; // we're stuck
  }

  bool can_GoDown = canGoDown(addr, x, y);

  if (can_GoDown) {
    goDown(addr, x, y);
  } else if (can_GoLeft && !can_GoRight) {
    goLeft(addr, x, y);
  } else if (can_GoRight && !can_GoLeft) {
    goRight(addr, x, y);
  } else if (random(2) == 1) { // we can go left and right, but not down
    goLeft(addr, x, y);
  } else {
    goRight(addr, x, y);
  }
  return true;
}



void fill(int addr, int maxcount) {
  int n = 8;
  byte x, y;
  int count = 0;
  for (byte slice = 0; slice < 2 * n - 1; ++slice) {
    byte z = slice < n ? 0 : slice - n + 1;
    for (byte j = z; j <= slice - z; ++j) {
      y = 7 - j;
      x = (slice - j);
      lc.setXY(addr, x, y, (++count <= maxcount));
    }
  }
}



/**
   Detect orientation using the accelerometer

       | up | right | left | down |
   --------------------------------
   400 |    |       | y    | x    |
   330 | y  | x     | x    | y    |
   260 | x  | y     |      |      |
*/
int getGravity() {
  int x = analogRead(PIN_X);
  int y = analogRead(PIN_Y);
  //Serial.println(y);
  //Serial.print(y);
  if (y < ACC_THRESHOLD_LOW)  {
    return 0;
  }
  if (x > ACC_THRESHOLD_HIGH) {
    return 90;
  }
  if (y > ACC_THRESHOLD_HIGH) {
    return 180;
  }
  if (x < ACC_THRESHOLD_LOW)  {
    return 270;
  }
}


int getTopMatrix() {
  return (getGravity() == 90) ? MATRIX_A : MATRIX_B;
}
int getBottomMatrix() {
  return (getGravity() != 90) ? MATRIX_A : MATRIX_B;
}



void resetTime() {
  for (byte i = 0; i < 2; i++) {
    lc.clearDisplay(i);
  }
  fill(getTopMatrix(), 60);
  d.Delay(getDelayDrop()*1000);
}

void displayTime(int seconds, int minutes){

    tm.point(1);
    tm.display(3, seconds % 10);
    tm.display(2, seconds / 10 % 10);
    tm.display(1, minutes % 10);
    tm.display(0, minutes / 10 % 10);
}
/**
   Traverse matrix and check if particles need to be moved
*/
bool updateMatrix() {
  int n = 8;
  bool somethingMoved = false;
  byte x, y;
  bool direction;
  for (byte slice = 0; slice < 2 * n - 1; ++slice) {
    direction = (random(2) == 1); // randomize if we scan from left to right or from right to left, so the grain doesn't always fall the same direction
    byte z = slice < n ? 0 : slice - n + 1;
    for (byte j = z; j <= slice - z; ++j) {
      y = direction ? (7 - j) : (7 - (slice - j));
      x = direction ? (slice - j) : j;
      // for (byte d=0; d<2; d++) { lc.invertXY(0, x, y); delay(50); }
      if (moveParticle(MATRIX_B, x, y)) {
        somethingMoved = true;
      };
      if (moveParticle(MATRIX_A, x, y)) {
        somethingMoved = true;
      }
    }
  }
  return somethingMoved;
}



/**
   Let a particle go from one matrix to the other
*/
boolean dropParticle() {
  if (d.Timeout()) {
    d.Delay(getDelayDrop()*1000);
    if (gravity == 0 || gravity == 180) {
      if ((lc.getRawXY(MATRIX_A, 0, 0) && !lc.getRawXY(MATRIX_B, 7, 7)) ||
          (!lc.getRawXY(MATRIX_A, 0, 0) && lc.getRawXY(MATRIX_B, 7, 7))
         ) {
        // for (byte d=0; d<8; d++) { lc.invertXY(0, 0, 7); delay(50); }
        lc.invertRawXY(MATRIX_A, 0, 0);
        lc.invertRawXY(MATRIX_B, 7, 7);
        tone(PIN_BUZZER, 440, 10);
        return true;
      }
    }
  }
  return false;
}



void alarm() {
  for (int i = 0; i < 5; i++) {
    tone(PIN_BUZZER, 440, 200);
    delay(1000);
  }
}



void resetCheck() {
  int z = analogRead(A3);
  if (z > ACC_THRESHOLD_HIGH || z < ACC_THRESHOLD_LOW) {
    resetCounter++;
    Serial.println(resetCounter);
  } else {
    resetCounter = 0;
  }
  if (resetCounter > 20) {
    Serial.println("RESET!");
    resetTime();
    resetCounter = 0;
  }
}



void displayLetter(char letter, int matrix) {
  // Serial.print("Letter: ");
  // Serial.println(letter);

  lc.clearDisplay(matrix);

  if (letter == 'M') {
    lc.setXY(matrix, 1, 4, true);
    lc.setXY(matrix, 2, 3, true);
    lc.setXY(matrix, 3, 2, true);
    lc.setXY(matrix, 4, 1, true);
  
    lc.setXY(matrix, 3, 6, true);
    lc.setXY(matrix, 4, 5, true);
    lc.setXY(matrix, 5, 4, true);
    lc.setXY(matrix, 6, 3, true);
  
    lc.setXY(matrix, 4, 2, true);
    lc.setXY(matrix, 4, 3, true);
    lc.setXY(matrix, 5, 3, true);
  }
  if (letter == 'S') {
    lc.setXY(matrix, 1, 5, true);
    lc.setXY(matrix, 2, 6, true);  
      
    lc.setXY(matrix, 3, 6, true);
    lc.setXY(matrix, 4, 5, true);

    lc.setXY(matrix, 3, 3, true);
    lc.setXY(matrix, 4, 4, true);    
    
    lc.setXY(matrix, 3, 2, true);
    lc.setXY(matrix, 4, 1, true);
  
    lc.setXY(matrix, 5, 1, true);
    lc.setXY(matrix, 6, 2, true); 
  }
}



void renderSetMinutes() {
  fill(getTopMatrix(), delayMinutes);
  displayTime(delaySeconds,delayMinutes);
  displayLetter('M', getBottomMatrix());
}
void renderSetSeconds() {
  fill(getTopMatrix(), delaySeconds);
  displayTime(delaySeconds,delayMinutes);
  displayLetter('S', getBottomMatrix());
}




void knobClockwise() {
  Serial.println("Clockwise");
  if (mode == MODE_SETSECONDS) {
    delaySeconds = constrain(delaySeconds + 1, 0, 59);
    renderSetSeconds();
  } else if (mode == MODE_SETMINUTES) {
    delayMinutes = constrain(delayMinutes + 1, 0, 60);
    renderSetMinutes();
  }
  Serial.print("Delay: ");
  Serial.println(getDelayDrop());
}
void knobCounterClockwise() {
  Serial.println("Counterclockwise");
  if (mode == MODE_SETSECONDS) {
    delaySeconds = constrain(delaySeconds - 1, 0, 59);
    renderSetSeconds();
  } else if (mode == MODE_SETMINUTES) {
    delayMinutes = constrain(delayMinutes - 1, 0, 60);
    renderSetMinutes();
  }
  Serial.print("Delay: ");
  Serial.println(getDelayDrop());
}




void boton1(unsigned long pulsacion){
  if(pulsacion>50000){
      mode = (mode + 1) % 3;
      Serial.print("Switched mode to: ");
      Serial.println(mode);
  
      if (mode == MODE_SETMINUTES) {
        lc.backup(); // we only need to back when switching from MODE_HOURGLASS->MODE_SETMINUTES
        renderSetMinutes();
      }
      if (mode == MODE_SETSECONDS) {
        renderSetSeconds();
      }
      if (mode == MODE_HOURGLASS) {
        lc.clearDisplay(0);
        lc.clearDisplay(1);
        lc.restore();
        resetTime();
      }
    }else{
      if (pulsacion>=500){
        if (mode != MODE_HOURGLASS){
          knobCounterClockwise();
        }
      }
    }
    
  //}
 
  
}


void tiempo1(){
    pulsacion = 0;    
    while(!digitalRead(PIN_BUTTON)){
      pulsacion++;
    }
    boton1(pulsacion);    
}

/**
   Button callback (incl. software debouncer)
   This switches between the modes (normal, set minutes, set hours)
*/
void buttonPush() {
    tiempo1();    
}


volatile unsigned long lastButton2PushMillis;
void button2Push() {
  if ((long)(millis() - lastButton2PushMillis) >= DEBOUNCE_THRESHOLD) {
    if (mode != MODE_HOURGLASS){
      knobClockwise();
    }
  }
}



/**
   Setup
*/
void setup() {
  Serial.begin(9600);

  // while (!Serial) {
  //   ; // wait for serial port to connect. Needed for native USB
  // }

  // setup rotary encoder
  pinMode(PIN_BUTTON, INPUT);
  pinMode(PIN_BUTTON2, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), buttonPush, LOW);  
  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON2), button2Push, RISING);
  
  tm.init();
  // set brightness; 0-7
  tm.set(2);  
  displayTime(delaySeconds,delayMinutes);

  randomSeed(analogRead(A0));

  // init displays
  for (byte i = 0; i < 2; i++) {
    lc.shutdown(i, false);
    lc.setIntensity(i, 0);
  }

  resetTime();
}



/**
   Main loop
*/
void loop() {
  delay(DELAY_FRAME);

  // update the driver's rotation setting. For the rest of the code we pretend "down" is still 0,0 and "up" is 7,7
  gravity = getGravity();
  //Serial.print(gravity);
  lc.setRotation((ROTATION_OFFSET + gravity) % 360);

  // handle special modes
  if (mode == MODE_SETMINUTES) {
    renderSetMinutes(); return;
  } else if (mode == MODE_SETSECONDS) {
    renderSetSeconds(); return;
  }

  // resetCheck(); // reset now happens when pushing a button
  bool moved = updateMatrix();
  bool dropped = dropParticle();

  // alarm when everything is in the bottom part
  if (!moved && !dropped && !alarmWentOff && (countParticles(getTopMatrix()) == 0)) {
    alarmWentOff = true;
    alarm();
  }
  // reset alarm flag next time a particle was dropped
  if (dropped) {
    alarmWentOff = false;
  }
}
