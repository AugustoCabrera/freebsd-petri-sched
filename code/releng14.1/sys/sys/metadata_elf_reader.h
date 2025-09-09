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

#ifndef METADATA_ELF_READER_H
#define METADATA_ELF_READER_H

#include <sys/param.h>
#include <sys/imgact.h>
#include <sys/imgact_elf.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/vnode.h>
#include <sys/malloc.h>
#include <sys/syslog.h>
#include <sys/sysctl.h>
#include <sys/metadata_payloads.h>

#define SECCION_BUSCADA ".metadata"

// ********* FUNCTIONS ********* 
void*getMetadataSectionPayload(const Elf_Ehdr *, struct image_params *, int *, size_t *);
void copyMetadataToProc(void *, int, size_t, struct thread *);
void decodeMetadataSectionThread(struct thread *);
void decodeMetadataSectionProc(struct proc *, char *, bool *);

// ********* PAYLOAD FUNCTIONS ********* 
void payload_Binary_func(void *, Payload_Binary *);
void payload_Sched_func(void *, Payload_Sched *, bool *, char *);

#endif