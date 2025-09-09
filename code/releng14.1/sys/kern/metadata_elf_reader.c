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

#include <sys/cdefs.h>
#ifdef __FreeBSD__
__FBSDID("$FreeBSD$");
#endif

#include <sys/metadata_elf_reader.h>

SYSCTL_STRING(_kern, OID_AUTO, metadata, CTLFLAG_RD, "ELF_METADATA_ACTIVE", 0,
    "ELF Metadata section is active");

// Memory space for buffers and other retrieved data
static MALLOC_DEFINE(M_ELFHEADER, "Elf Header", "Memory for the elf header");  // @1: type (must start with 'M_') @2: shortdesc @3: longdesc 
static MALLOC_DEFINE(M_BUFFER, "buffer", "Memory for buffer");                 // " " of type M_BUFFER
static MALLOC_DEFINE(M_SECTION, "section data", "Memory for section data");              // " " of type M_SECTION

void 
payload_Binary_func(void* payload_addr, Payload_Binary* payloadBinary_decod)
{
	memcpy(payloadBinary_decod, payload_addr, sizeof(Payload_Binary));	

	char char_bin_data[BINPAYLOAD_MAXSIZE] = {0};		// Create a char array to save the string (bin data as char) in it
	memcpy(char_bin_data,  payloadBinary_decod->binary_data, BINPAYLOAD_MAXSIZE);	

	char bytes_as_char[BINPAYLOAD_MAXSIZE][3] = {{0}};
	for (int i = 0; i < payloadBinary_decod->bin_data_size; i = i + 1) {
		bytes_as_char[i][0] = char_bin_data[2*i];
		bytes_as_char[i][1] = char_bin_data[(2*i) + 1];
		bytes_as_char[i][2] = '\0';
	}

	uint8_t bin_data[BINPAYLOAD_MAXSIZE] = {0};
	int tmp_cast_value = 0;
	for (int m = 0; m < payloadBinary_decod->bin_data_size; m++) {
		sscanf(bytes_as_char[m], "%x", &tmp_cast_value);
		bin_data[m] = (uint8_t) tmp_cast_value;
	}
}

typedef void (*payload_Binary_func_wrapper)(void* payload_addr, Payload_Binary* payloadBinary_decod);

void 
payload_Sched_func(void* payload_addr, Payload_Sched* payloadSched_decod, bool *monopolize, char *proc_type)
{
	memcpy(payloadSched_decod, payload_addr, sizeof(Payload_Sched));	

	*monopolize = payloadSched_decod->monopolize_cpu;
	memcpy(proc_type, payloadSched_decod->sched_proc_type, SCHEDPAYLOAD_STRING_MAXSIZE);	
}

typedef void (*payload_Sched_func_wrapper)(void* payload_addr, Payload_Sched* payloadSched_decod, bool *monopolize, char *proc_type);

typedef void (*payload_func)(void); // array of function pointers to store each function matching a different payload struct. Definition and instantiation 
									// in metadata_elf_reader.c

payload_func payload_functions[PAYLOADS_TOTAL + 1] = { NULL, (payload_func) payload_Binary_func, (payload_func) payload_Sched_func };

