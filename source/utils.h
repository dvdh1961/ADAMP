#ifndef UTILS_H
#define UTILS_H

#include <QString>
#include <QImage>
#include <cstdint> // Voor uint32_t

/*
 * utils.h
 *
 * Gemigreerde versie voor Qt6.
 * Alle VCL-types zoals AnsiString en Graphics::TBitmap zijn vervangen.
 */

// --- C/C++ Functies ---
// Dit is de zelfstandige CRC32-functie uit utils.cpp
uint32_t CRC32Block(const unsigned char *buf, unsigned int len);

// --- C++ (Qt) Functies ---
QString FileNameGetPath(QString Fname);
QString FileNameGetExt(QString Fname);
QString GetExt(QString Fname);
QString GetFileName(QString Fname); // De originele code had deze, maar niet in de header
QString NameAndDateTimePng(QString name);
bool ImageToPNG(QString name, QImage *bitmap);
void Logger(QString logMsg);

#endif // UTILS_H
