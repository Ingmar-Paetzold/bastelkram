/*
    link between the computer and the RFID Shield
    at 9600 bps 8-N-1
    Computer is connected to Hardware UART
    rfidSerial Shield is connected to the Software UART
*/

#include <Arduino.h>

#include <SoftwareSerial.h>
#include <EEPROM.h>
#include <stdint.h>
#include <ctype.h>
#include <string.h>



/*
     Pin assignment
*/
static const uint8_t statusLedPin = 13U;
static const uint8_t softRxPin = 10;
static const uint8_t softTxPin = 11;

/*
    EEPROM stuff
*/
static const uint8_t maxRecords = 26U;
static const uint8_t idLength = 16U;
static const uint8_t nameLength = 16U;


class Record
{
        char id[ idLength ];
        char name[ nameLength ];
};


/*
    Control
*/
static bool isAdminMode = false;
typedef void (*stateType)( char );
static stateType adminState = NULL;
static uint8_t recordUnderWork = 0;
static uint8_t nameInputLength = 0;
static char nameInputBuffer[nameLength];


/*
    RFID stuff
*/

static const char STX_CHAR = 2;
static const char ETX_CHAR = 3;

SoftwareSerial rfidSerial( softRxPin, softTxPin );
static const uint8_t rfidBufferSize = 64U;
static uint8_t rfidBuffer[ rfidBufferSize ];
static uint8_t rfidBufferIndex = 0;




/*--------------------------------------------------------------------------------------------*/
void setup()
{
    pinMode( statusLedPin, OUTPUT);

    rfidSerial.begin(9600);     // the rfidSerial baud rate
    Serial.begin(9600);         // the Serial port of Arduino baud rate.
}


/*--------------------------------------------------------------------------------------------*/
void loop()
{
    if ( Serial.available() > 0 )
    {
        adminMode( Serial.read() );
    }
    else if ( ! isAdminMode )
    {
        if ( isRfidComplete() )
        {
            processRfid();
        }
    }


    //    if( Serial.available() )             // if data is available on hardware serial port ==> data is coming from PC or notebook
    //    {
    //        rfidSerial.write(Serial.read());    // write it to the rfidSerial shield
    //    }
}


/*--------------------------------------------------------------------------------------------*/
void printHex8( uint16_t num )
{
    char tmp[3];
    sprintf( tmp, "%02X", num );
    Serial.print( tmp );
}

/*--------------------------------------------------------------------------------------------*/
void printHex16( uint16_t num )
{
    char tmp[5];
    sprintf( tmp, "%04X", num );
    Serial.print( tmp );
}

/*--------------------------------------------------------------------------------------------*/
inline uint16_t baseAddressOf( uint8_t record )
{
    return record * sizeof( Record );
}


/*  ----------------------------------------------------------------------------------------------
    Tries to read from the RFID interface
    Returns true when an ID is complete in the buffer
    ----------------------------------------------------------------------------------------------*/
bool isRfidComplete()
{
    bool isComplete = false;

    while ( rfidSerial.available() > 0 )
    {
        char character = rfidSerial.read();
        //Serial.print( (unsigned int)character, HEX );
        //Serial.write( ' ' );

        if ( character == STX_CHAR )
        {
            rfidBufferIndex = 0;
        }
        else if ( character == ETX_CHAR )
        {
            isComplete = true;
            Serial.print( "Complete ID read: " );
            Serial.write( rfidBuffer, rfidBufferIndex );
            Serial.write( '\n' );
        }
        else
        {
            rfidBuffer[ rfidBufferIndex++ ] = character;

            if ( rfidBufferIndex == rfidBufferSize )
            {
                break;
            }
        }
    }

    return isComplete;
}


/*  ----------------------------------------------------------------------------------------------
    Searches the EEPROM for the ID in the rfidBuffer
    If found, returns its index,
    if not, returns maxRecords
*/
uint8_t findIdInEeprom()
{
    uint8_t record;

    for ( record = 0; record != maxRecords; ++record )
    {
        const uint16_t baseAddress = baseAddressOf( record );

        if ( (EEPROM.read( baseAddress ) != 0x00) && isEqualInEeprom( baseAddress, rfidBuffer, rfidBufferIndex ) )
        {
            Serial.println( "Equal" );
            break;
        }
    }

    return record;
}


/*--------------------------------------------------------------------------------------------*/
bool isEqualInEeprom( const uint16_t eepromBaseAddress, const uint8_t* memoryAddress, size_t number )
{
    bool isEqual = true;

    for ( uint8_t pos = 0; pos != number; ++pos )
    {
        if ( EEPROM.read( eepromBaseAddress + pos ) != *(memoryAddress + pos) )
        {
            isEqual = false;
            break;
        }
    }

    return isEqual;
}


/*  ----------------------------------------------------------------------------------------------
    Searches the next free space in EEPROM (where first byte is 00)
    If not found, return maxRecords
*/
uint8_t findFreeSpaceInEeprom()
{
    uint8_t record = 0U;

    for ( ; record != maxRecords; ++record )
    {
        if ( EEPROM.read( baseAddressOf( record ) ) == 0x00 )
        {
            break;
        }
    }

    return record;
}


