#include "clang/Driver/Options.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/FrontendOptions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Rewrite/Core/Rewriter.h"

// CPP header files and libs
#include <iostream>
#include <fstream>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <iomanip>
#include <vector>
#include <string>
#include <sstream>

#include <sys/metadata_payloads.h>

// Spaces
using namespace std;
using namespace clang;
using namespace llvm;

// Limits
#define MAX_ARGUMENTS 4     // Max amount of arguments to be passed to the plugin

// Structs
struct payload_lines{
    string line;
    size_t size_payload;
};

struct payloadhdr_lines{
  string string_function_num;
};

// Global variables
Rewriter rewriter;
vector<string> global_args;

// Function declarations
string  getMetadataLines(vector<string>);
uint8_t *get_payload_from_file(vector<string>, int, off_t *);
void    fill_payload_line(string &, string, int, struct payload_lines*);
void    fill_payloadHeaders_line(string &, int, struct payloadhdr_lines*);
void    fill_payloadMetadataHdr_line(string &, string);
void    merge_lines(string &, string, string, string);
void    payload_Binary_fill(uint8_t*, string &, long);
void    payload_Sched_fill(uint8_t*, string &);
void    check_error(bool, string);

// classes
class RenameVisitor : public RecursiveASTVisitor<RenameVisitor> {
private:
    ASTContext *astContext; // used for getting additional AST info
 
public:
    explicit RenameVisitor(CompilerInstance *CI)
        : astContext(&(CI->getASTContext())) // initialize private members
    {
        rewriter.setSourceMgr(astContext->getSourceManager(),
            astContext->getLangOpts());
    }

    virtual bool VisitVarDecl(VarDecl *vardecl) {

        string VarDeclName = vardecl->getName().str();
        if (VarDeclName == "__PAYLOAD__") {
            // Check if no arguments were passed
            if (global_args.empty() == true) {
                cout << "No arguments were passed. __PAYLOAD__ is not replaced";
                return false;   // Return false to abort the tree traversing 
            }

            // Obtain the whole line to replace
            string complete_metadata_replace = getMetadataLines(global_args);

            // Get the location in source code of __PAYLOAD__
            SourceLocation location = vardecl->getLocation();
            
            // Replace __PAYLOAD__ with the full processed metadata lines
            rewriter.ReplaceText(location.getLocWithOffset(0), VarDeclName.length(), complete_metadata_replace);
            
            return false;       // Return false to abort the entire traversal of the tree
        }

        return true;
    }    
};

class RenameASTConsumer : public ASTConsumer {
private:
    RenameVisitor *visitor; // doesn't have to be private

    // Function to get the base name of the file provided by path
    string basename(std::string path) {
        return std::string( std::find_if (path.rbegin(), path.rend(), MatchPathSeparator()).base(), path.end());
    }

    // Used by std::find_if
    struct MatchPathSeparator
    {
        bool operator()(char ch) const {
            return ch == '/';
        }
    };
 
public:
    explicit RenameASTConsumer(CompilerInstance *CI)
        // Initialize the visitor. Calls RenameVisitor
        : visitor(new RenameVisitor(CI)) 
        { }
 
    virtual void HandleTranslationUnit(ASTContext &Context) {
        // Traverse across the AST tree
        visitor->TraverseDecl(Context.getTranslationUnitDecl());

        // Create an output file to write the updated code
        FileID id = rewriter.getSourceMgr().getMainFileID();
        string filename = "/tmp/metadata_" + basename(rewriter.getSourceMgr().getFilename(rewriter.getSourceMgr().getLocForStartOfFile(id)).str());
        std::error_code OutErrorInfo;
        std::error_code ok;
        llvm::raw_fd_ostream outFile(llvm::StringRef(filename),
            OutErrorInfo, llvm::sys::fs::OF_None);
        if (OutErrorInfo == ok) {
            const RewriteBuffer *RewriteBuf = rewriter.getRewriteBufferFor(id);
            outFile << std::string(RewriteBuf->begin(), RewriteBuf->end());
            errs() << "Output file created - " << filename << "\n";
        } else 
            llvm::errs() << "Could not create file\n";
    }
};

