// Example usage for Wiegand library by MonkeyBoard.

#include "Wiegand.h"

// Initialize objects from the lib
Wiegand wiegand;

void setup() {
    // Call functions on initialized library objects that require hardware
    wiegand.begin();
}

void loop() {
    // Use the library's initialized objects and functions
    wiegand.process();
}
