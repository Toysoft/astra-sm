/*
 * Astra SoftCAM module
 * Copyright (C) 2012, Andrey Dyldin <and@cesbo.com>
 *
 * This module is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This module is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this module.  If not, see <http://www.gnu.org/licenses/>.
 *
 * For more information, visit http://cesbo.com
 */

#include "../softcam.h"

/* to access to the cam module information */
struct module_data_s
{
    CAM_MODULE_BASE();
};

struct cas_data_s
{
    CAS_MODULE_BASE();

    uint8_t parity;
};

/* cas api */

static int videoguard_check_caid(uint16_t caid)
{
    return ((caid & 0xFF00) == 0x0900);
}

/* check Entitlement Message */
static const uint8_t * videoguard_check_em(cas_data_t *cas
                                           , const uint8_t *payload)
{
    const uint8_t em_type = payload[0];
    switch(em_type)
    {
        // ECM
        case 0x80:
        case 0x81:
        {
            if(em_type != cas->parity)
            {
                cas->parity = em_type;
                return payload;
            }
            break;
        }
        default:
        {
            const uint8_t emm_type = (payload[3] & 0xC0) >> 6;
            if(emm_type == 0)
            { // global
                return payload;
            }
            else if(emm_type == 1 || emm_type == 2)
            {
                const uint8_t serial_count = ((payload[3] >> 4) & 3) + 1;
                const uint8_t serial_len = (payload[3] & 0x80) ? 3: 4;
                for(uint8_t i = 0; i < serial_count; ++i)
                {
                    const uint8_t *serial = &payload[i * 4 + 4];
                    if(!memcmp(serial, &CAS2CAM(cas).ua[4], serial_len))
                        return payload;
                }
            }
            break;
        }
    }

    return NULL;
}

static int videoguard_check_keys(cas_data_t *cas, const uint8_t *keys)
{
    return 1; // if 0, then cas don't send any message to decrypt module
}

/*
 * CA descriptor (iso13818-1):
 * tag      :8 (must be 0x09)
 * length   :8
 * caid     :16
 * reserved :3
 * pid      :13
 * data     :length-4
 */

static uint16_t videoguard_check_desc(cas_data_t *cas, const uint8_t *desc)
{
    return CA_DESC_PID(desc);
}

CAS_MODULE(videoguard);
