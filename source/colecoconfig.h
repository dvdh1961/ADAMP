#ifndef COLECOCONFIG_SHIM_H
#define COLECOCONFIG_SHIM_H

#ifdef __cplusplus
// --- C++ DEFINITIES (voor .cpp bestanden) ---

struct TCheckBox {
    bool Checked;
};

struct TComboBox {
    int ItemIndex;
};

class TFormColeco {
public:
    TComboBox *ComboBoxPad1;
    TComboBox *ComboBoxPad2;
    TCheckBox *CheckBoxSav;
};

extern TFormColeco* FormColeco;

#else
// --- C DEFINITIES (voor .c bestanden) ---
#include <stdbool.h> // C heeft dit nodig voor 'bool'

struct TCheckBox {
    bool Checked;
};

struct TComboBox {
    int ItemIndex;
};

// C kent geen 'class', dus we gebruiken 'struct'
struct TFormColeco {
    struct TComboBox *ComboBoxPad1;
    struct TComboBox *ComboBoxPad2;
    struct TCheckBox *CheckBoxSav;
};

extern struct TFormColeco* FormColeco;

#endif // __cplusplus

#endif // COLECOCONFIG_SHIM_H
