/*
	For compiling this plugin: 
		gcc -shared -I$($(shell $(TARGET_GCC) -print-file-name=plugin))/include -fPIC -fno-rtti -O2 insertPayload_GCCPlugin.c -o InsertPayload_GCCPlugin.so

	For including it on your code:
		gcc -o <program> <program.c> -fplugin=./InsertPayload_GCCPlugin.so
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

#include "gcc-plugin.h"

/* Includes for use of cpp_define */
#include <c-family/c-common.h>
#include <c-family/c-pragma.h>

/* Includes for save_decoded_options and save_decoded_options_counts */
#include <opts.h>
#include <toplev.h>

#include <sys/metadata_payloads.h>

int plugin_is_GPL_compatible = 1;

#define MAX_ARGUMENTS 4 // Max amount of arguments to be passed to the plugin

#define MAX_PAYLOAD_BINARY_LENGTH_LINE 2048								 // Max size of the Payload_Binary.binary_data char array field
#define MAX_BINARY_PAYLOAD_VALUE_LEN (MAX_PAYLOAD_BINARY_LENGTH_LINE + 56)  // Max count of characters in Payload_Binary value
#define MAX_SCHED_PAYLOAD_VALUE_LEN  256									// Max count of characters in Payload_Sched value

#define METADATA_HDR_LENGTH 256   // Max count of char of Metadata_Hdr line
#define PAYLOAD_HDR_INTRO_LEN 128 // Max count of the Payload_Hdr line
#define PAYLOAD_HDR_VALUE_LEN 48  // Max count of each argument Payload_Hdr value

#define BIN_PAYLOAD 0
#define SCHED_PAYLOAD 1

#define FINAL_BUFFER_MAX (((MAX_ARGUMENTS + 1) * MAX_BINARY_PAYLOAD_VALUE_LEN * (PAYLOAD_HDR_VALUE_LEN)) + PAYLOAD_HDR_INTRO_LEN + METADATA_HDR_LENGTH)
											  // Max count of all metadata lines in user's source code to be replaced

/* STRUCTS */
struct payload_lines {
	char line[MAX_BINARY_PAYLOAD_VALUE_LEN]; 
	size_t size_payload;
};

struct payloadhdr_lines {
	char string_function_num[sizeof(int) + 1];
};

/* Private declarations */
uint8_t		*get_payload_from_file(struct plugin_name_args *, int, off_t *);
void		addPayload_line(char *, struct payload_lines*);
void		check_errno(bool, const char *);
void		check_error(bool, const char *);
void		create_addPayloadHdr_line(char *,  struct payloadhdr_lines*);
static void define_payload_as_macro(char *,char const *, char const *);
void		fill_payloads_line(char *, int, int, struct payload_lines []);
void		fill_payloadHeaders_line(char *, int, struct payloadhdr_lines []);
void		fill_payloadMetadataHdr_line(char *, int);
void 		get_value_from_sched_info(char *, char *);
void		merge_lines(char *, char *, char *, char *);
void		payload_Binary_fill(uint8_t *, char *, long);
void 		payload_Sched_fill(uint8_t *, char *);
void		print_args_info(struct plugin_name_args *, struct plugin_gcc_version *);
void		replacePAYLOAD(char *);

/**
 * @brief Plugin callback called during attribute registration
 */