// Create the AST Consumer
class PluginRenameAction : public PluginASTAction {
protected:
    bool ParseArgs(const CompilerInstance &CI, const vector<string> &args) {
        // Copy the args vector to the global_args vector 
        global_args = args;     

        return true;
    }

    unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, StringRef file) {
        // Create a single ASTConsumer. Calls RenameAstConsumer
        return make_unique<RenameASTConsumer>(&CI);
    }
};

string getMetadataLines(vector<string> args) {

    check_error(args.size() > MAX_ARGUMENTS, "ERROR - La cantidad de argumentos pasado como archivo " + to_string(args.size()) +
                                                " es mayor al valor permitido: " + to_string(MAX_ARGUMENTS) + "\n");    
    
    int bins = 0, scheds = 0;//num of each type of metadata payload

    payload_lines* pay_bin_lines_array = new payload_lines[args.size()]();
    payload_lines* pay_sched_lines_array = new payload_lines[args.size()]();
    payloadhdr_lines* payhdr_lines_array = new payloadhdr_lines[args.size()]();

    for (int z = 0; z < args.size(); z++) {
        off_t file_size;

        uint8_t* payload_body = get_payload_from_file(args, z, &file_size);
        check_error(payload_body == NULL, "error malloc payload_body\n");

        // Create string for the payload_Binary line
        string payload_line;

        // detect function num. Binary: 1 | Sched Info: 2
        int function_num = strncmp((const char *)payload_body, "Sched Info:\n", 12) != 0 ?
                            1 :
                            2;                        

        // Call fill function
        // Add line to payload_lines array 
        switch (function_num) {
            case 1:
                payload_Binary_fill(payload_body, payload_line, file_size);
                pay_bin_lines_array[bins++].line = payload_line;     // CHANGE INDEX TO ITERATOR
                break;
            case 2:
                check_error(bins > 0, "ERROR: Sched Info must come before another payloads\n");
                check_error(scheds == 1, "ERROR: Cant have more than 1 Sched Info\n");
            
                payload_Sched_fill(payload_body, payload_line);
                pay_sched_lines_array[scheds++].line = payload_line;     // CHANGE INDEX TO ITERATOR
                break;
            default:
                check_error(true, "ERROR: Bad function num\n");
        }
        //  Add function number to payloadhdr_lines array
        payhdr_lines_array[z].string_function_num = to_string(function_num);
    }

    // Iterate and obtain full payload lines
    string payloads_line_complete;
    if (scheds > 0)
        fill_payload_line(payloads_line_complete, "static Payload_Sched payloads_sched[]", scheds, pay_sched_lines_array);
    if (bins > 0)
        fill_payload_line(payloads_line_complete, "static Payload_Binary payloads_bin[]", bins, pay_bin_lines_array);

    // Iterate and obtain full payload_Hdr lines
    string payloadsHeader_line_complete;
    fill_payloadHeaders_line(payloadsHeader_line_complete, args.size(), payhdr_lines_array);

    // Obtain full metadata_Hdr line
    string payloadsMetadataHdr_line_complete;
    fill_payloadMetadataHdr_line(payloadsMetadataHdr_line_complete, to_string(args.size()));

    // Merge all lines
    string macro_complete;
    merge_lines(macro_complete, payloads_line_complete, payloadsHeader_line_complete, payloadsMetadataHdr_line_complete);        
    cout << "metadata insertion:\n" << macro_complete << "\n";

    return macro_complete;
}

void merge_lines(string & macro_complete, string payloads_line_complete, string payloadsHeader_line_complete, string payloadsMetadataHdr_line_complete) {

    macro_complete.append(payloadsMetadataHdr_line_complete); 
    macro_complete.append(payloads_line_complete);
    macro_complete.append(payloadsHeader_line_complete);
}

