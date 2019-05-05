#include <Arduino.h>
#include <Keypad.h>
#include <MFRC522.h>
#include <eeprom.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <Servo.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);  // set the LCD address to 0x27 for a 16 chars and 2 line display

Servo servo;
									 
/* Typical pin layout used:
 * -----------------------------------------------------------------------------------------
 *             MFRC522      Arduino       Arduino   Arduino    Arduino          Arduino
 *             Reader/PCD   Uno/101       Mega      Nano v3    Leonardo/Micro   Pro Micro
 * Signal      Pin          Pin           Pin       Pin        Pin              Pin
 * -----------------------------------------------------------------------------------------
 * RST/Reset   RST          9             5         D9         RESET/ICSP-5     RST
 * SPI SS      SDA(SS)      10            53        D10        10               10
 * SPI MOSI    MOSI         11 / ICSP-4   51        D11        ICSP-4           16
 * SPI MISO    MISO         12 / ICSP-1   50        D12        ICSP-1           14
 * SPI SCK     SCK          13 / ICSP-3   52        D13        ICSP-3           15
 */
const int RST_PIN = 5;        
MFRC522 mfrc522(SS, RST_PIN);  // Create MFRC522 instance

const byte ROWS = 4;  // four rows
const byte COLS = 4;  // four columns
// define the cymbols on the buttons of the keypads
char hexaKeys[ROWS][COLS] = { {'1', '2', '3', 'A'},
							  {'4', '5', '6', 'B'},
							 {'7', '8', '9', 'C'},
							 {'*', '0', '#', 'D'} };
byte rowPins[ROWS] = { 13, 12, 11, 10 };  // connect to the row pinouts of the keypad
byte colPins[COLS] = { 9, 8, 7, 6 };  // connect to the column pinouts of the keypad

// initialize an instance of class NewKeypad
Keypad customKeypad =
Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);

const char *password = "1234";
boolean onInput = true;

// Holds all the card's informations
String strContent;
int balanceLeft;

void setup() {
	Serial.begin(9600);
	EEPROM.begin(); // 1024 bytes of memory

	// Init servo motor
	servo.attach(4);

	// Init LiquidCrystal_I2C
	lcd.begin();
	lcd.backlight();
	lcd.setCursor(0, 0);

	// Init SPI bus
	SPI.begin();

	// Init MFRC522
	mfrc522.PCD_Init();
	// Show details of PCD - MFRC522 Card Reader details
	mfrc522.PCD_DumpVersionToSerial();

	Serial.println("CURRENT CONTENT");
	Serial.println(EEPROMGetContents());
	Serial.println("--------------------------------------------------------");

	servo.write(180);
}

void loop() {
	lcd.setCursor(0, 0);
	lcd.print("Tap a PICC card");

	// Wait for PICC tap
	if (!mfrc522.PICC_IsNewCardPresent()) {
		return;
	}

	if (!mfrc522.PICC_ReadCardSerial()) {
		return;
	}

	// Enable keypad input
	onInput = true;
	
	// Now that if card has been tapped get RFID information
	char *id = dump_byte_array(mfrc522.uid.uidByte, mfrc522.uid.size);

	lcd.clear();
	lcd.setCursor(0, 0);
	lcd.print("Verifying...");

	if (!Exist(id)) {
		Serial.println("ID does not exist");
		lcd.clear();
		lcd.setCursor(0, 0);
		lcd.print("PICC does not");
		lcd.setCursor(0, 1);
		lcd.print("exist");
		
		delay(1500);

		lcd.clear();

		return;
	}

	lcd.clear();
	lcd.setCursor(0, 0);
	lcd.print("Processing...");

	// Evaluate the PICC card content
	CaptureHEX(id);

	// Do not proceed with low balance
	if (balanceLeft <= 0) {
		lcd.clear();
		lcd.setCursor(0, 0);
		lcd.print("Bal not enough.");
		lcd.setCursor(0, 1);
		lcd.print("Process failed.");

		delay(1500);

		lcd.clear();

		onInput = false;

		return;
	}

	delay(1500);

	lcd.clear();
	lcd.setCursor(0, 0);
	lcd.print("Input password");

	delay(1500);

	lcd.clear();

	// User input
	char userInput[10];
	char *userInputPtr = userInput;
	int index = 0;
	uint8_t col = 0;
	while (onInput) {
		char key = customKeypad.getKey();

		if (key) {
			Serial.println(key);
			userInput[index++] = key;

			// Display every keypad input
			lcd.setCursor(col++, 0);
			// Only print asterisks
			lcd.print("*");

			// 
			if (key == 'D') {
				// End the input with a null terminate to indicate end of string
				userInput[0] = '\0';

				index = 0;

				col = 0;

				lcd.clear();
			}

			// # will be the enter key
			if (key == '#') {

				// Disregard the hash input
				userInput[index - 1] = '\0';

				// Compare input and password
				if (strcmp(password, userInputPtr) == 0) {

					lcd.clear();
					lcd.setCursor(0, 0);
					lcd.print("Processing...");

					// Clear EEPROM
					ClearEEPROM();

					Serial.println("Updating contents...");
					// Then replace with new values
					WriteString(0, strContent);

					Serial.println("--------------------------------------------------------");
					Serial.println(EEPROMGetContents());

					servo.write(90);

					Serial.println("Access granted");

					lcd.clear();
					lcd.setCursor(0, 0);
					lcd.print("Paid!");
					lcd.setCursor(0, 1);
					lcd.print("Bal left:");
					lcd.setCursor(10, 1);
					lcd.print(balanceLeft);

					delay(1500);

					// Clear lcd
					lcd.clear();

					// Reset current index
					index = 0;

					// Null terminate first element/Erase array content
					userInput[0] = '\0';

					onInput = false;

					servo.write(180);
				}
				else {
					lcd.clear();
					lcd.setCursor(0, 0);
					lcd.print("Invalid password");

					delay(1500);

					lcd.clear();

					// Reset current index
					index = 0;

					// Null terminate first element/Erase array content
					userInput[0] = '\0';

					onInput = false;
				}
			}
		}
	}
}