static void register_payload (void *event_data, void *data) {

	struct plugin_name_args *plugin_info_regmet = (struct plugin_name_args *)data; // Cast to struct plugin_name_args *
   
    int bins = 0, scheds = 0;//num of each type of metadata payload

	struct payload_lines pay_bin_lines_array[plugin_info_regmet->argc] = {0}; // Array to contain each Payload line
	struct payload_lines pay_sched_lines_array[plugin_info_regmet->argc] = {0};
	struct payloadhdr_lines payhdr_lines_array[plugin_info_regmet->argc] = {0}; // Array to contain each Payload_Hdr line

	if (plugin_info_regmet->argc > MAX_ARGUMENTS) { 
		printf("ERROR: - La cantidad de argumentos pasado como archivo (%d) es mayor al valor permitido: %d\n",
			plugin_info_regmet->argc, MAX_ARGUMENTS);
		exit(EXIT_FAILURE);
	}

	for (int i = 0; i < plugin_info_regmet->argc; i++) {
		// Iterate over array containing lines extracted
		off_t file_size;
		uint8_t *payload_body = get_payload_from_file(plugin_info_regmet, i, &file_size);

		// ALLOCATE MEMORY FOR THE CHAR LINE OF THE PAYLOAD STRUCT
		char *payload_line;
		payload_line = (char *)xmalloc(MAX_BINARY_PAYLOAD_VALUE_LEN);
		check_error(payload_line == NULL, "ERROR: malloc payload_line\n");

		char *endptr;
		int function_num = strtol(plugin_info_regmet->argv[i].key, &endptr, 10);
		check_error(*endptr != '\0' && *endptr != '\n', "ERROR: Invalid input\n");

		switch (function_num) {
			case 1:
                payload_Binary_fill(payload_body, payload_line, file_size);
				addPayload_line(payload_line, &pay_bin_lines_array[bins++]);
                break;
            case 2:
                check_error(bins > 0, "ERROR: Sched Info must come before another payloads\n");
                check_error(scheds == 1, "ERROR: Cant have more than 1 Sched Info\n");
            
                payload_Sched_fill(payload_body, payload_line);
				addPayload_line(payload_line, &pay_sched_lines_array[scheds++]);
                break;
            default:
                check_error(true, "ERROR: Bad function num\n");
		}

		create_addPayloadHdr_line(plugin_info_regmet->argv[i].key, &payhdr_lines_array[i]);

		free(payload_body);
		free(payload_line);
	}

	// ALLOCATE MEMORY FOR ALL BINARY PAYLOAD  LINES
	size_t payloads_line_complete_size = ((plugin_info_regmet->argc) + 1) * MAX_BINARY_PAYLOAD_VALUE_LEN;

	char *payloads_line_complete;
	payloads_line_complete = (char *)xmalloc(payloads_line_complete_size);
	check_error(payloads_line_complete == NULL, "ERROR: malloc payloads_line_complete\n");

  	if (scheds > 0) {
		char *payloads_sched_line_complete;
		payloads_sched_line_complete = (char *)xmalloc(MAX_SCHED_PAYLOAD_VALUE_LEN);
		check_error(payloads_sched_line_complete == NULL, "ERROR: malloc payloads_sched_line_complete\n");

		fill_payloads_line(payloads_sched_line_complete, SCHED_PAYLOAD, scheds, pay_sched_lines_array);
		
		strcpy(payloads_line_complete, payloads_sched_line_complete);
		
		free(payloads_sched_line_complete);
	}
	
	if (bins > 0) {
		char *payloads_bin_line_complete;
		payloads_bin_line_complete = (char *)xmalloc(payloads_line_complete_size);
		check_error(payloads_bin_line_complete == NULL, "ERROR: malloc payloads_bin_line_complete\n");

		fill_payloads_line(payloads_bin_line_complete, BIN_PAYLOAD, bins, pay_bin_lines_array);

		if (scheds == 0)
			strcpy(payloads_line_complete, payloads_bin_line_complete);
		else
			strcat(payloads_line_complete, payloads_bin_line_complete);
	
		free(payloads_bin_line_complete);
	}
  
	// ALLOCATE MEMORY FOR ALL BINARY PAYLOAD HEADER LINES
	size_t payloadsHeader_line_complete_size = ((plugin_info_regmet->argc * PAYLOAD_HDR_VALUE_LEN) + PAYLOAD_HDR_INTRO_LEN); 

	char *payloadsHeader_line_complete;
	payloadsHeader_line_complete = (char *)xmalloc(payloadsHeader_line_complete_size);
	check_error(payloadsHeader_line_complete == NULL, "ERROR: malloc payloadsHeader_line_complete\n");

	fill_payloadHeaders_line(payloadsHeader_line_complete, plugin_info_regmet->argc, payhdr_lines_array);

	// ALLOCATE MEMORY FOR THE METADATA HEADER LINE
	size_t payloadsMetadataHdr_line_complete_size = METADATA_HDR_LENGTH; 

	char *payloadsMetadataHdr_line_complete;
	payloadsMetadataHdr_line_complete = (char *)xmalloc(payloadsMetadataHdr_line_complete_size);
	check_error(payloadsMetadataHdr_line_complete == NULL, "ERROR: malloc payloadsMetadataHdr_line_complete\n");

	fill_payloadMetadataHdr_line(payloadsMetadataHdr_line_complete, plugin_info_regmet->argc);

	// MERGE ALL LINES
	size_t macro_complete_size = payloads_line_complete_size + payloadsHeader_line_complete_size + payloadsMetadataHdr_line_complete_size + 1;

	char *macro_complete;
	macro_complete = (char *)xmalloc(macro_complete_size);
	check_error(macro_complete == NULL, "ERROR: malloc macro_complete\n");

	merge_lines(macro_complete, payloads_line_complete, payloadsHeader_line_complete, payloadsMetadataHdr_line_complete);

	free(payloadsHeader_line_complete);
	free(payloadsMetadataHdr_line_complete);

	/// REPLACE PAYLOAD INTO MACRO
	replacePAYLOAD(macro_complete);
}