void fill_payloadMetadataHdr_line(string & payloadsMetadataHdr_line_complete, string argc_string) {

    payloadsMetadataHdr_line_complete.append("static Metadata_Hdr metadata_header __attribute__((__used__, __section__(\".metadata\"))) = {");
    payloadsMetadataHdr_line_complete.append(argc_string);
    payloadsMetadataHdr_line_complete.append(", sizeof(Payload_Hdr)};\n");
}

void fill_payloadHeaders_line(string & payloadsHeader_line_complete, int argc, struct payloadhdr_lines* payhdr_lines_array) {

    payloadsHeader_line_complete.append("static Payload_Hdr payload_headers[] __attribute__((__used__, __section__(\".metadata\"))) = {");

    for (int m = 0, bins = 0, scheds = 0; m < argc; m++) {
        payloadsHeader_line_complete.append("{");
        payloadsHeader_line_complete.append(payhdr_lines_array[m].string_function_num);

        if (payhdr_lines_array[m].string_function_num == "1") {
            payloadsHeader_line_complete.append(", sizeof(payloads_bin[");
            payloadsHeader_line_complete.append(to_string(bins++)); //index of array
        } else if (payhdr_lines_array[m].string_function_num == "2") {
            payloadsHeader_line_complete.append(", sizeof(payloads_sched[");
            payloadsHeader_line_complete.append(to_string(scheds++)); //index of array
        } else 
            check_error(true, "ERROR: Bad function num\n");

        payloadsHeader_line_complete.append("])}");
    
        if (m < argc - 1)
            payloadsHeader_line_complete.append(",");
    }

    payloadsHeader_line_complete.append("}");
}

void fill_payload_line(string & payloads_line_complete, string payload_type, int argc, struct payload_lines* pay_lines_array) {

    payloads_line_complete.append(payload_type);
    payloads_line_complete.append(" __attribute__((__used__, __section__(\".metadata\"), __aligned__(8))) = {");
    for (int k = 0; k < argc; k++) {
        payloads_line_complete.append(pay_lines_array[k].line);

        if (k < argc - 1) 
            payloads_line_complete.append(",");
    }

    payloads_line_complete.append("};\n");
}

void payload_Sched_fill(uint8_t* sched_data, string & filled) {
    bool condition = sizeof(sched_data) > SCHEDPAYLOAD_STRING_MAXSIZE;
    check_error(condition, "ERROR: Sched Info size bigger than allowed: " + 
                            to_string(sizeof(sched_data)) + " > 60 bytes\n");
    
    // Data as string
    string sched_info = reinterpret_cast<char*>(sched_data);

    // Find Monopolize and ProcType
    string monopolize = "Monopolize: ";
    string procType = "ProcType: ";

    size_t monopolizePos = sched_info.find(monopolize);
    size_t procTypePos = sched_info.find(procType);
    condition = (monopolizePos == string::npos || procTypePos == string::npos);
    check_error(condition, "ERROR: Bad formatted Sched Info. It must be like this\n" \
                            "Sched Info:\n\tMonopolize: true|false\n" \
                            "\tProcType: \"LOWPERF|STANDARD|HIGHPERF|CRITICAL\"\n");
    
    size_t endLinePos = sched_info.find("\n", monopolizePos);
    string monopolizeValue = sched_info.substr(monopolizePos + 12, endLinePos - (monopolizePos + 12));

    endLinePos = sched_info.find("\n", procTypePos);
    string procTypeValue = sched_info.substr(procTypePos + 10, endLinePos - (procTypePos + 10));

    condition = (monopolizeValue.compare("true") && monopolizeValue.compare("false")); 
    check_error(condition, "ERROR: Monopolize must be true or false\n");

    condition = (procTypeValue.compare("\"LOWPERF\"") && procTypeValue.compare("\"STANDARD\"") &&
                    procTypeValue.compare("\"HIGHPERF\"") && procTypeValue.compare("\"CRITICAL\"")); 
    check_error(condition, "ERROR: ProcType must be LOWPERF, STANDARD, HIGHPERF or CRITICAL between quotes (\")\n");

    // CREATE THE FULL STRING
    string payloads_type_Sched;

    payloads_type_Sched.append("{");
    payloads_type_Sched.append(monopolizeValue);
    payloads_type_Sched.append(",");
    payloads_type_Sched.append(procTypeValue);
    payloads_type_Sched.append("}");

    // Assign the line to the string passed by reference
    filled = payloads_type_Sched;
}