void * 
getMetadataSectionPayload(const Elf_Ehdr *hdr, struct image_params *imgp, int* return_flag, size_t* payload_size)
{
	
 	struct thread *td;
    Elf_Shdr *sectionheader_table;
	int flags_mallocELFRead = M_WAITOK | M_ZERO;
	int error = 0;
	
	// Asign current thread to td struct
    td = curthread; 

	// 1) Memory allocation to store ELF section header table
	// Use M_TEMP allocated memory
	sectionheader_table = malloc((hdr->e_shentsize)*(hdr->e_shnum), M_ELFHEADER, flags_mallocELFRead );
	if (sectionheader_table == NULL) {
	//	log(LOG_INFO, "\t\t1) // getMetadataSectionPayload() // LectorELF // -- ERROR IN MALLOC - sectionheader_table\n");
		goto fail;
	}

    // 2) Read ELF section header table
	// Check if VP is locked
	int prev_locked_status = 0;
	prev_locked_status = VOP_ISLOCKED(imgp->vp);	// 0 -> UNLOCKED, else: previously locked.
	//log(LOG_INFO, "\t\t1) // getMetadataSectionPayload() // LectorELF // 2.1) Is imgp->vp locked?: %d\n", prev_locked_status);

	// If not locked, then lock to serialize the access to the file system
	if (prev_locked_status == 0) { // If 0, then UNLOCKED
		vn_lock(imgp->vp, LK_EXCLUSIVE | LK_RETRY);
		//log(LOG_INFO, "\t\t1) // getMetadataSectionPayload() // LectorELF // 2.1) Lock applied - New lock state: %d\n", VOP_ISLOCKED(imgp->vp));
	}

	// Read ELF file to sectionheader_table
	error = vn_rdwr(UIO_READ, imgp->vp, sectionheader_table, (hdr->e_shentsize)*(hdr->e_shnum), hdr->e_shoff,
		UIO_SYSSPACE, IO_NODELOCKED, td->td_ucred, NOCRED, NULL, td);
	if (error) {
	//	log(LOG_INFO, "\t\t1) // getMetadataSectionPayload() // LectorELF // -- ERROR vn_rdwr section header table %d\n", error);
        goto fail1;
    }

	// 3) Elements needed to iterate over each section of the ELF file.
  	char* buff;

	buff = malloc(sectionheader_table[hdr->e_shstrndx].sh_size, M_BUFFER, flags_mallocELFRead);
  	if (buff == NULL) {
		//log(LOG_INFO, "\t\t1) // getMetadataSectionPayload() // LectorELF // -- ERROR IN MALLOC - buff\n");
    	goto fail1;
  	}

    // 4) Read buff
	error = vn_rdwr(UIO_READ, imgp->vp, buff, sectionheader_table[hdr->e_shstrndx].sh_size, sectionheader_table[hdr->e_shstrndx].sh_offset,
		UIO_SYSSPACE, IO_NODELOCKED, td->td_ucred, NOCRED, NULL, td);
	if (error) {
		//log(LOG_INFO, "\t\t1) // getMetadataSectionPayload() // LectorELF // -- ERROR vn_rdwr buff %d\n", error);
		goto fail2;
	}

	// 5) Iterate over each section of the section header table
	int mm = 0;

	for (mm = 0; mm < hdr->e_shnum; mm++) {  
		if (strcmp(SECCION_BUSCADA, (buff + sectionheader_table[mm].sh_name)) == 0) {
			//log(LOG_INFO, "\t\t1) // getMetadataSectionPayload() // LectorELF // -- Seccion encontrada --\n");
			//log(LOG_INFO, "\t\t1) // getMetadataSectionPayload() // LectorELF // -- Seccion %s encontrada en offset 0x%lx y de tamanio %lu bytes--\n", 
			//		SECCION_BUSCADA, (off_t) sectionheader_table[mm].sh_offset, (size_t) sectionheader_table[mm].sh_size);			
			break;
		}
	}

    // 6) Check if the desired section (.metadata) was found in earlier iteration
	void* sectiondata;

	if (mm < hdr->e_shnum) {
		// 7) Memory allocation to store data of the section
		sectiondata = malloc(sectionheader_table[mm].sh_size, M_SECTION, flags_mallocELFRead);
		if (sectiondata == NULL) {
			//log(LOG_INFO, "\t\t1) // getMetadataSectionPayload() // LectorELF // -- ERROR IN MALLOC - sectiondata\n");
			goto fail2;
		}

		// 8) Init variables to section data
		off_t sectiondata_offset = 0;
		size_t sectiondata_size = 0;

		sectiondata_offset = sectionheader_table[mm].sh_offset;    // Points to the start of the desired section
		sectiondata_size = sectionheader_table[mm].sh_size;      // Size of the desired section

		// 9) Read section data
		error = vn_rdwr(UIO_READ, imgp->vp, sectiondata, sectiondata_size, 
						sectiondata_offset, 
						UIO_SYSSPACE, IO_NODELOCKED, td->td_ucred, NOCRED, NULL, td);

		if (error) {
			//log(LOG_INFO, "\t\t1) // getMetadataSectionPayload() // LectorELF // -- ERROR vn_rdwr sectiondata %d\n", error);
			goto fail3;
		}

        // 10) Return sectiondata pointer
		*payload_size = sectiondata_size;
		*return_flag = 1;
        goto ret;
	} else {
        // Section was not found, return NULL
		//log(LOG_INFO, "\t\t1) // getMetadataSectionPayload() // LectorELF // -- *** SECTION NOT FOUND ***\n");
        *return_flag = 2;
		sectiondata = NULL;

        goto ret;
	}

	// Free allocated memory
fail3:
  	free(sectiondata, M_SECTION);
fail2:
    free(buff, M_BUFFER);    
fail1:
	free(sectionheader_table, M_ELFHEADER);
	// Unlock if the vnode was locked in this function. 
	if (prev_locked_status == 0) 	// 0 = UNLOCKED before this function.
		VOP_UNLOCK(imgp->vp);	// Lock applied in this function, then unlock to return to prev lock state
fail:
	*return_flag = -1;
    return NULL;
ret:
	// Unlock if the vnode was locked in this function. 
	if (prev_locked_status == 0) 	// 0 = UNLOCKED before this function.
		VOP_UNLOCK(imgp->vp);	// Lock applied in this function, then unlock to return to prev lock state

	// Free allocated memory 
	free(buff, M_BUFFER);
	free(sectionheader_table, M_ELFHEADER);
	
    return sectiondata;
}