void merge_lines(char *macro_complete, char *payloads_line_complete, char *payloadsHeader_line_complete, char *payloadsMetadataHdr_line_complete) {

	strcpy(macro_complete, payloadsMetadataHdr_line_complete);
	strcat(macro_complete, payloads_line_complete);
	strcat(macro_complete, payloadsHeader_line_complete);
}

void fill_payloadMetadataHdr_line(char *payloadsMetadataHdr_line_complete, int payloads_num) {
	char payloads_num_str[16];

	snprintf(payloads_num_str, 16, "%d", payloads_num);

	strcpy(payloadsMetadataHdr_line_complete, "static Metadata_Hdr metadata_header __attribute__((__used__, __section__(\".metadata\"))) = {");
	strcat(payloadsMetadataHdr_line_complete, payloads_num_str);
	strcat(payloadsMetadataHdr_line_complete, ", sizeof(Payload_Hdr)};");
}

void fill_payloadHeaders_line(char *payloadsHeader_line_complete, int argc, struct payloadhdr_lines payhdr_lines_array[]) {

	strcpy(payloadsHeader_line_complete, "static Payload_Hdr payload_headers[] __attribute__((__used__, __section__(\".metadata\"))) = {");

	char index_string[sizeof(int) + 1] = {0};

	for (int r = 0; r < sizeof(int) + 1; r++)
		index_string[r] = '\0';

	for (int m = 0, bins = 0, scheds = 0; m < argc; m++) {
		strcat(payloadsHeader_line_complete, "{");
		strcat(payloadsHeader_line_complete, payhdr_lines_array[m].string_function_num);

		if (strncmp(payhdr_lines_array[m].string_function_num, "1", 1) == 0) {
			strcat(payloadsHeader_line_complete, ", sizeof(payloads_bin[");
			sprintf(index_string, "%d", bins++);
			strcat(payloadsHeader_line_complete, index_string);
		} else if (strncmp(payhdr_lines_array[m].string_function_num, "2", 1) == 0) {
			strcat(payloadsHeader_line_complete, ", sizeof(payloads_sched[");
			sprintf(index_string, "%d", scheds++);
			strcat(payloadsHeader_line_complete, index_string);
		} else
            check_error(true, "ERROR: Bad function num\n");

		strcat(payloadsHeader_line_complete, "])}");

		if (m < argc - 1)
			strcat(payloadsHeader_line_complete, ",");
	}

	strcat(payloadsHeader_line_complete, "};");
}

