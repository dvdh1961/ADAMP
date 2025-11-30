/* ADAMP_EMU  - A Windows ColecoVision emulator.
 * Copyright (C) 2025 DannyVdH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

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
