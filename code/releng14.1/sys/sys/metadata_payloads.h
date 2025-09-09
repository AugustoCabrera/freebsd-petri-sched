/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2022 Jose Cancinos (josemacanc@gmail.com) and 
 * 				      Julian Gonzalez (julian.agustin.gonzalez@mi.unc.edu.ar)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * [v1]
 */

#ifndef METADATA_PAYLOADS_H_
#define	METADATA_PAYLOADS_H_

#include <sys/types.h> //for bool type

#define PAYLOADS_TOTAL 2 			// IMPORTANT: update this number each time you add or remove a Payload_X struct
#define BINPAYLOAD_MAXSIZE 2048
#define SCHEDPAYLOAD_STRING_MAXSIZE 60 

#define PAYLOAD_BINARY_ARGS_COUNT 3			// For security

typedef struct {
	long bin_data_size; 						  // Size in bytes of the raw binary data
	size_t charArray_bin_size;					  // Count of characters of the binary_data as string
	char binary_data[BINPAYLOAD_MAXSIZE];         // Binary data as a string 
} Payload_Binary;

typedef struct {
	bool monopolize_cpu; //if true, the process wants to monopolize a core
	char sched_proc_type[SCHEDPAYLOAD_STRING_MAXSIZE]; //kind of process based on cpu load requirement (LOWPERF, STD, HIPERF or CRITICAL)
	char padding[3]; //make size 64 bytes       
} Payload_Sched;

typedef struct {
	int p_function_number;         // Function number associated to the payload                 
    size_t p_size;                 // Size of the payload
} Payload_Hdr;

typedef struct {
    int m_number_payloads;          // Total number of different payloads in the section (num of entries in the payload header table)               
    size_t ph_size;                 // Size of an entry in the payload header table
} Metadata_Hdr;

#endif /* METADATA_PAYLOADS_H_ */