void fill_payloads_line(char *payloads_line_complete, int payload_type, int argc, struct payload_lines pay_lines_array[]) {

	switch (payload_type) {
		case BIN_PAYLOAD:
			strcpy(payloads_line_complete, "static Payload_Binary payloads_bin[]");
			break;
		case SCHED_PAYLOAD:
			strcpy(payloads_line_complete, "static Payload_Sched payloads_sched[]");
			break;
		default:
			check_error(true, "ERROR: Bad function num");
	}

	strcat(payloads_line_complete, " __attribute__((__used__, __section__(\".metadata\"), __aligned__(8))) = {");

	for (int k = 0; k < argc; k++) {
		strcat(payloads_line_complete, pay_lines_array[k].line);
		
		if (k < argc - 1)
			strcat(payloads_line_complete, ",");
	}

	strcat(payloads_line_complete, "};");
}

void addPayload_line(char *line, struct payload_lines *payload_line_struct) {

	size_t line_length = strlen(line);

	check_error(line_length > MAX_BINARY_PAYLOAD_VALUE_LEN, "ERROR: line_length greater than addPayload_line()\n");

	void *ret_memcpy = NULL;
	ret_memcpy = memcpy(payload_line_struct->line, line, line_length);
	check_error(ret_memcpy == NULL, "ERROR: memcpy addPayload_line()\n");
}

void create_addPayloadHdr_line(char *funct_number, struct payloadhdr_lines *payloadHdr_line_struct) {

	size_t line_length_funct = strlen(funct_number);

	check_error(line_length_funct > sizeof(int), "ERROR: line_length_funct greater than create_addPayloadHdr_line()\n");

	void *ret_memcpy = NULL;
	ret_memcpy = memcpy(payloadHdr_line_struct->string_function_num, funct_number, line_length_funct);
	check_error(ret_memcpy == NULL, "ERROR: memcpy create_addPayloadHdr_line()\n");
}

void replacePAYLOAD(char *macro_complete) {
	//
	// Checks to see if user has previously defined __SEED_UINT__ and __SEED_ULINT__ macros
	// with -D argument. If they have been defined we will keep its value, otherwise
	// we generate a seed based on current time, we assign unsigned int and unsigned long int
	// presition for the corresponding macro
	//
	int payload_found = 0;

	// Buffer for holding the string defined as a macro //
	char macro_buff[FINAL_BUFFER_MAX];

	check_error(strlen(macro_complete) > FINAL_BUFFER_MAX, "ERROR: macro_complete is larger than FINAL_BUFFER_MAX - replacePAYLOAD()\n");

	if (!payload_found) 
		define_payload_as_macro(macro_buff, "__PAYLOAD__", macro_complete);

	free(macro_complete);
}