void 
copyMetadataToProc(void *metadata_addr, int returned_flag, size_t payload_size, struct thread *td)
{
	/* * METADATA: proc struct metadata pointer points to returned address */
	td->td_proc->p_metadata_addr = metadata_addr;

	/* * METADATA: Assign metadata returned flag to proc struct */
	td->td_proc->p_metadata_section_flag = returned_flag;

	/* * METADATA: Assign metadata returned size to proc struct */
	td->td_proc->p_metadata_size = payload_size;
}

void 
decodeMetadataSectionThread(struct thread *td)
{
	/* 1) Get Metadata_Hdr address using:
		- Start addr of Metadata_Hdr  = td->td_proc->p_metadata_addr
		- End addr of Metadata_Hdr  = td->td_proc->p_metadata_addr + sizeof(Metadata_Hdr) 
	*/
	void* metadata_hdr_start_addr = (char *) (td->td_proc->p_metadata_addr);
	void* metadata_hdr_end_addr = (char *) (metadata_hdr_start_addr) + sizeof(Metadata_Hdr);
	
	Metadata_Hdr metadata_header_decod;
	memcpy(&metadata_header_decod, metadata_hdr_start_addr, sizeof(Metadata_Hdr));

	//log(LOG_INFO, "\t\t4) // decodeMetadataSection() // LectorELF // Metadata_Hdr = metadata_header_decod //// m_number_payloads: %d - ph_size: %lu\n", 
	//		metadata_header_decod.m_number_payloads, metadata_header_decod.ph_size);

	/* 2) Get Payload_Hdr table address:
		- Start addr of Payload_Hdr table  = td->td_proc->p_metadata_addr + td->td_proc->p_metadata_size - (Metadata_Hdr.m_number_payloads * Metadata_Hdr.ph_size)
		- End addr of Payload_Hdr table = td->td_proc->p_metadata_addr + td->td_proc->p_metadata_size
	*/
	size_t payload_headers_table_size = (metadata_header_decod.m_number_payloads) * (metadata_header_decod.ph_size);
	void* payload_hdrs_table_end_addr = (char *) (td->td_proc->p_metadata_addr) + (td->td_proc->p_metadata_size);
	void* payload_hdrs_table_start_addr = (char *) (payload_hdrs_table_end_addr) - payload_headers_table_size;

	Payload_Hdr payload_header_decod;
	void* payhdr_addr = NULL;

	void* payload_addr = (char *) (metadata_hdr_end_addr);

	for (int k = 0; k < metadata_header_decod.m_number_payloads; k++) {
		payhdr_addr = &(((Payload_Hdr *)payload_hdrs_table_start_addr)[k]);
		
		memcpy(&payload_header_decod, payhdr_addr, sizeof(Payload_Hdr));

		//log(LOG_INFO, "\t\t4) // decodeMetadataSection() // LectorELF // Payload_Hdr = payload_header_decod[%d] //// address: %p - p_function_number: %d - ph_size: %lu\n", 
		//	k, payhdr_addr, payload_header_decod.p_function_number, payload_header_decod.p_size);

		/* 3) Get each Payload address:
			- Start addr of first Payload  = Start of metadata section
			- End addr of last Payload  = Start addr of Payload_Hdr
		*/		
		switch (payload_header_decod.p_function_number) {
			case 1:
			{
				Payload_Binary payloadBinary_decod;
				((payload_Binary_func_wrapper)(payload_functions[payload_header_decod.p_function_number]))(payload_addr, &payloadBinary_decod);
				break;				
			}
			case 2:
			{
				Payload_Sched payloadSched_decod;
				bool monopolize; 
				char proc_type[SCHEDPAYLOAD_STRING_MAXSIZE];
				((payload_Sched_func_wrapper)(payload_functions[payload_header_decod.p_function_number]))(payload_addr, &payloadSched_decod, &monopolize, proc_type);
				break;				
			}
			default:
			{
				//log(LOG_INFO, "\t\t4) // decodeMetadataSection() // LectorELF // payload_header_decod[%d] has p_function_number: %d NOT SUPPORTED\n", 
				//	k, payload_header_decod.p_function_number);
				break;
			}
		}

		payload_addr = (char *) (payload_addr) + payload_header_decod.p_size;
	}
}

