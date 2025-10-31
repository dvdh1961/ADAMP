// Dit is onze custom "dummy" vcl.h om de C-core te laten compileren met Qt.
#ifndef VCL_SHIM_H
#define VCL_SHIM_H

// We gebruiken Qt's types waar mogelijk voor een soepele overgang.
#include <cstdint> // Voor standaard integer types zoals uint32_t

// Dummy definities voor VCL classes.
// Dit zijn "forward declarations". Ze vertellen de compiler alleen "dit type bestaat",
// zonder te zeggen wat het doet. Dat is vaak al genoeg.
class TObject {};
class TBitmap : public TObject {};
class TCanvas : public TObject {};
class TImage : public TObject {};

// Definieer een struct voor een rechthoek (TRect)
// De originele code gebruikt waarschijnlijk de members 'left', 'top', 'right', 'bottom'.
struct TRect {
    long left;
    long top;
    long right;
    long bottom;
};

// Typedef voor kleuren. VCL gebruikte TColor.
// We mappen dit naar een simpele 32-bit integer.
typedef uint32_t TColor;

// Definieer de basiskleuren die de code zou kunnen gebruiken.
// Dit zijn placeholder-waarden; ze doen er nu nog niet toe.
const TColor clBlack = 0x000000;
const TColor clWhite = 0xFFFFFF;
// Voeg hier eventueel andere 'cl...' kleuren toe als de compiler erover klaagt.


// Zorg ervoor dat Pascal-specifieke keywords niet voor problemen zorgen.
#define __fastcall

#endif // VCL_SHIM_H