void payload_Binary_fill(uint8_t *binary_data, char *filled, long file_size) {
	char *payloads_type_Binary;
		
	payloads_type_Binary = (char *)xmalloc(500);
	check_error(payloads_type_Binary == NULL, "ERROR: malloc payloads_type_Binary\n");

	// Save binary data into an uint8_t array
	uint8_t payload[BINPAYLOAD_MAXSIZE + 1] = {0};

	memcpy(payload, binary_data, file_size);

	char canonical_hex_byte[3] = {0};
	canonical_hex_byte[2] = '\0';

	char container[2] = {0};
	container[1] = '\0';

	char file_size_string[sizeof(size_t) + 1] = {0};

	for (int r = 0; r < sizeof(size_t) + 1; r++)
		file_size_string[r] = '\0';

	char char_count_string[sizeof(long) + 1] = {0};

	for (int p = 0; p < sizeof(long) + 1; p++)
		char_count_string[p] = '\0';

	// Create string with data
	strcpy(payloads_type_Binary, "{");

	sprintf(file_size_string, "%ld", file_size); // First field: long bin_data_size

	strcat(payloads_type_Binary, file_size_string);
	strcat(payloads_type_Binary, ",");

	size_t char_count_prev = (file_size) * (2);

	if (char_count_prev > MAX_PAYLOAD_BINARY_LENGTH_LINE) {
		printf("Bytes como string %zu es mas grande que BINPAYLOAD_MAXSIZE - payload_Binary_fill\n", char_count_prev);
		exit(EXIT_FAILURE);
	}

	sprintf(char_count_string, "%zu", char_count_prev); // Second field: size_t charArray_bin_size;
	strcat(payloads_type_Binary, char_count_string);

	strcat(payloads_type_Binary, ",\""); // because char binary_data[] in Payload_Binary struct is a char array

	size_t char_count = 0;

	for (int i = 0; i < file_size; i++) {
		sprintf(canonical_hex_byte, "%02X", payload[i]);
		strcat(payloads_type_Binary, canonical_hex_byte);

		char_count = char_count + 2;
	}

	strcat(payloads_type_Binary, "\"}");

	strcpy(filled, payloads_type_Binary);

	free(payloads_type_Binary);
}

void payload_Sched_fill(uint8_t *sched_data, char *filled) {
    bool condition = sizeof(sched_data) > SCHEDPAYLOAD_STRING_MAXSIZE;
    check_error(condition, "ERROR: Sched Info size bigger than allowed (60 bytes)\n");
    
    // Data as string
    char *sched_info = (char *)sched_data;

    // Find Monopolize and ProcType
    const char *monopolize = "Monopolize: ";
    const char *procType = "ProcType: ";

    char *monopolizePos = strstr(sched_info, monopolize);
    char *procTypePos = strstr(sched_info, procType);
    condition = (monopolizePos == NULL || procTypePos == NULL);
    check_error(condition, "ERROR: Bad formatted Sched Info. It must be like this\n" \
                            "Sched Info:\n\tMonopolize: true|false\n" \
                            "\tProcType: \"LOWPERF|STANDARD|HIGHPERF|CRITICAL\"\n");
    
    char monopolizeValue[12];
    char procTypeValue[12];
	get_value_from_sched_info(monopolizePos, monopolizeValue);
	get_value_from_sched_info(procTypePos, procTypeValue);

    condition = (strncmp(monopolizeValue, "true", 4) != 0 && strncmp(monopolizeValue, "false", 5) != 0);
    check_error(condition, "ERROR: Monopolize must be true or false");

    condition = (strncmp(procTypeValue, "\"LOWPERF\"", 9) != 0 && strncmp(procTypeValue, "\"STANDARD\"", 10) != 0 &&
                  strncmp(procTypeValue, "\"HIGHPERF\"", 10) != 0 && strncmp(procTypeValue, "\"CRITICAL\"", 10) != 0);
    check_error(condition, "ERROR: ProcType must be LOWPERF, STANDARD, HIGHPERF, or CRITICAL between quotes (\")");

	if (strncmp(monopolizeValue, "true", 4) == 0)
		monopolizeValue[4] = '\0';
	else 
		monopolizeValue[5] = '\0';

	if (strncmp(procTypeValue, "\"LOWPERF\"", 9) == 0)
		procTypeValue[9] = '\0';
	else 
		procTypeValue[10] = '\0';

    // CREATE THE FULL STRING
    char payloads_type_Sched[SCHEDPAYLOAD_STRING_MAXSIZE];
    snprintf(payloads_type_Sched, sizeof(payloads_type_Sched), "{%s,%s}", monopolizeValue, procTypeValue);

    // Assign the line to the string passed by reference
    strcpy(filled, payloads_type_Sched);
}

