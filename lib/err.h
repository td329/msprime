/*
** Copyright (C) 2014 Jerome Kelleher <jerome.kelleher@well.ox.ac.uk>
**
** This file is part of msprime.
**
** msprime is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** msprime is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with msprime.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef __ERR_H__
#define __ERR_H__

/* 
 * raise a compiler warning if a potentially error raising function's return
 * value is not used.
 */
#ifdef __GNUC__
    #define WARN_UNUSED __attribute__ ((warn_unused_result))
#else
    #define WARN_UNUSED
#endif


#define MSP_ERR_GENERIC -1
#define MSP_ERR_NO_MEMORY -2
#define MSP_ERR_IO -3
#define MSP_ERR_FILE_FORMAT -4
#define MSP_ERR_FILE_VERSION -5
#define MSP_ERR_BAD_MODE -6
#define MSP_ERR_TOO_MANY_SEG_SITES -7

#endif /*__ERR_H__*/
