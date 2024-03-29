/*
 * Astra Module: SoftCAM
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2014, Andrey Dyldin <and@cesbo.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "../module_cam.h"

struct module_data_t
{
    MODULE_CAS_DATA();
};

static bool cas_check_em(module_data_t *mod, mpegts_psi_t *em)
{
    const uint8_t em_type = em->buffer[0];
    switch(em_type)
    {
        // ECM
        case 0x80:
        case 0x81:
        {
            return true;
        }
        // EMM
        case 0x82:
        {
            if(!memcmp(&em->buffer[3], &mod->__cas.decrypt->cam->ua[2], 6))
                return true;
            break;
        }
        case 0x84:
        {
            asc_list_for(mod->__cas.decrypt->cam->prov_list)
            {
                uint8_t *sa = (uint8_t *)asc_list_data(mod->__cas.decrypt->cam->prov_list);
                if(!memcmp(&em->buffer[5], &sa[5], 3))
                    return true;
            }
            break;
        }
        default:
            break;
    }

    return false;
}

static bool cas_check_keys(module_data_t *mod, const uint8_t *keys)
{
    __uarg(mod);
    __uarg(keys);
    return true;
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

static bool cas_check_descriptor(module_data_t *mod, const uint8_t *desc)
{
    __uarg(mod);
    __uarg(desc);

#if 0
    const int length = desc[1] - 4;
    if(length < 2)
        return false;

    asc_list_for(mod->__cas.decrypt->cam->prov_list)
    {
        uint8_t *prov = asc_list_data(mod->__cas.decrypt->cam->prov_list);
        if((prov[1] == desc[6]) && (prov[2] == desc[7]))
            return true;
    }

    return false;
#endif

    return true;
}

static bool cas_check_caid(uint16_t caid)
{
    return (caid == 0x0100);
}

MODULE_CAS(mediaguard)
