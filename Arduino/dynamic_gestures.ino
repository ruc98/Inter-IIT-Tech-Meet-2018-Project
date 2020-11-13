 #include <Wire.h>
#include <Kalman.h> // Source: https://github.com/TKJElectronics/KalmanFilter
#include "I2Cdev.h"
#include "MPU6050.h"
#define OUTPUT_READABLE_ACCELGYRO
#define RESTRICT_PITCH // Comment out to restrict roll to ±90deg instead - please read: http://www.freescale.com/files/sensors/doc/app_note/AN3461.pdf
MPU6050 accelgyro;
int minVal=265;
int maxVal=402;
double x,y,z;
Kalman kalmanX; // Create the Kalman instances
Kalman kalmanY;
//button variables
int pushButton = 2;
unsigned long lastDebounceTime = {0}; 
unsigned long debounceDelay = 50;    
const int buttonPin= 3;
const int buttonPin1=4;
int buttonState= {LOW};
int buttonState1= {LOW};                  
int lastButtonState = {LOW};
int reading;
int lastButtonState1={LOW};

//flex sensor variables
int flexSensorPin0 = A0; //analog pin 0
int flexSensorPin1 = A1; //analog pin 1
int flexSensorPin2 = A2; //analog pin 2
int flexSensorPin3 = A3; //analog pin 3
int flexSensorPin4 = A4; //analog pin 4
int flexSensorPin5 = A5; //analog pin 5
int flexSensorPin6 = A6; //analog pin 6



/* IMU Data */
int16_t ax, ay, az;
int16_t gx, gy, gz;
int16_t tempRaw;
double accX, accY, accZ;
double gyroX, gyroY, gyroZ;
double smooth_ax, smooth_ay, smooth_az;
double smooth_gx, smooth_gy, smooth_gz;




double gyroXangle, gyroYangle; // Angle calculate using the gyro only
double compAngleX, compAngleY; // Calculated angle using a complementary filter
double kalAngleX, kalAngleY; // Calculated angle using a Kalman filter

uint32_t timer;
uint8_t i2cData[14]; // Buffer for I2C data

// TODO: Make calibration routine
const uint8_t IMUAddress = 0x68; // AD0 is logic low on the PCB
const uint16_t I2C_TIMEOUT = 1000; // Used to check for errors in I2C communication

uint8_t i2cWrite(uint8_t registerAddress, uint8_t data, bool sendStop) {
  return i2cWrite(registerAddress, &data, 1, sendStop); // Returns 0 on success
}

uint8_t i2cWrite(uint8_t registerAddress, uint8_t *data, uint8_t length, bool sendStop) {
  Wire.beginTransmission(IMUAddress);
  Wire.write(registerAddress);
  Wire.write(data, length);
  uint8_t rcode = Wire.endTransmission(sendStop); // Returns 0 on success
  if (rcode) {
    Serial.print(F("i2cWrite failed: "));
    Serial.println(rcode);
  }
  return rcode; // See: http://arduino.cc/en/Reference/WireEndTransmission
}

uint8_t i2cRead(uint8_t registerAddress, uint8_t *data, uint8_t nbytes) {
  uint32_t timeOutTimer;
  Wire.beginTransmission(IMUAddress);
  Wire.write(registerAddress);
  uint8_t rcode = Wire.endTransmission(false); // Don't release the bus
  if (rcode) {
    Serial.print(F("i2cRead failed: "));
    Serial.println(rcode);
    return rcode; // See: http://arduino.cc/en/Reference/WireEndTransmission
  }
  Wire.requestFrom(IMUAddress, nbytes, (uint8_t)true); // Send a repeated start and then release the bus after reading
  for (uint8_t i = 0; i < nbytes; i++) {
    if (Wire.available())
      data[i] = Wire.read();
    else {
      timeOutTimer = micros();
      while (((micros() - timeOutTimer) < I2C_TIMEOUT) && !Wire.available());
      if (Wire.available())
        data[i] = Wire.read();
      else {
        Serial.println(F("i2cRead timeout"));
        return 5; // This error value is not already taken by endTransmission
      }
    }
  }
  return 0; // Success
}