/*--------------------------------------------------------------------------------------------*/
void processRfid()
{
    Serial.println( "Process RFID" );
    static bool isInsertMode = false;

    uint8_t foundRecordIndex = findIdInEeprom();

    // Found in EEPROM?
    if ( foundRecordIndex != maxRecords )
    {
        // Master card?
        const uint16_t nameAddress = baseAddressOf( foundRecordIndex ) + idLength;

        if ( isEqualInEeprom( nameAddress, (const uint8_t*)"Master", 5 ) )
        {
            Serial.println( "Master card." );
            isInsertMode = ! isInsertMode;
            digitalWrite( statusLedPin, isInsertMode);
        }
        else
        {
            Serial.println( "Known user card." );

            if ( isInsertMode )
            {
                isInsertMode = false;
                digitalWrite( statusLedPin, isInsertMode);
            }
            else
            {
                // TODO: gate open logic
            }
        }
    }

    // unknown card
    else
    {
        if ( isInsertMode )
        {
            uint8_t freeRecordSpace = findFreeSpaceInEeprom();

            if ( freeRecordSpace != maxRecords )
            {
                Serial.print( "Insert to slot: " );
                Serial.println( freeRecordSpace );

                uint16_t baseAddress = baseAddressOf( freeRecordSpace );

                for ( uint8_t pos = 0; pos != rfidBufferIndex; ++pos )
                {
                    EEPROM[ baseAddress + pos ] = rfidBuffer[ pos ];
                }
            }
            else
            {
                Serial.println( "No slot free in EEPROM!" );
            }

            isInsertMode = false;
            digitalWrite( statusLedPin, isInsertMode);
        }
        else
        {
            Serial.println( "Unknown card. Ignore." );
        }
    }



    clearBufferArray();
}


/*--------------------------------------------------------------------------------------------*/
void adminMode( char key )
{
    if ( adminState == NULL && key == 'a' )
    {
        showStateMainMenu();
        adminState = &stateAdminMain;
    }
    else
    {
        // call next state;
        adminState( key );
    }
}


/*--------------------------------------------------------------------------------------------*/
void showStateMainMenu()
{
    Serial.println( );
    Serial.println( "* Admin mode *" );
    Serial.println( );

    for ( uint8_t i = 0; i != maxRecords; ++i )
    {
        showEepromBlock( i );
    }

    Serial.println( );
    Serial.println( "a .. z  select a record" );
    Serial.println( "X       erase EEPROM" );
    Serial.println( "Q       quit admin mode" );
    Serial.println( );
}


/*--------------------------------------------------------------------------------------------*/
void stateAdminMain( char key )
{
    if ( key >= 'a' && key <= 'z' )
    {
        uint8_t record = key - 'a';

        if ( EEPROM.read( baseAddressOf( record ) ) == 0x00 )
        {
            Serial.println( "Selected ID is empty!" );
        }
        else
        {
            recordUnderWork = record;
            Serial.println( "n assign a name to record" );
            Serial.println( "d delete record" );
            Serial.println( "Q back to main menu" );
            adminState = stateAdminActionWithSelectedId;
        }
    }
    else if ( key == 'Q' )
    {
        Serial.println( "bye" );
        adminState = NULL;
    }
    else if ( key == 'X' )
    {
        // erase EEPROM
    }
}


/*--------------------------------------------------------------------------------------------*/
void stateAdminActionWithSelectedId( char key )
{
    switch ( key )
    {
        case 'Q':
            showStateMainMenu();
            adminState = stateAdminMain;
            break;

        case 'd':
            deleteRecord();
            showStateMainMenu();
            adminState = stateAdminMain;
            break;

        case 'n':
            Serial.println( "Enter name, finish with #" );
            memset( (void*)nameInputBuffer, 0, nameLength );
            nameInputLength = 0;
            adminState = stateAdminNameRecord;
            break;
    }
}


/*--------------------------------------------------------------------------------------------*/
void deleteRecord()
{
    uint16_t baseAddress = baseAddressOf( recordUnderWork );

    for ( uint8_t pos = 0; pos != sizeof( Record ); ++pos )
    {
        EEPROM.update( baseAddress + pos, 0 );
    }
}


/*--------------------------------------------------------------------------------------------*/
void stateAdminNameRecord( char key )
{
    if ( key == '#' )
    {
        Serial.print( '\n' );

        uint16_t baseAddress = baseAddressOf( recordUnderWork ) + idLength;

        // Copy the whole buffer incl. the null bytes
        for ( uint8_t pos = 0; pos != nameLength; ++pos )
        {
            EEPROM[ baseAddress + pos ] = nameInputBuffer[ pos ];
        }

        // Finished, then back to main menu
        showStateMainMenu();
        adminState = stateAdminMain;
    }
    else if ( isprint( key ) && (nameInputLength < nameLength) )
    {
        Serial.print( key );
        nameInputBuffer[ nameInputLength ] = key;
        ++nameInputLength;
    }
}


/*--------------------------------------------------------------------------------------------*/
void showEepromBlock( uint8_t record )
{
    Serial.print( (char)(record + 'a') );
    Serial.print( " - " );

    uint16_t address = record * sizeof( Record );
    printHex16( address );
    Serial.print( ": " );

    for ( uint8_t pos = 0; pos != sizeof( Record ); ++pos )
    {
        printHex8( EEPROM.read( address + pos ) );
        Serial.print( ' ' );
    }

    for ( uint8_t pos = 0; pos != sizeof( Record ); ++pos )
    {
        uint8_t value = EEPROM.read( address + pos );

        if ( isprint( value ))
        {
            Serial.print( (char)value );
        }
        else
        {
            Serial.print( '.' );
        }
    }
    Serial.print( '\n' );
}


/*--------------------------------------------------------------------------------------------*/
void clearBufferArray()                 // function to clear buffer array
{
    // clear all index of array with command NULL
    for ( int i = 0; i < rfidBufferIndex; i++ )
    {
        rfidBuffer[i] = '\0';
    }

    rfidBufferIndex = 0;
}


/*--------------------------------------------------------------------------------------------*/
void eraseEeprom()
{
    for ( uint16_t i = 0; i < EEPROM.length(); i++ )
    {
        EEPROM.write( i, 0 );
    }
}