void get_value_from_sched_info(char *line, char *dest) {
	char *token;

	token = strtok(line, ":");
	token = strtok(NULL, " ");

	strncpy(dest, token, 12);
}

/**
 * @brief Parses buffer with macro_name and defines a macro with such value
 * buffer: Pointer to buffer where the parsed macro will be stored
 * macro_name: name of the macro to be defined
 */
static void define_payload_as_macro(char *buffer, char const *macro_name, char const *payload_def) {
		
	sprintf(buffer, "%s=%s", macro_name, payload_def);

	printf("To be replaced: \n%s\n\n", buffer);

	cpp_define(parse_in, buffer);
}

uint8_t *get_payload_from_file(struct plugin_name_args *plugin_info_regmet, int index, off_t *file_size) {
	// Open the file mentioned in the plugin number i argument AS BINARY.
	int fdescriptor_input;
	FILE *rawbin_fp;
	struct stat stbuf;
	size_t bytes_read;

	// Open file and obtain the file descriptor
	fdescriptor_input = open(plugin_info_regmet->argv[index].value, O_RDONLY);
	check_errno(fdescriptor_input == -1, "ERROR: file descriptor de .bin");

	// Open file as a fstream using the file descriptor
	rawbin_fp = fdopen(fdescriptor_input, "rb");
	check_errno(rawbin_fp == NULL, "ERROR: apertura file .bin");

	// Obtain the stat structure of the open file using the file descriptor
	check_errno((fstat(fdescriptor_input, &stbuf) != 0) || (!S_ISREG(stbuf.st_mode)), "ERROR: stat del file .bin");
		
	// CALCULAR EL TAMANIO DEL ARCHIVO
	*file_size = stbuf.st_size;

	// Check file size is eq or less than MAX_PAYLOAD_BINARY_LENGTH_LINE
	check_error(*file_size > MAX_PAYLOAD_BINARY_LENGTH_LINE, "ERROR: al abrir archivo para leer - size mayor que MAX_PAYLOAD_BINARY_LENGTH_LINE - rawbin_fp\n");
	
	// Read input file as binary and save data into the array
	uint8_t *payload = (uint8_t*)xmalloc(MAX_PAYLOAD_BINARY_LENGTH_LINE);
	bytes_read = fread(payload, sizeof(uint8_t), (size_t)(*file_size), rawbin_fp);

	fclose(rawbin_fp);
	close(fdescriptor_input);

	check_error(bytes_read != (*file_size), "ERROR: al leer bytes del archivo input\n");

	return payload;
}

void check_errno(bool condition, const char *msg) {

	if (condition) {
		char strerrorbuf[128];

		if (strerror_r(errno, strerrorbuf, sizeof(strerrorbuf)) != 0) {
			strncpy(strerrorbuf, msg, sizeof(strerrorbuf));
			strerrorbuf[sizeof(strerrorbuf) - 1] = '\0';
			perror(strerrorbuf);
		}

		exit(EXIT_FAILURE);
	}
}

void check_error(bool condition, const char *msg) {

	if (condition) {
		perror(msg);
		exit(EXIT_FAILURE);
	}
}

/**
 * @brief Plugin initialization method
 * info: Holds plugin arg
 * ver: Version info of GCC
 */
int plugin_init(struct plugin_name_args *info, struct plugin_gcc_version *ver) {
		
	print_args_info(info, ver); // Print arguments passed to the plugin

	register_callback("InsertPayload_GCCPlugin", PLUGIN_START_UNIT, register_payload, info);

	return 0;
}

void print_args_info(struct plugin_name_args *plugin_info, struct plugin_gcc_version *version) {

	printf("Number of arguments of this plugin: %d\n", plugin_info->argc);

	for (int i = 0; i < plugin_info->argc; i++)
		printf("Arg [%d]: - Key: [%s] - Value: [%s]\n", i, plugin_info->argv[i].key, plugin_info->argv[i].value);
}