void setup() {
  Serial.begin(38400);
  pinMode(pushButton, INPUT);
  pinMode(buttonPin, INPUT);
  pinMode(buttonPin1, INPUT);
  
    
  #if ARDUINO >= 157
    Wire.setClock(400000UL); // Set I2C frequency to 400kHz
    #else
      TWBR = ((F_CPU / 400000UL) - 16) / 2; // Set I2C frequency to 400kHz
  #endif

  i2cData[0] = 7; // Set the sample rate to 1000Hz - 8kHz/(7+1) = 1000Hz
  i2cData[1] = 0x00; // Disable FSYNC and set 260 Hz Acc filtering, 256 Hz Gyro filtering, 8 KHz sampling
  i2cData[2] = 0x00; // Set Gyro Full Scale Range to ±250deg/s
  i2cData[3] = 0x00; // Set Accelerometer Full Scale Range to ±2g
  while (i2cWrite(0x19, i2cData, 4, false)); // Write to all four registers at once
  while (i2cWrite(0x6B, 0x01, true)); // PLL with X axis gyroscope reference and disable sleep mode

  while (i2cRead(0x75, i2cData, 1));
  if (i2cData[0] != 0x68) { // Read "WHO_AM_I" register
    Serial.print(F("Error reading sensor"));
    while (1);
  }

  delay(100); // Wait for sensor to stabilize

  /* Set kalman and gyro starting angle */
  while (i2cRead(0x3B, i2cData, 6));
  accX = (int16_t)((i2cData[0] << 8) | i2cData[1]);
  accY = (int16_t)((i2cData[2] << 8) | i2cData[3]);
  accZ = (int16_t)((i2cData[4] << 8) | i2cData[5]);

  // Source: http://www.freescale.com/files/sensors/doc/app_note/AN3461.pdf eq. 25 and eq. 26
  // atan2 outputs the value of -π to π (radians) - see http://en.wikipedia.org/wiki/Atan2
  // It is then converted from radians to degrees
#ifdef RESTRICT_PITCH // Eq. 25 and 26
  double roll  = atan2(accY, accZ) * RAD_TO_DEG;
  double pitch = atan(-accX / sqrt(accY * accY + accZ * accZ)) * RAD_TO_DEG;
#else // Eq. 28 and 29
  double roll  = atan(accY / sqrt(accX * accX + accZ * accZ)) * RAD_TO_DEG;
  double pitch = atan2(-accX, accZ) * RAD_TO_DEG;
#endif

  kalmanX.setAngle(roll); // Set starting angle
  kalmanY.setAngle(pitch);
  gyroXangle = roll;
  gyroYangle = pitch;
  compAngleX = roll;
  compAngleY = pitch;

  timer = micros();
  Serial.println("STARTING BATCH");
}