void payload_Binary_fill(uint8_t* binary_data, string & filled, long file_size) {
    // Binary file size
    string file_size_string = to_string(file_size);

    // Canonical hex bytes as string
    string hex_bytes;
    stringstream stream;

    for (int i = 0; i < file_size; i++) {
        stream << hex << uppercase << setw(2) << static_cast<unsigned int>(binary_data[i]);
    }

    hex_bytes = stream.str();

    stream.str(string());
    stream.clear();

    cout << resetiosflags(cout.flags()); // clears all flags - reset cout

    // Length of bytes as string 
    size_t bytes_char_length = hex_bytes.length();
    string bytes_char_len_string = to_string(bytes_char_length);

    check_error(bytes_char_length > BINPAYLOAD_MAXSIZE, "Bytes como string (" + bytes_char_len_string + 
                                                        ") es mas grande que BINPAYLOAD_MAXSIZE - payload_Binary_fill\n");

    // CREATE THE FULL STRING
    string payloads_type_Binary;

    payloads_type_Binary.append("{");
    payloads_type_Binary.append(file_size_string);
    payloads_type_Binary.append(",");
    payloads_type_Binary.append(bytes_char_len_string);
    payloads_type_Binary.append(",\"");
    payloads_type_Binary.append(hex_bytes);
    payloads_type_Binary.append("\"}");

    // Assign the line to the string passed by reference
    filled = payloads_type_Binary;
}

void check_error(bool condition, string msg) {
    if (condition) {
        cout << msg;
        exit(EXIT_FAILURE); 
    }
}

uint8_t *get_payload_from_file(vector<string> args, int file_arg_num, off_t *file_size) {
    cout << "Args[" << file_arg_num << "] = " << args[file_arg_num] << "\n";  // Print plugin arguments

    int fdescriptor_input = open(args[file_arg_num].c_str(), O_RDONLY);
    check_error(fdescriptor_input == -1, "Error file descriptor de .bin\n");

    // Get the stat of the file
    struct stat stbuf;
    check_error((fstat(fdescriptor_input, &stbuf) != 0) || (!S_ISREG(stbuf.st_mode)), "Error stat del file .bin\n");

    *file_size = stbuf.st_size;

    close(fdescriptor_input);    

    // Open file in binary mode 
    fstream rawbin_fp;
    rawbin_fp.open(args[file_arg_num].c_str(), ios::in | ios::binary);
    check_error(rawbin_fp.is_open() == false, "Error al leer archivo rawbin_fp\n");

    // Create the uint8_t buffer for the read bytes
    uint8_t* buffer = new uint8_t [*file_size]();

    // Read bytes from the .bin file
    rawbin_fp.read((char*) buffer, *file_size);
    check_error(rawbin_fp.rdstate(), "Error al leer archivo rawbin_fp hacia buffer - read()\n");

    // Get read bytes from the last read operation
    size_t bytes_read = rawbin_fp.gcount();
    check_error(bytes_read != *file_size, "READ - Bytes_read: " + to_string(bytes_read) +
                                            " es distinto a file_size: " + to_string(*file_size) + "\n");

    // Close the opened file
    rawbin_fp.close();

    check_error(*file_size > BINPAYLOAD_MAXSIZE, "Error al abrir archivo para leer - size mayor que BINPAYLOAD_MAXSIZE - rawbin_fp\n");

    return buffer;
}

// Init. Create an instance of PluginRenameAction
static FrontendPluginRegistry::Add<PluginRenameAction>
    X("InsertPayload_Plugin", "Insert metadata plugin");