/**
 * this function decodes the metadata inserted
 * into the executable of the process passed as an argument
 * it can decode (for now) binary and sched info payloads
 * and only saves the results of decoding sched info
 * as its the only payload that is currently used 
*/
void 
decodeMetadataSectionProc(struct proc *proc, char *proc_type, bool *monopolize)
{
	/* 1) Get Metadata_Hdr address using:
		- Start addr of Metadata_Hdr  = proc->p_metadata_addr
		- End addr of Metadata_Hdr  = proc->p_metadata_addr + sizeof(Metadata_Hdr) 
	*/
	void* metadata_hdr_start_addr = (char *) (proc->p_metadata_addr);
	void* metadata_hdr_end_addr = (char *) (metadata_hdr_start_addr) + sizeof(Metadata_Hdr);
	
	Metadata_Hdr metadata_header_decod;
	memcpy(&metadata_header_decod, metadata_hdr_start_addr, sizeof(Metadata_Hdr));

	/* 2) Get Payload_Hdr table address:
		- Start addr of Payload_Hdr table  = proc->p_metadata_addr + proc->p_metadata_size - (Metadata_Hdr.m_number_payloads * Metadata_Hdr.ph_size)
		- End addr of Payload_Hdr table = proc->p_metadata_addr + proc->p_metadata_size
	*/
	size_t payload_headers_table_size = (metadata_header_decod.m_number_payloads) * (metadata_header_decod.ph_size);
	void* payload_hdrs_table_end_addr = (char *) (proc->p_metadata_addr) + (proc->p_metadata_size);
	void* payload_hdrs_table_start_addr = (char *) (payload_hdrs_table_end_addr) - payload_headers_table_size;

	Payload_Hdr payload_header_decod;
	void* payhdr_addr = NULL;

	void* payload_addr = (char *) (metadata_hdr_end_addr);

	for (int k = 0; k < metadata_header_decod.m_number_payloads; k++) {
		payhdr_addr = &(((Payload_Hdr *)payload_hdrs_table_start_addr)[k]);
		
		memcpy(&payload_header_decod, payhdr_addr, sizeof(Payload_Hdr));

		/* 3) Get each Payload address:
			- Start addr of first Payload  = Start of metadata section
			- End addr of last Payload  = Start addr of Payload_Hdr
		*/		
		switch (payload_header_decod.p_function_number) {
			case 1:
			{
				Payload_Binary payloadBinary_decod;
				((payload_Binary_func_wrapper)(payload_functions[payload_header_decod.p_function_number]))(payload_addr, &payloadBinary_decod);
				break;				
			}
			case 2:
			{
				Payload_Sched payloadSched_decod;
				((payload_Sched_func_wrapper)(payload_functions[payload_header_decod.p_function_number]))(payload_addr, &payloadSched_decod, monopolize, proc_type);
				break;				
			}
			default:
			{
				break;
			}
		}
		
		payload_addr = (char *) (payload_addr) + payload_header_decod.p_size;
	}
}