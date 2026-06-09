/*
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
 *
 * adampcb.cpp
 *
 * Based on   PCB emulation (C) Marat Fayzullin 1994-2021
 *
*/
#include <QDebug>

#include "adnet_core.h"
#include "CORE/cv.h"

#include <algorithm>
#include <cstring>
#include <stdint.h>
#include <string>

#define RAM(A)  (RAM_Memory[A])

int g_prn_line_counter = 0;
bool g_prn_in_wp = false;

//--------------------------------------------------------------------------------------
#ifdef __cplusplus
extern "C" {
void adam_printer_chunk(const uint8_t* data, int len);
}
#endif
//--------------------------------------------------------------------------------------
extern "C" void adamnet_host_prn_write_ascii(const char* s)
{
    if (!s) return;

    const uint8_t* p = reinterpret_cast<const uint8_t*>(s);

    while (*p) {
        int n = 0;
        const uint8_t* start = p;
        while (p[n] && n < 512) ++n;
        adam_printer_chunk(start, n);
        p += n;
    }
}
//--------------------------------------------------------------------------------------
// --- AdamNet printer sink: UI kan zich hierop abonneren ---
extern "C" {
typedef void (*AdamPrinterSink)(const char* data, int len);
static AdamPrinterSink g_printer_sink = nullptr;
void adam_printer_set_sink(AdamPrinterSink sink) { g_printer_sink = sink; }
}
void UpdatePRN(byte Dev, int V)
{
    int N;
    word A;
    static char g_row_buf[121];
    static int g_cur_x = 0;
    static bool g_row_init = false;

    if (!g_row_init) {
        memset(g_row_buf, ' ', 120);
        g_row_buf[120] = '\0';
        g_row_init = true;
    }

    switch(V)
    {
    case CMD_STATUS:
    case CMD_SOFT_RESET:
        // Rapporteer altijd dat het apparaat 'Gezond' is (0x0001)
        ReportDevice(Dev, 0x0001, 0);
        // Forceer de status direct op Idle
        SetDCB(Dev, DCB_CMD_STAT, 0x80);
        break;

    case CMD_READ:
        SetDCB(Dev,DCB_CMD_STAT,RSP_ACK+0x0B);
        break;

    case CMD_WRITE:
    {
        SetDCB(Dev, DCB_CMD_STAT, 0x00);
        A = GetDCBBase(Dev);
        N = GetDCBLen(Dev);

        if (N > 0) {
            for (int j = 0; j < N; ++j, A = (A + 1) & 0xFFFF) {
                uint8_t c = RAM_Memory[A];

                if (c == 13 || c == 10 || c == 11) {
                    std::string line(g_row_buf, 120);

                    size_t first = line.find_first_not_of(' ');
                    size_t last = line.find_last_not_of(' ');

                    if (first != std::string::npos) {
                        std::string processed = line.substr(first, (last - first + 1));

                        // Gebruik de globale teller voor de spiegel-logica
                        if (g_prn_line_counter % 2 != 0) {
                            qDebug() << "[ADAMNET] Lijn teller:" << g_prn_line_counter ;
                            std::reverse(processed.begin(), processed.end());
                        }
                        else
                            qDebug() << "[ADAMNET] Lijn teller:" << g_prn_line_counter ;

                        adam_printer_chunk((uint8_t*)processed.c_str(), processed.length());
                        uint8_t nl = '\n';
                        adam_printer_chunk(&nl, 1);

                        // bij wordprocessor mode
                        if (g_prn_in_wp)
                        {
                            g_prn_line_counter++;
                        }
                    }

                    memset(g_row_buf, ' ', 120);
                    g_cur_x = 0;
                }
                else if (c == 8) {
                    if (g_cur_x > 0) g_cur_x--;
                }
                else if (c >= 32) {
                    if (g_cur_x < 120) {
                        g_row_buf[g_cur_x] = (char)c;
                        g_cur_x++;
                    }
                }
            }
        }

        SetDCB(Dev, DCB_CMD_STAT, RSP_ACK + 0x0B);
    }
    break;

    default:
        //SetDCB(Dev, DCB_CMD_STAT, RSP_STATUS);
        // Voor alle andere commando's: zeg gewoon 'OK'
        SetDCB(Dev, DCB_CMD_STAT, 0x80);
        break;
    }
}