// Helper routine to dump a byte array as hex values to Serial.
char *dump_byte_array(byte *buffer, byte bufferSize) {
	char data[11];
	sprintf(data, "%02X:%02X:%02X:%02X", buffer[0], buffer[1], buffer[2], buffer[3]);

	Serial.print(F("Scanned -> "));
	Serial.println(data);

	return strdup(data);
}

void CaptureHEX(char *target) {
	/*
		How the content are handled...

		EEPROM CONTENT:
			79:02:B5:55,2,500|69:15:B8:55,3,500| ... |69:15:B8:55,3,500
			      |
				  |_ _ _ 79:02:B5:55,2,500
								|	
								| _ _ _ [79:02:B5:55] [2]	[500]
											|		   |	  |
										  Card ID	 Class  Balance
		
		They're splitted with comma then evaluates whether its an ID, Class or a Balance
	 */
	char str[1024];

	// Cache the EEPROM content to str
	strcpy(str, EEPROMGetContents().c_str());

	strContent = String(str);
	Serial.println(strContent);

	// Split EEPROM content with '|' character. See serial monitor for details
	char *bufferPtr = strtok(str, "|");
	
	char classType;
	char balance[5];
	String newContent;
	while (bufferPtr != nullptr) {
		bufferPtr = strtok(nullptr, "|");

		// If the card exists within the list of EEPROM values
		if (strstr(bufferPtr, target) == nullptr) {
			continue;
		}

		// Get the class definiton
		classType = *(bufferPtr + 12);

		// The character we'll look for upon when to split
		char *token = strrchr(bufferPtr, ',');

		// Resolve/Fetch balance value for scanned card
		balance[5];
		int index = 0;
		for (int i = token - bufferPtr + 1; i < strlen(bufferPtr); i++) {
			balance[index++] = bufferPtr[i];
		}

		balance[index] = '\0';

		// Evaluate how much should be deducted
		balanceLeft = atoi(balance);

		if (classType == '1') {
			balanceLeft -= 24;
		}
		else if (classType == '2') {
			balanceLeft -= 42;
		}
		else if (classType == '3') {
			balanceLeft -= 72;
		}

		// All information are now cached and ready to be used or discarded
		newContent = String(target) + "," + String(classType) + "," + balanceLeft + "|";

		// Stop scanning
		break;
	}

	// Updated strContent value
	strContent.replace(String(target), newContent);
}

void ClearEEPROM() {
	// Write 0 values to all memory addresses of EEPROM. Basically erase the data
	for (size_t i = 0; i < EEPROM.length(); i++) {
		EEPROM.write(i, 0);
	}
}

void WriteString(char add, String data) {
	// Size of the data to be writtent
	int _size = data.length();
	int i;
	// Write every element of the data
	for (i = 0; i < _size; i++) {
		EEPROM.write(add + i, data[i]);
	}

	// Add termination null character for String Data 
	// as per requirement when handling characters
	EEPROM.write(add + _size, '\0'); 
}

String EEPROMGetContents() {
	char data[1021];
	unsigned char k;
	int memPtr = 0;
	k = EEPROM.read(0);
	//Read until null character
	while (k != '\0' && memPtr < 1021) {
		k = EEPROM.read(0 + memPtr);
		data[memPtr] = k;
		memPtr++;
	}
	data[memPtr] = '\0';
	return String(data);
}

boolean Exist(char *id) {
	char content[1024];

	strcpy(content, EEPROMGetContents().c_str());

	if (strstr(content, id) != nullptr) {
		content[0] = '\0';
		return true;
	}
	else {
		content[0] = '\0';
		return false;
	}
}