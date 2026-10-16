// Geigerteller-geluid via de ingebouwde I2S-versterker (SPK-connector):
// hoe sterker het signaal, hoe korter de pauze tussen de piepjes.
#pragma once

namespace beeper {
void begin();
// Elke meetronde: alarm 0 = stil, 1/2 = piepen; db = sterkte; soft = oranje drempel.
void update(int alarm, float db, float soft);
void test();          // geigerteller-demo (traag → snel) van SOUND_TEST_S
}