void loop() {
  /* Update all the values */
int buttonState1 = digitalRead(pushButton);
int buttonState2 = digitalRead(buttonPin1);
int flexSensorReading0 = analogRead(flexSensorPin0);
int flexSensorReading1 = analogRead(flexSensorPin1);
int flexSensorReading2 = analogRead(flexSensorPin2);
int flexSensorReading3 = analogRead(flexSensorPin3);
int flexSensorReading4 = analogRead(flexSensorPin4);
int flexSensorReading5 = analogRead(flexSensorPin5);
int flexSensorReading6 = analogRead(flexSensorPin6);

//int flex0to1000 = map(flexSensorReading0, 280, 430, 0, 100);
//int flex0to1001 = map(flexSensorReading1, 180, 300, 0, 100);
{
   reading= digitalRead(buttonPin);       
  if (reading != lastButtonState)
      {
         lastDebounceTime = millis();
       }
  if ((millis() - lastDebounceTime) > debounceDelay)
  {
          
    if (reading != buttonState) 
   {  
      buttonState= reading;          
      if (buttonState == HIGH) 
       {                              
          //Serial.print('\n');
          Serial.println("CLOSING BATCH");
          Serial.print('\n');
          Serial.println("STARTING BATCH");  
        }         
           }
  }
lastButtonState= reading;                      
}
  accelgyro.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

  smooth_ax = 0.95 * smooth_ax + 0.05 * ax;
  smooth_ay = 0.95 * smooth_ay + 0.05 * ay;
  smooth_az = 0.95 * smooth_az + 0.05 * az;
  smooth_gx = 0.95 * smooth_gx + 0.05 * gx;
  smooth_gy = 0.95 * smooth_gy + 0.05 * gy;
  smooth_gz = 0.95 * smooth_gz + 0.05 * gz;

  while (i2cRead(0x3B, i2cData, 14));
  accX = (int16_t)((i2cData[0] << 8) | i2cData[1]);
  accY = (int16_t)((i2cData[2] << 8) | i2cData[3]);
  accZ = (int16_t)((i2cData[4] << 8) | i2cData[5]);
  tempRaw = (int16_t)((i2cData[6] << 8) | i2cData[7]);
  gyroX = (int16_t)((i2cData[8] << 8) | i2cData[9]);
  gyroY = (int16_t)((i2cData[10] << 8) | i2cData[11]);
  gyroZ = (int16_t)((i2cData[12] << 8) | i2cData[13]);;

  double dt = (double)(micros() - timer) / 1000000; // Calculate delta time
  timer = micros();

  // Source: http://www.freescale.com/files/sensors/doc/app_note/AN3461.pdf eq. 25 and eq. 26
  // atan2 outputs the value of -π to π (radians) - see http://en.wikipedia.org/wiki/Atan2
  // It is then converted from radians to degrees
#ifdef RESTRICT_PITCH // Eq. 25 and 26
  double roll  = atan2(accY, accZ) * RAD_TO_DEG;
  double pitch = atan(-accX / sqrt(accY * accY + accZ * accZ)) * RAD_TO_DEG;
#else // Eq. 28 and 29
  double roll  = atan(accY / sqrt(accX * accX + accZ * accZ)) * RAD_TO_DEG;
  double pitch = atan2(-accX, accZ) * RAD_TO_DEG;
#endif

  double gyroXrate = gyroX / 131.0; // Convert to deg/s
  double gyroYrate = gyroY / 131.0; // Convert to deg/s

#ifdef RESTRICT_PITCH
  // This fixes the transition problem when the accelerometer angle jumps between -180 and 180 degrees
  if ((roll < -90 && kalAngleX > 90) || (roll > 90 && kalAngleX < -90)) {
    kalmanX.setAngle(roll);
    compAngleX = roll;
    kalAngleX = roll;
    gyroXangle = roll;
  } else
    kalAngleX = kalmanX.getAngle(roll, gyroXrate, dt); // Calculate the angle using a Kalman filter

  if (abs(kalAngleX) > 90)
    gyroYrate = -gyroYrate; // Invert rate, so it fits the restriced accelerometer reading
  kalAngleY = kalmanY.getAngle(pitch, gyroYrate, dt);
#else
  // This fixes the transition problem when the accelerometer angle jumps between -180 and 180 degrees
  if ((pitch < -90 && kalAngleY > 90) || (pitch > 90 && kalAngleY < -90)) {
    kalmanY.setAngle(pitch);
    compAngleY = pitch;
    kalAngleY = pitch;
    gyroYangle = pitch;
  } else
    kalAngleY = kalmanY.getAngle(pitch, gyroYrate, dt); // Calculate the angle using a Kalman filter

  if (abs(kalAngleY) > 90)
    gyroXrate = -gyroXrate; // Invert rate, so it fits the restriced accelerometer reading
  kalAngleX = kalmanX.getAngle(roll, gyroXrate, dt); // Calculate the angle using a Kalman filter
#endif

  gyroXangle += gyroXrate * dt; // Calculate gyro angle without any filter
  gyroYangle += gyroYrate * dt;
  //gyroXangle += kalmanX.getRate() * dt; // Calculate gyro angle using the unbiased rate
  //gyroYangle += kalmanY.getRate() * dt;

  compAngleX = 0.93 * (compAngleX + gyroXrate * dt) + 0.07 * roll; // Calculate the angle using a Complimentary filter
  compAngleY = 0.93 * (compAngleY + gyroYrate * dt) + 0.07 * pitch;

  // Reset the gyro angle when it has drifted too much
  if (gyroXangle < -180 || gyroXangle > 180)
    gyroXangle = kalAngleX;
  if (gyroYangle < -180 || gyroYangle > 180)
    gyroYangle = kalAngleY;

  /* Print Data */
int xAng = map(smooth_ax,minVal,maxVal,-90,90);
int yAng = map(smooth_ay,minVal,maxVal,-90,90); 
int zAng = map(smooth_az,minVal,maxVal,-90,90);
x= RAD_TO_DEG * (atan2(-yAng, -zAng)+PI); 
y= RAD_TO_DEG * (atan2(-xAng, -zAng)+PI);
z= RAD_TO_DEG * (atan2(-yAng, -xAng)+PI);
if(z>=180&&z<360){
  z=-(360-z);
}

if(buttonState1==1 ){
  Serial.print(flexSensorReading0);//thumb
  Serial.print('\t');
  Serial.print(flexSensorReading1);//index
  Serial.print('\t');
  Serial.print(flexSensorReading2);//middle
  Serial.print('\t');
  Serial.print(flexSensorReading3);//ring
  Serial.print('\t');
  Serial.print(flexSensorReading4);//little
  Serial.print('\t');
  Serial.print(flexSensorReading5);//wrist
  Serial.print('\t');
  Serial.print(flexSensorReading6);//elbow
  Serial.print('\t');
  //Serial.print(roll); Serial.print("\t\t");
  //Serial.print(gyroXangle); Serial.print("\t\t");
  Serial.print(compAngleX); Serial.print("\t");
  //Serial.print(kalAngleX); Serial.print("\t\t");

  //Serial.print("\t");

  //Serial.print(pitch); Serial.print("\t\t");
  //Serial.print(gyroYangle); Serial.print("\t\t");
  Serial.print(compAngleY); Serial.print("\t");
  //Serial.print(kalAngleY); Serial.print("\t\t");
  Serial.print(z);Serial.print('\t');
  #ifdef OUTPUT_READABLE_ACCELGYRO
        // display tab-separated accel/gyro x/y/z values
        Serial.print(smooth_ax); Serial.print("\t");
        Serial.print(smooth_ay); Serial.print("\t");
        Serial.print(smooth_az); Serial.print("\t");
        Serial.print(smooth_gx); Serial.print("\t");
        Serial.print(smooth_gy); Serial.print("\t");
        Serial.print(smooth_gz);
    #endif

  Serial.print("\r\n");
  delay(2);
}

}
