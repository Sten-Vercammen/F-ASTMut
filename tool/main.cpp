///------------------------------------------------------------------------------
// Mutant schemata using Clang rewriter and RecursiveASTVisitor
//
// Sten Vercammem (sten.vercammen@uantwerpen.be)
//------------------------------------------------------------------------------

#include <fstream>
#include <functional>
#include <iostream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <vector>

#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Lex/Lexer.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaConsumer.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"

#define DebugFlag 1
#if _MSC_VER
#include <windows.h>
#define SYSERROR() GetLastError()
#else
#include <errno.h>
#define SYSERROR() errno
#endif


#ifndef MS_CLANG_REWRITE
#define MS_CLANG_REWRITE
#define STANDALONE
#ifndef STANDALONE
static SchemataApiCpp* sac;
#endif
#define EXPORT_MUTANTS
//#define SCHEMATA
//#define EXPORT_NON_COMPILING_MUTANTS
//#define SPLIT_STREAM
//#define MAXRUNTIME_ST_MS 60000
//#define MOCK_SPLIT
//#define 
_NON_COMPILING_MUTANTS
//#define DEBUG_MODE
//#define EXPORT_REACHABLE_MUTANTS

std::string restrictedPath;
std::string buildPath;

// defines to control what happens \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\|
bool multiAnalysePerFile = false; // multi-analyse can create multiple AST's for the same file, use
                                  // in case a file is compiled mulitple times with different flags
// config mutant schemata
bool ROR = true; // Relational Operator Replacement  <,<=,>,>=,==,!=,true,false
bool AOR = true; // Arithmetic Operator Replacement
bool LCR = true; // Logical Connector Replacement
// bool UOI = true;    // Unary Operator Insertion
// bool ABS = true;    // Absolute Value Insertion
///////////////////////////////////////////////////////////////////////////////|

#include <chrono>
double makesSenseTime = 0.0;

// global clang vars \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\|
clang::Rewriter rewriter;
clang::SourceManager* SourceManager;
///////////////////////////////////////////////////////////////////////////////|

// global vars for mutant schemata housekeeping \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\|
std::vector<std::string> SourcePaths;
std::set<std::string> VisitedSourcePaths;
unsigned mutant_count = 0;
unsigned insertedMutants_count = 0;
///////////////////////////////////////////////////////////////////////////////|

// look for the restricted path withing the filename (filename is absolutepath ex.
// /path/to/src/file1.cpp and restrictedPath is /path/to/src/)
bool isWithinRestricted(std::string filename) {
#ifdef STANDALONE
    return std::find(SourcePaths.begin(), SourcePaths.end(), filename) != SourcePaths.end();
#else
    return filename.find(restrictedPath) != std::string::npos;
#endif
}

// variables need to uniquely insert meta-mutants \\\\\\\\\\\\\\\\\\\\\\\\\\\\\|
struct MutantInsert {
    const clang::FileEntry* fE; // needed to calc FID
    unsigned exprOffs;
    unsigned bracketsOffs;

    unsigned lineStart, columnStart;
    unsigned changeOffsStart, changeOffsEnd;
    std::string change;
    unsigned mutantNR;
    
    std::string expr;
    std::size_t exprHash;
    std::string brackets;

    bool valid;
    bool consty;
    bool templaty;

    MutantInsert(const clang::FileEntry* f, unsigned int eO, unsigned int bO, std::string e,
                 std::string b, bool v)
        : fE(f), exprOffs(eO), bracketsOffs(bO), expr(e), exprHash(std::hash<std::string>{}(e)),
          brackets(b), valid(v) {}
};

std::vector<MutantInsert*> mutantInserts;
// custom comperator for mutants based on their FileID (FileEntry) and location (offsets)
struct mutantLocComp {
    bool operator()(const MutantInsert* lhs, const MutantInsert* rhs) const {
        if (lhs->fE == rhs->fE) {
            if (lhs->exprOffs == rhs->exprOffs) {
                if (lhs->bracketsOffs == rhs->bracketsOffs) {
                    return lhs->exprHash < rhs->exprHash;
                }
                return lhs->bracketsOffs < rhs->bracketsOffs;
            }
            return lhs->exprOffs < rhs->exprOffs;
        }
        return lhs->fE < rhs->fE;
    }
};
// keep an ordered set of the created mutants to prevent duplicates
// TODO optimise: 1 per file, only overwrite when new mutants were added
std::set<MutantInsert*, mutantLocComp> insertedMutants;
///////////////////////////////////////////////////////////////////////////////|

// variables need to identify const/conexpr constructs \\\\\\\\\\\\\\\\\\\\\\\\|
struct ConstLoc {
    unsigned start;
    unsigned end;

#if _MSC_VER
    bool operator<(const ConstLoc& other) const {
        if (this->start == other.start) {
            return this->end < other.end;
        }
        return this->start < other.start;
    }
#else
    // sort based on first, then on second
    bool operator()(const ConstLoc& lhs, const ConstLoc& rhs) {
        if (lhs.start == rhs.start) {
            return lhs.end < rhs.end;
        }
        return lhs.start < rhs.start;
    }
#endif
};

// ordered set of all const/constexpr locations
#if _MSC_VER
std::map<const clang::FileEntry*, std::set<ConstLoc>*> constLocs;
std::map<const clang::FileEntry*, std::set<ConstLoc>*> templateLocs;
#else
std::map<const clang::FileEntry*, std::set<ConstLoc, ConstLoc>*> constLocs;
std::map<const clang::FileEntry*, std::set<ConstLoc, ConstLoc>*> templateLocs;
#endif
///////////////////////////////////////////////////////////////////////////////|

// needed functionality not (publically) available in clang \\\\\\\\\\\\\\\\\\\|
/**
 * Return true if this character is non-new-line whitespace:
 * ' ', '\\t', '\\f', '\\v', '\\r'.
 */
static inline bool isWhitespaceExceptNL(unsigned char c) {
    switch (c) {
    case ' ':
    case '\t':
    case '\f':
    case '\v':
    case '\r':
        return true;
    default:
        return false;
    }
}

class MutantRewriter : public clang::Rewriter {
public:
    bool InsertText(const clang::FileEntry* fE, unsigned StartOffs, clang::StringRef Str,
                    bool InsertAfter = true, bool indentNewLines = false) {
        clang::FileID FID = this->getSourceMgr().translateFile(fE);
        if (!FID.getHashValue()) {
            FID = this->getSourceMgr().createFileID(fE, clang::SourceLocation(),
                                                    clang::SrcMgr::CharacteristicKind::C_User);
        }

        llvm::SmallString<128> indentedStr;
        if (indentNewLines && Str.find('\n') != clang::StringRef::npos) {
            clang::StringRef MB = this->getSourceMgr().getBufferData(FID);

            unsigned lineNo = this->getSourceMgr().getLineNumber(FID, StartOffs) - 1;
            unsigned lineOffs;
#if _MSC_VER
            const clang::SrcMgr::ContentCache Content = this->getSourceMgr().getSLocEntry(FID).getFile().getContentCache();
             lineOffs = Content.SourceLineCache[lineNo];
#else
            const clang::SrcMgr::ContentCache* Content =
                this->getSourceMgr().getSLocEntry(FID).getFile().getContentCache();
            lineOffs = Content->SourceLineCache[lineNo];
#endif
            // Find the whitespace at the start of the line.
            clang::StringRef indentSpace;
            {
                unsigned i = lineOffs;
                while (isWhitespaceExceptNL(MB[i]))
                    ++i;
                indentSpace = MB.substr(lineOffs, i - lineOffs);
            }

            llvm::SmallVector<clang::StringRef, 4> lines;
            Str.split(lines, "\n");

            for (unsigned i = 0, e = lines.size(); i != e; ++i) {
                indentedStr += lines[i];
                if (i < e - 1) {
                    indentedStr += '\n';
                    indentedStr += indentSpace;
                }
            }
            Str = indentedStr.str();
        }

        getEditBuffer(FID).InsertText(StartOffs, Str, InsertAfter);
        return false;
    }
};

// fix backWards compatibility
clang::SourceLocation getSR(clang::CharSourceRange SR, bool afterToken = false) {
    return afterToken ? SR.getEnd() : SR.getBegin();
}
// fix backWards compatibility
clang::SourceLocation getSR(std::pair<clang::SourceLocation, clang::SourceLocation> SR,
                            bool afterToken = false) {
    return afterToken ? SR.second : SR.first;
}

// starting with clang 8, getLocStart is removed and replaced with getBeginLoc
template <class T>
clang::SourceLocation getBeginLoc(T *t) {
#if __clang_major__ > 7
    return t->getBeginLoc();
#else
    return t->getLocStart();
#endif
}

// starting with clang 8, getLocEnd is removed and replaced with getEndLoc
template <class T>
clang::SourceLocation getEndLoc(T *t) {
#if __clang_major__ > 7
    return t->getEndLoc();
#else
    return t->getLocEnd();
#endif
}


#if _MSC_VER
clang::SourceLocation getFileLoc(clang::SourceLocation Loc, bool afterToken = false) {
    if (Loc.isFileID())
        return Loc;
    clang::FullSourceLoc fsm = clang::FullSourceLoc(Loc, *SourceManager);
    return fsm.getFileLoc();
    //return getFileLocSlowCase(Loc, afterToken);
}
#else
clang::SourceLocation getFileLocSlowCase(clang::SourceLocation Loc, bool afterToken = false) {
    do {
        if (SourceManager->isMacroArgExpansion(Loc)) {
            Loc = SourceManager->getImmediateSpellingLoc(Loc);
        } else {
            Loc = getSR(SourceManager->getImmediateExpansionRange(Loc), afterToken);
        }
    } while (!Loc.isFileID());
    return Loc;
}

clang::SourceLocation getFileLoc(clang::SourceLocation Loc, bool afterToken = false) {
    if (Loc.isFileID())
        return Loc;
    return getFileLocSlowCase(Loc, afterToken);
}
#endif

/**
 * Calculates offset of location, optionally increased with the range of the last token
 * Returns true on failure
 */
bool calculateOffsetLoc(clang::SourceLocation Loc, const clang::FileEntry*& fE, unsigned& offset,
                        unsigned& lineNr, unsigned& columnNr, bool afterToken = false) {
    // make sure we use the correct Loc, MACRO's needs to be tracked back to their spelling location
    Loc = getFileLoc(Loc, afterToken);
    if (!clang::Rewriter::isRewritable(Loc)) {
        llvm::errs() << "not rewritable !!!!!\n";
        return true;
    }
    assert(Loc.isValid() && "Invalid location");
    /*
     * Get FileID and offset from the Location.
     * Offset is the offset in the file, so this is a "constant".
     * FileID's can change depending on the order of opening, so we can't trust this.
     * We'll store the FileEntry and infer the FileID from it when we need it.
     */
    std::pair<clang::FileID, unsigned> V = SourceManager->getDecomposedLoc(Loc);
    clang::FileID FID = V.first;
    fE = SourceManager->getFileEntryForID(FID);
    offset = V.second;

    // TODO note that this is expensive!!!
    lineNr = SourceManager->getLineNumber(V.first, V.second);
    columnNr = SourceManager->getColumnNumber(V.first, V.second);

    if (afterToken) {
        // we want the offset after the last token, so we need to calculate the range of the last
        // token
        clang::Rewriter::RewriteOptions rangeOpts;
        rangeOpts.IncludeInsertsAtBeginOfRange = false;
        unsigned tokenLength = rewriter.getRangeSize(clang::SourceRange(Loc, Loc), rangeOpts);
        offset += tokenLength;
        columnNr += tokenLength;
    }
    return false;
}
///////////////////////////////////////////////////////////////////////////////|

// functionality to write changed files to disk \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\|
/**
 * Write the modified AST to files.
 * Either in place, or with suffix _mutated
 *
 * Note: use false, as inPlace causes a sementation fault in clang's sourcemaneger calling the
 * ComputeLineNumbers function, when used in the EndSourceFileAction. Caling
 * rewriter.overwriteChangedFiles() after Tool.run causes a segmentation fault as some rewrite
 * buffers are already deconstructed. This is why we need to use temp files.
 */
void writeChangedFiles(bool inPlace) {
    if (inPlace) {
        rewriter.overwriteChangedFiles();
    } else {
        /*
         * iterate through rewrite buffer
         *
         * Note: when a (system) file is visited without being mutated,
         * then it will currently also turn up here (as an editBuffer was created for it)
         */
        for (std::map<clang::FileID, clang::RewriteBuffer>::iterator I = rewriter.buffer_begin(),
                                                                     E = rewriter.buffer_end();
             I != E; ++I) {
            const clang::FileEntry* file = rewriter.getSourceMgr().getFileEntryForID(I->first);
            if (file) {
                if (file->isValid()) {
                    // Mark changed file as visited, so we know it was changed.
                    VisitedSourcePaths.insert(file->tryGetRealPathName());
                    
                    // include MUTANT_NR definition in each TU
                    std::string include = "";
#ifndef SPLIT_STREAM
                    include += "#ifndef schemataFunctions_h\n";
                    include += "#define schemataFunctions_h\n";
                    include += "#include <cstdlib>\n";
                    include += "static const int MUTANT_NR = std::atoi(getenv(\"MUTANT_NR\"));\n";
                    //                    include += "static void __schemataENVSetter(void) __attribute__((constructor));\n";
                    //                    include += "static void __schemataENVSetter(void) {MUTANT_NR = std::atoi(getenv(\"MUTANT_NR\"));}\n";
                    include += "#endif /* schemataFunctions_h */\n";
                    include += "\n\n";
#ifdef EXPORT_REACHABLE_MUTANTS
                    include += R"VOGON(
        #include <fstream>
        #ifdef EXPORT_REACHABLE_MUTANTS
        extern bool *reachable_mutants;
        bool f_export_ms(int new_mutant);
        #else
        #define EXPORT_REACHABLE_MUTANTS
        // calloc ensures array is set to 0
        bool *reachable_mutants __attribute__((weak)) = static_cast<bool*>(calloc(8388608, sizeof(bool))); //1024*1024*8 = 8,388,608 bits, 1 MByte, i.e. current hard limit to mutants
        bool __attribute__((weak))  f_export_ms(int new_mutant) {
        if (MUTANT_NR == 0) {
        if (reachable_mutants[new_mutant] == false) {
            reachable_mutants[new_mutant] = true;
            //todo printf new_mutant to reachable_mutants.csv
        
        std::ofstream ofstream(")VOGON";
                    include += buildPath;
                    include += R"VOGON("+std::string("mutants.csv"), std::ios_base::out | std::ios_base::app | std::ios_base::binary); // append, don't override
        if (!ofstream.is_open()){
            printf("file not open!!!\n");
        }
        ofstream << new_mutant << "\n";
        ofstream.flush();
        ofstream.close();
        }
        return false;
    }
    return new_mutant == MUTANT_NR;
}
#endif
)VOGON";
#endif
                    
#endif
#ifdef MOCK_SPLIT
                    include += "#define MOCK_SPLIT\n";
#endif
#ifdef SPLIT_STREAM
                    include += R"VOGON(
#ifdef SPLIT_STREAM
#include <signal.h>     // siginfo_t

extern int MN, max_MN;
extern bool *mutants_executed, *exit_gracefully;
extern bool first_mutant_encounter;

void hook_after_mutant_executed(int mutant_nr);
void handle_alarm_signal(int signal, siginfo_t *info, void *context);
void setup_timer(long long timeout);
bool f_test123fa(int new_mutant);

void process_mutant_progress_file();
bool is_mutant_executed(int mutant);
void update_progress_file(int mutant_nr, int exit_status);

#else
#define SPLIT_STREAM
#include <cerrno>       // errno
#include <signal.h>     // sigaction
#include <stdlib.h>     // calloc, exit, free
#include <stdio.h>      // printf
#include <string.h>     // memset
#include <sys/time.h>   // setitimer itimerval
#include <sys/wait.h>   // wait
#include <unistd.h>     // _exit
#include <iostream>
#include <fstream>
#include <sstream>

int MN __attribute__((weak)) = 0;
int max_MN __attribute__((weak)) = 8388608; //1024*1024*8 = 8,388,608 bits, 1 MByte, i.e. current hard limit to mutants

// calloc ensures array is set to 0
bool *mutants_executed __attribute__((weak)) = static_cast<bool*>(calloc(max_MN, sizeof(bool)));
bool *exit_gracefully __attribute__((weak)) = static_cast<bool*>(calloc(max_MN, sizeof(bool)));
int MN_encountered __attribute__((weak)) = 0;
bool first_mutant_encounter __attribute__((weak)) = true;

void __attribute__((weak)) hook_after_mutant_executed(int mutant_nr) {
    // This is your chance to move reports to a dedicated folder or put the results in a database
    // They might/will be overwritten by the next mutant.
}

void __attribute__((weak)) handle_alarm_signal(int signal, siginfo_t *info, void *context) {
    printf("Timed out, exiting\n");
    fflush(NULL);
    exit(112);
}

void __attribute__((weak)) setup_timer(long long timeout) {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_sigaction = &handle_alarm_signal;
    if (sigaction(SIGALRM, &action, NULL) != 0) {
        perror("sigaction");
        abort();
    }

    struct itimerval timer;     // Get only seconds in
    timer.it_value.tv_sec = timeout / 1000; /// Cut off seconds, and convert what's left into microseconds
    timer.it_value.tv_usec = (timeout % 1000) * 1000;

    /// Do not repeat
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 0;

    if (setitimer(ITIMER_REAL, &timer, NULL) != 0) {
        perror("setitimer");
        abort();
    }
}

void __attribute__((weak)) process_mutant_progress_file() {
    std::ifstream mutant_progress_file;
    mutant_progress_file.open("mutant_progress.txt");
    std::string mutant_line;
    if ( mutant_progress_file.is_open() ) { // file exists
        while ( mutant_progress_file ) { // equivalent to mutant_progress_file.good()
            std::getline (mutant_progress_file, mutant_line);
            // split on words (on spaces)
            std::string word;
            std::istringstream iss(mutant_line, std::istringstream::in);
            iss >> word;
            int file_mutant_nr = std::stoi( word );
            mutants_executed[file_mutant_nr] = true;
            iss >> word;
            if (word[0] == 'g') { // not gracefull for fail or timeout
                exit_gracefully[file_mutant_nr] = true;
            }
        }
    }
    mutant_progress_file.close();
}


// support for tests with exists of different test executions (multiple parts)
bool __attribute__((weak)) is_mutant_executed(int mutant) {
    if (first_mutant_encounter) {
        first_mutant_encounter = false;
        // if our mutant_progress file exists, we know this is a second test part
        // in this case, read in the file and which mutants already failed
        process_mutant_progress_file();
    }
    return mutants_executed[mutant];
}

void __attribute__((weak)) update_progress_file(int mutant_nr, int exit_status) {
    std::fstream fs("mutant_progress.txt", std::fstream::in | std::fstream::out);
    if (fs.is_open()) {
        char c;
        bool look_for_number = true;
        std::string number = "";
        while (fs.get(c)) {
            if (look_for_number) {
                number = ""; // reset
                while (fs.get(c)) {
                    if (c == ' ') {
                        look_for_number = false;
                        break;
                    }
                    fs.put(c);
                }
                if (std::stoi(number) == mutant_nr) {
                    // update status
                    if (exit_status == 112) {
                        fs.write("timeout: ", 9);
                    } else {
                        fs.write("fail: ", 6);
                    }
                    std::string exit_status_string = std::to_string(exit_status);
                    fs.write(exit_status_string.c_str(), exit_status_string.length());
                    // write remainder of line
                    while(fs.peek() != '\n') {
                        fs.put(' ');
                    }
                }
            } else if (c == '\n') {
                look_for_number = true;
            }
        }
    }
    fs.close();
}

bool __attribute__((weak))  f_test123fa(int new_mutant) {
#ifdef MOCK_SPLIT
    return false;
#else
    // if child, (MN != 0) return whether you're the mutant in question
    if (MN != 0) {
        return MN == new_mutant;
    }
    // if parent, (MN == 0) and the mutant has been forked, return false, otherwise fork it (later)
    if (mutants_executed[new_mutant]) { // don't merge with previous, to prevent copy on read of mutants_executed in forked processes, a forked process does not execute this
        return false;//MN == new_mutant;
    }
    // store mutant as forked
    mutants_executed[new_mutant] = 1;
    MN_encountered++;
    // output the mutant nr to a file
//    std::ofstream outfile ("mutant_progress.txt", std::ios_base::app);
 //   outfile << new_mutant;
  //  outfile.close();


    fflush(NULL);
    // adding random file to ensure we have something to commit :)
    printf("new_mutant: %i\n", new_mutant);
 //   system("touch ../mt_specifici_garbage_file_" + new_mutant);
    std::string filename = "../mt_specifici_garbage_file_" + std::to_string(new_mutant) + ".txt";
    std::ofstream file {filename};
    system("git add -A");
    system("git commit -m \"commit before fork\" --author=\"auto <auto@local.com>\"");
    fflush(NULL);
    //sleep(100);
    const pid_t worker_pid = fork();
    if (worker_pid == -1) {
      perror("fork worker");
      abort();
    }
    if (worker_pid == 0) {
        setup_timer(
)VOGON";
                    
                    include += std::to_string(MAXRUNTIME_ST_MS);
                    include += R"VOGON(
);
        MN = new_mutant;
        printf("Child: Executing mutant NR: %i\n", MN);
        return true;
    }
    printf("Parent: detected new mutant: %i\n", new_mutant);
    int status = 0;
    while (wait(&status) == -1) {
        if (errno == EINTR) {
            continue;
        }
    if (WTERMSIG(status) == SIGSEGV) {
            break;
        }
    //else {
        //    perror("waitpid");
         //   abort();
        //}
    }
    exit_gracefully[new_mutant] = (WIFEXITED(status));// && (WTERMSIG(status) != SIGSEGV));// &&*/ WEXITSTATUS(status) == 0;//112);  // store if exited gracefully (i.e. exit code 0 -> true)

    std::ofstream outfile ("mutant_progress.txt", std::ios_base::app);
    if (exit_gracefully[new_mutant]){
        outfile << new_mutant;
        switch (WEXITSTATUS(status)) {
            case 0:
            case 1:
                outfile << " gracefull: ";
                break;
            case 112:
                outfile << " timeout";
                break;
            default:
                outfile << " fail";
                break;
        }
        outfile << ": " << WEXITSTATUS(status) << std::endl;
    } else {
        outfile << new_mutant << " fail:" << std::endl;
    }
    outfile.close();

    hook_after_mutant_executed(new_mutant);
    //system("echo \"-----------------------\"");
    //system ("git diff --name-only");
    //system ("cd `git rev-parse --show-toplevel`; git diff --name-only | while read line; do git show HEAD:$line > $line; done");
    system("git reset --hard HEAD~1");
    return false;
#endif
}
#endif
)VOGON";
#endif
                    
                    //    I->second.InsertTextAfter(0, include);
                    
                    // write what's in the buffer to a temporary file
                    // placed here to prevent writing the mutated file multiple times
                    std::error_code error_code;
                    //                        std::string fileName = file->getName().str() +
                    //                        "_mutated_" + std::to_string(mutant_count);
                    std::string fileName = file->getName().str() + "_mutated";
#if _MSC_VER
                    llvm::raw_fd_ostream outFile(fileName, error_code, llvm::sys::fs::OF_None);
#else
                    llvm::raw_fd_ostream outFile(fileName, error_code, llvm::sys::fs::F_None);
#endif
                    
                    //I->second.InsertTextAfter(0, include);
                    // write the schemata code to the top of the file
                    outFile << include;
                    
                    // detect if there are BOM chars that we need to skip
                    // (currently only checking for utf8 BOM)
                    std::stringstream BOMstream;
                    clang::RewriteBuffer::iterator it = I->second.begin();
                    for (int i = 0; i < 3; i++) {
                        if (it == I->second.end()) {
                            break;
                        }
                        it++;
                    }
                    BOMstream << std::string(I->second.begin(), it);
                    std::string BomStr = BOMstream.str();
                    it = I->second.begin();
                    char a = 0xEF;
                    char b = 0xBB;
                    char c = 0xBF;
                    
                    if (BomStr.find(a) != std::string::npos) {
                        it++;
                    }
                    if (BomStr.find(b) != std::string::npos) {
                        it++;
                    }
                    if (BomStr.find(c) != std::string::npos) {
                        it++;
                    }
                    // write original file to (skipping BOM chars)
                    outFile << std::string(it, I->second.end());
                    
                    
                    
                    outFile.close();
                    
                    // debug output
                    // llvm::errs() << "Mutated file: " << file->getName().str() << "\n";
                }
            }
        }
    }
}

/**
 * write the temporary files over the original ones
 */
void overWriteChangedFile() {
    for (std::string fileName : VisitedSourcePaths) {
        std::string mutatedFileName = fileName + "_mutated";
        // deletes the old file
#if _MSC_VER
        std::ifstream ifs (mutatedFileName, std::ifstream::in);
        if (ifs.is_open()) {
            // file exists, remove original file
            ifs.close();
            if (std::remove(fileName.c_str()) != 0) {
                std::string err = "Error removing file, " + fileName + ", error: ";
                std::perror(err.c_str());
            }
        } else {
            // file does not exist, do nothing
            continue;
        }
#endif
        // rename the mutated file to the original file name
        if (std::rename(mutatedFileName.c_str(), fileName.c_str()) != 0) {
            std::string err = "Error renaming file, " + mutatedFileName + " not found";
            std::perror(err.c_str());
        }
    }
}
///////////////////////////////////////////////////////////////////////////////|

enum Singleton { LHS, RHS, False, True, NotASingleTon };

// actual clang functions to traverse AST \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\|
class MutatingVisitor : public clang::RecursiveASTVisitor<MutatingVisitor> {
private:
    clang::ASTContext* astContext;
    clang::PrintingPolicy pp;

    bool makesSense(clang::BinaryOperator* binOp, clang::BinaryOperatorKind Opc) {
        std::chrono::time_point<std::chrono::system_clock> t0 = std::chrono::system_clock::now();
        assert(sema && "no Sema");  // just making sure

        //TODO I think this is fixed, leaving it here in case it isn't
        /* TODO dirty hack to ensure that the rem operator does not cause compilation faults.
         * for some reason, when getting templated stored values are not getting verified:
         * _Tp val[m]; -> val[i]%val[j] for BuildBinOp this is perfectly fine even if _Tp is
         * float/double
         */
//        if (Opc == clang::BO_Rem) {
//            if (!binOp->getLHS()->getType()->hasIntegerRepresentation() ||
//                !binOp->getRHS()->getType()->hasIntegerRepresentation()) {
//                return false;
//            }
//        }
        
        // TODO sema doesn't (yet) take into account -Werror -> -Werror=type-limit
        /* TODO
         * Retrieve the parser's current scope.
         *
         * This routine must only be used when it is certain that semantic analysis and the parser are in precisely the same context, which is not the case when, e.g., we are performing any kind of template instantiation. Therefore, the only safe places to use this scope are in the parser itself and in routines directly invoked from the parser and never from template substitution or instantiation.

         * -> Thus we probably shouldn't use that here
         */
//        printf("LHS: %s\n", convertExpressionToString(binOp->getLHS()).c_str());
//        printf("RHS: %s\n", convertExpressionToString(binOp->getRHS()).c_str());
//        printf("getting inermost items\n");
        clang::Expr* lhs = getIntendedLHS(binOp->getLHS());
        clang::Expr* rhs = getIntendedRHS(binOp->getRHS());
//        printf("LHS: %s\n", convertExpressionToString(binOp->getLHS()).c_str());
//        printf("RHS: %s\n", convertExpressionToString(binOp->getRHS()).c_str());
//        printf("op %s is ", clang::BinaryOperator::getOpcodeStr(Opc).str().c_str());
        clang::ExprResult expr = sema->BuildBinOp(sema->getCurScope(), binOp->getExprLoc(), Opc, lhs, rhs);

        bool rt = false;
        if (!expr.isInvalid() && expr.isUsable()) {
//            printf("valid\n");
            std::chrono::time_point<std::chrono::system_clock> t1 = std::chrono::system_clock::now();
            makesSenseTime += std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
            rt = true;
        }
//        printf("invalid\n");
        return rt;
    }
    
    /**
     * returns actual LHS  of expression, which might be different from original parse tree.
     * e.g. LHS: a + b & RHS c
     * In this case the operand between b and c matters, if it's +, then LHS + RHS is the same.
     * if it's *, then LHS * RHS is effectively (a+b) * c, while we want a + (b*c).
     * Thus we return LHS = b
     */
    clang::Expr* getIntendedLHS(clang::Expr* e) {
        for(;;) {
            std::string s = convertExpressionToString(e);
            std::string::iterator start = s.begin();
            while (start != s.end() && std::isspace(*start)) {
                start++;
            }
            if (*start == '(') {
                return e;
            }
            if (clang::isa<clang::BinaryOperator>(e)) {
                e = clang::cast<clang::BinaryOperator>(e)->getRHS();
            } else {
                return e;
            }
        }
    }

    /**
     * returns actual RHS of expression, which might be different from original parse tree.
     * e.g. LHS: a & RHS b + c
     * In this case the operand between b and c matters, if it's +, then LHS + RHS is the same.
     * if it's *, then LHS * RHS is effectively a * (b+c), while we want (a * b) + c.
     * Thus we return RHS = b
     */
    clang::Expr* getIntendedRHS(clang::Expr* e) {
        for(;;) {
            std::string s = convertExpressionToString(e);
            std::string::iterator end = s.end();
            do {
                end--;
            } while (end != s.begin() && std::isspace(*end));
            if (*end == ')') {
                return e;
            }
            if (clang::isa<clang::BinaryOperator>(e)) {
                e = clang::cast<clang::BinaryOperator>(e)->getLHS();
            } else {
                return e;
            }
        }
    }

    /**
     * get the actual string representation of the expression, as given in the original source files
     */
    std::string convertExpressionToString(clang::Expr* E) {
        clang::SourceManager& SM = astContext->getSourceManager();
        clang::SourceLocation b = getFileLoc(getBeginLoc(E), false);
        clang::SourceLocation _e = getFileLoc(getEndLoc(E), true);
        clang::SourceLocation e =
            clang::Lexer::getLocForEndOfToken(_e, 0, SM, astContext->getLangOpts());
        return std::string(SM.getCharacterData(b), SM.getCharacterData(e) - SM.getCharacterData(b));
    }

    /**
     * Craete meta-mutants for the operation at binOp with the provided operators in the lists
     *
     * Note: This funciton does not (yet) use the Lexer to retrieve the sourceText.
     * Macro's etc. can thus be expanded, the operands might thus look slightly different than the
     * original code
     */
    void insertMutantSchemata(clang::BinaryOperator* binOp,
                              std::initializer_list<clang::BinaryOperatorKind> list,
                              std::initializer_list<Singleton> singletons) {
        // get lhs of expression
        std::string lhs = convertExpressionToString(binOp->getLHS());

        // get rhs of expression
        std::string rhs = convertExpressionToString(binOp->getRHS());

        // get expression with mutant schemata
        std::string newExpr;
        std::string endBracket = ")";
        for (const auto& elem : list) {
            if (binOp->getOpcode() != elem) {
                bool validMutant = makesSense(binOp, elem);
                newExpr = lhs;
                newExpr += " ";
                newExpr += clang::BinaryOperator::getOpcodeStr(elem).str();
                newExpr += " ";
                newExpr += rhs;
                createAndStoreActualMutant(binOp, newExpr, endBracket, validMutant,
                                           Singleton::NotASingleTon,
                                           clang::BinaryOperator::getOpcodeStr(elem).str());
#ifdef DEBUG_MODE
                if (!validMutant) {
                    llvm::errs() << "compiler found invalid mutant, can't change this to: "
                                 << clang::BinaryOperator::getOpcodeStr(elem).str() << "\n";
                }
#endif
            }
        }

        // check if we need to insert some singletons
        for (const auto& elem : singletons) {
            // TODO return type must be the same when using ? operator
            // This can be an issue with char* + 2 -> NR == 2? 2 : char* + 2
            // In googletest this happens when char* is LHS and we want a bool
            // For now we just add a hardcoded cast whene we dedect that it should be a bool

            bool compileOk = true;
            switch (elem) {
            case Singleton::LHS:
                compileOk = (binOp->getType().getTypePtr() == binOp->getLHS()->getType().getTypePtr() || binOp->getLHS()->getType().getTypePtr() <= binOp->getRHS()->getType().getTypePtr()) && (binOp->getType().getTypePtr()->isPointerType() == binOp->getLHS()->getType().getTypePtr()->isPointerType());// || binOp->getType().getTypePtr() < binOp->getLHS()->getType().getTypePtr();
                if (binOp->getType().getTypePtr()->isBooleanType()) {
                    newExpr = "bool(" + lhs + ")";
                } else {
                    newExpr = lhs;
                }
                break;
            case Singleton::RHS:
                compileOk = (binOp->getType().getTypePtr() == binOp->getRHS()->getType().getTypePtr() || binOp->getLHS()->getType().getTypePtr() <= binOp->getRHS()->getType().getTypePtr()) && (binOp->getType().getTypePtr()->isPointerType() == binOp->getRHS()->getType().getTypePtr()->isPointerType());// || binOp->getType().getTypePtr() < binOp->getRHS()->getType().getTypePtr();
                if (binOp->getType().getTypePtr()->isBooleanType()) {
                    newExpr = "bool(" + rhs + ")";
                } else {
                    newExpr = rhs;
                }
                break;
            case Singleton::True:
                compileOk = true;//binOp->getType().getTypePtr()->isBooleanType();
                newExpr = "true";
                break;
            case Singleton::False:
                compileOk = true;//binOp->getType().getTypePtr()->isBooleanType();
                newExpr = "false";
                break;
            default:
                // continue for loop
                continue;
            }
            // insert the singleton
            createAndStoreActualMutant(binOp, newExpr, endBracket, compileOk, elem, newExpr);
        }
    }

    void createAndStoreActualMutant(const clang::BinaryOperator* binOp, const std::string& newExpr,
                                    const std::string& endBracket, bool valid, Singleton singleton,
                                    std::string changedExpr) {
        // calculate offset and FileEntry (we don't use FileID as this can change depending on the
        // order of opening files etc.)
        const clang::FileEntry* fE;
        unsigned lineStart, columnStart;
        unsigned exprOffs, bracketsOffs;
        // calculate offset for start of expression
        calculateOffsetLoc(getBeginLoc(binOp), fE, exprOffs, lineStart, columnStart);
        // calculate offset for end of exbracketpression
        calculateOffsetLoc(getEndLoc(binOp), fE, bracketsOffs, lineStart, columnStart, true);

        // create and store the created (meta-) mutant
        MutantInsert* mi = new MutantInsert(fE, exprOffs, bracketsOffs, newExpr, endBracket, valid);

        // we only need additional information if we want to store the mutant/export it
#ifdef EXPORT_MUTANTS
        unsigned changeOffsStart, changeOffsEnd;

        switch (singleton) {
            // e.g. expression: int r = a + b;
            // calculate the offsets for what we want to replace as in traditional mutation testing
        case Singleton::NotASingleTon:
            // replace only the operator "+"
            calculateOffsetLoc(binOp->getExprLoc(), fE, changeOffsStart, lineStart, columnStart);
            calculateOffsetLoc(binOp->getExprLoc(), fE, changeOffsEnd, lineStart, columnStart,
                               true);
            break;
        case Singleton::LHS:
            // keep the LHS, replace "a + b" (as we might also add type cast, we replace everything to a)
            // the offsets does also represent this
            calculateOffsetLoc(getBeginLoc(binOp), fE, changeOffsStart, lineStart, columnStart);
            calculateOffsetLoc(getEndLoc(binOp), fE, changeOffsEnd, lineStart, columnStart, true);
            break;
        case Singleton::RHS:
            // keep the RHS, replace "a + b" (as we might also add type cast, we replace everything to b)
            // the offsets does also represent this
            calculateOffsetLoc(getBeginLoc(binOp), fE, changeOffsStart, lineStart, columnStart);
            calculateOffsetLoc(getEndLoc(binOp), fE, changeOffsEnd, lineStart, columnStart,
                               true);
            break;
        case Singleton::True:
            // fall true to false below
        case Singleton::False:
            // keep nothing, replace "a + b"
            changeOffsStart = exprOffs;
            changeOffsEnd = bracketsOffs;
            break;
        default:
            break;
        }

        mi->lineStart = lineStart;
        mi->columnStart = columnStart;
        mi->changeOffsStart = changeOffsStart;
        mi->changeOffsEnd = changeOffsEnd;
        mi->change = changedExpr;
#endif
        insertedMutants.value_comp() = insertedMutants.key_comp();
        std::set<MutantInsert*, mutantLocComp>::iterator it = insertedMutants.find(mi);
        if (it == insertedMutants.end()) {
            mutantInserts.push_back(mi);
            insertedMutants.insert(mi);
            //            llvm::errs() << "inserted mutant in FE: " <<
            //            std::to_string((long)(mi->fE)) << " @offset: [" << mi->exprOffs << ", " <<
            //            mi->bracketsOffs << "] | " << mi->expr << "\n";
            mutant_count++; // increase count as we are sure it isn't a duplicated vallid mutant
        } else {
            //            llvm::errs() << "duplicate mutant in FE: " <<
            //            std::to_string((long)(mi->fE)) << " @offset: [" << mi->exprOffs << ", " <<
            //            mi->bracketsOffs << "] | " << mi->expr << "\n";
        }
    }

    /**
     * Check if we want to mutate this file.
     * (if it isn't a system file)
     */
    bool isfileToMutate(clang::FullSourceLoc FullLocation) {
        const clang::FileEntry* fE =
            SourceManager->getFileEntryForID(SourceManager->getFileID(FullLocation));
        if (fE) {
            return !SourceManager->isInSystemHeader(FullLocation) &&
                   !SourceManager->isInExternCSystemHeader(FullLocation) &&
                   isWithinRestricted(fE->tryGetRealPathName().str());
        }
        return false;
    }

    template <class T>
    void insertExcludedLoc(
        T* declOrSmtm,
#if _MSC_VER
        std::map<const clang::FileEntry*, std::set<ConstLoc>*>& excludedLocs) {
#else
        std::map<const clang::FileEntry*, std::set<ConstLoc, ConstLoc>*>& excludedLocs) {
#endif
        const clang::FileEntry* fE;
        unsigned startOffs, endOffs;
        unsigned lineStart, columnStart;
        unsigned lineEnd, columnEnd;
        calculateOffsetLoc(getBeginLoc(declOrSmtm), fE, startOffs, lineStart, columnStart);
        calculateOffsetLoc(getEndLoc(declOrSmtm), fE, endOffs, lineEnd, columnEnd, true);

#if _MSC_VER
        std::map<const clang::FileEntry*, std::set<ConstLoc>*>::iterator it =
            excludedLocs.find(fE);
        if (it == excludedLocs.end()) {
            it = excludedLocs.insert({fE, new std::set<ConstLoc>()}).first;
        }
#else
        std::map<const clang::FileEntry*, std::set<ConstLoc, ConstLoc>*>::iterator it =
            excludedLocs.find(fE);
        if (it == excludedLocs.end()) {
            it = excludedLocs.insert({fE, new std::set<ConstLoc, ConstLoc>()}).first;
        }
#endif
        it->second->insert({startOffs, endOffs});
    }

public:
    clang::Sema* sema;

    explicit MutatingVisitor(clang::CompilerInstance* CI)
        : astContext(&(CI->getASTContext())), pp(astContext->getLangOpts()) {
        SourceManager = &astContext->getSourceManager();
    }

    virtual bool VisitDecl(clang::Decl* d) {
        //std::cout << "Visiting Decl" << std::endl; 
        bool consty = false;
        bool constyexpr = false;
        bool templaty = false;
        //if (clang::isa<clang::FunctionDecl>(d)) {
        //    clang::FunctionDecl* funcD = clang::cast<clang::FunctionDecl>(d);
        //    std::cout << "Visited: "  << funcD->getNameInfo().getAsString() << std::endl;
        //}

        if (clang::isa<clang::VarDecl>(d)) {
            clang::ValueDecl* valD = clang::cast<clang::ValueDecl>(d);
            clang::QualType qt = valD->getType();
            consty = qt.isConstQualified();

            clang::VarDecl* varD = clang::cast<clang::VarDecl>(d);
            constyexpr = varD->isConstexpr();

            const clang::Type* t = qt.getTypePtr();
            if (varD->isStaticLocal()) {
                // variable length array declaration cannot have 'static' storage duration
                // static array's lenght must dus be declared by const
                // this is a hack to detect this, TODO find correct clang function for detection
                if (t->isVariableArrayType() || t->isArrayType() || t->isConstantArrayType() ||
                    t->isIncompleteArrayType() || t->isDependentSizedArrayType()) {
                    consty = true;
                }
            }
        }
        if (clang::isa<clang::TypeDecl>(d)) {
            // TODO support typedefs (usings etc.)
            consty = true;
        }
        if (clang::isa<clang::TemplateDecl>(d)) {
            // TODO support templates
            templaty = true;
        }

        if (clang::isa<clang::FunctionDecl>(d)) {
            clang::FunctionDecl* funD = clang::cast<clang::FunctionDecl>(d);
            if (funD->getTemplatedKind() != clang::FunctionDecl::TemplatedKind::TK_NonTemplate) {
                templaty = true;
            }
            if (funD->isConstexpr()) {
                consty = true;
            }
        }

        if (clang::isa<clang::StaticAssertDecl>(d)) {
            consty = true;
        }

        if (consty || constyexpr) {
            insertExcludedLoc(d, constLocs);
        } else if (templaty) {
            insertExcludedLoc(d, templateLocs);
        }
        return true;
    }

    virtual bool VisitStmt(clang::Stmt* s) {
        //std::cout << "Visiting Stmt" << std::endl; 
        clang::FullSourceLoc FullLocation = astContext->getFullLoc(getBeginLoc(s));
        if (!isfileToMutate(FullLocation)) {
            return true;
        }

        bool templaty = false;

        if (clang::isa<clang::CaseStmt>(s)) {
            // LHS of case (before the : ) cannot be non-const
            insertExcludedLoc(clang::cast<clang::CaseStmt>(s)->getLHS(), constLocs);
            return true;
        }

        // mutate all expressions
        if (clang::isa<clang::Expr>(s)) {
            if (clang::isa<clang::BinaryOperator>(s)) {
                clang::BinaryOperator* binOp = clang::cast<clang::BinaryOperator>(s);
                mutateBinaryOperator(binOp);
            } else if (clang::isa<clang::UnaryOperator>(s)) {
                clang::UnaryOperator* unOp = clang::cast<clang::UnaryOperator>(s);
                mutateUnaryOperator(unOp);
            } else if (clang::isa<clang::DeclRefExpr>(s)) {
                // prevent mutating templated expressions as they should be constexpr: e.g.
                // int a = f<1+b> -> b must be const, -> f<MN == 1? 1+b: 1-b> isn't const
                clang::DeclRefExpr* declRefExpr = clang::cast<clang::DeclRefExpr>(s);
                if (declRefExpr->getNumTemplateArgs() > 0) {
                    templaty = true;
                }
            }
        }

        if (templaty) {
            insertExcludedLoc(s, templateLocs);
        }

        return true;
    }

private:
    void mutateBinaryOperator(clang::BinaryOperator* binOp) {
        if (ROR) {
            mutateBinaryROR(binOp);
        }
        if (AOR) {
            mutateBinaryAOR(binOp);
        }
        if (LCR) {
            mutateBinaryLCR(binOp);
        }
        // TODO UOI
        // TODO ABS
    }

    void mutateUnaryOperator(clang::UnaryOperator* unOp) {
        // TODO ROR
        // TODO AOR
        // TODO LCR
        // TODO UOI
        // TODO ABS
    }

    void mutateBinaryROR(clang::BinaryOperator* binOp) {
        const clang::Type* typeLHS = binOp->getLHS()->getType().getTypePtr();
        const clang::Type* typeRHS = binOp->getRHS()->getType().getTypePtr();
        if (typeLHS->isBooleanType() && typeRHS->isBooleanType()) {
            /* mutate booleans
             * only mutate == and !=
             * Mutations such as < for a boolean type is nonsensical in C++ or in C when the type is
             * _Bool.
             */
            switch (binOp->getOpcode()) {
            case clang::BO_EQ:
                insertMutantSchemata(binOp, {clang::BO_NE}, {Singleton::False});
                break;
            case clang::BO_NE:
                insertMutantSchemata(binOp, {clang::BO_EQ}, {Singleton::True});
                break;
            default:
                break;
            }
        } else if (typeLHS->isFloatingType() && typeRHS->isFloatingType()) {
            /* Mutate floats
             * Note: that == and != isn't changed compared to the original mutation schema
             * because normally they shouldn't be used for a floating point value but if they are,
             * and it is a valid use, the original schema should work.
             */
            switch (binOp->getOpcode()) {
                // Relational operators
            case clang::BO_LT:
                insertMutantSchemata(binOp, {clang::BO_GT}, {Singleton::False});
                break;
            case clang::BO_GT:
                insertMutantSchemata(binOp, {clang::BO_LT}, {Singleton::False});
                break;
            case clang::BO_LE:
                insertMutantSchemata(binOp, {clang::BO_GT}, {Singleton::True});
                break;
            case clang::BO_GE:
                insertMutantSchemata(binOp, {clang::BO_LT}, {Singleton::True});
                break;
                // Equality operators
            case clang::BO_EQ:
                insertMutantSchemata(binOp, {clang::BO_LE, clang::BO_GE}, {Singleton::False});
                break;
            case clang::BO_NE:
                insertMutantSchemata(binOp, {clang::BO_LT, clang::BO_GT}, {Singleton::True});
                break;
            default:
                break;
            }
        } else if (typeLHS->isEnumeralType() && typeRHS->isEnumeralType()) {
            /* Mutate the same type of enums
             * TODO: verify that the enums are of the same type
             */
            switch (binOp->getOpcode()) {
                // Relational operators
            case clang::BO_LT:
                insertMutantSchemata(binOp, {clang::BO_GE, clang::BO_NE}, {Singleton::False});
                break;
            case clang::BO_GT:
                insertMutantSchemata(binOp, {clang::BO_GE, clang::BO_NE}, {Singleton::False});
                break;
            case clang::BO_LE:
                insertMutantSchemata(binOp, {clang::BO_LT, clang::BO_EQ}, {Singleton::True});
                break;
            case clang::BO_GE:
                insertMutantSchemata(binOp, {clang::BO_GT, clang::BO_EQ}, {Singleton::True});
                break;
                // Equality operators
            case clang::BO_EQ:
                insertMutantSchemata(binOp, {}, {Singleton::False});
                // Specific additional schema for equal: TODO this need further investigation. It
                // seems like the generated mutant can be simplified to true/false but for now I am
                // not doing that because I may be wrong.
                // TODO if LHS is min enum literal: LHS <= RHS
                // TODO if LHS is max enum literal: LHS >= RHS
                // TODO if RHS is min enum literal: LHS >= RHS
                // TODO if RHS is max enum literal: LHS <= RHS
                break;
            case clang::BO_NE:
                insertMutantSchemata(binOp, {}, {Singleton::True});
                // Specific additional schema for not equal:
                // TODO if LHS is min enum literal: LHS < RHS
                // TODO if LHS is max enum literal: LHS > RHS
                // TODO if RHS is min enum literal: LHS > RHS
                // TODO if RHS is max enum literal: LHS < RHS
                break;
            default:
                break;
            }
        } else if (typeLHS->isPointerType() && typeRHS->isPointerType()) {
            /* Mutate pointers
             * This schema is only applicable when type of the expressions either sides is a pointer
             * type.
             */
            switch (binOp->getOpcode()) {
                // Relational operators
            case clang::BO_LT:
                insertMutantSchemata(binOp, {clang::BO_GE, clang::BO_NE}, {Singleton::False});
                break;
            case clang::BO_GT:
                insertMutantSchemata(binOp, {clang::BO_GE, clang::BO_NE}, {Singleton::False});
                break;
            case clang::BO_LE:
                insertMutantSchemata(binOp, {clang::BO_LT, clang::BO_EQ}, {Singleton::True});
                break;
            case clang::BO_GE:
                insertMutantSchemata(binOp, {clang::BO_GT, clang::BO_EQ}, {Singleton::True});
                break;
                // Equality operators
            case clang::BO_EQ:
                insertMutantSchemata(binOp, {clang::BO_NE}, {Singleton::False});
                break;
            case clang::BO_NE:
                insertMutantSchemata(binOp, {clang::BO_EQ}, {Singleton::True});
                break;
            default:
                break;
            }
        } else {
            // TODO verify this it's ok to use general rules and hope for the best
            switch (binOp->getOpcode()) {
                // Relational operators
            case clang::BO_LT:
                insertMutantSchemata(binOp, {clang::BO_LE, clang::BO_NE}, {Singleton::False});
                break;
            case clang::BO_GT:
                insertMutantSchemata(binOp, {clang::BO_GE, clang::BO_NE}, {Singleton::False});
                break;
            case clang::BO_LE:
                insertMutantSchemata(binOp, {clang::BO_LT, clang::BO_EQ}, {Singleton::True});
                break;
            case clang::BO_GE:
                insertMutantSchemata(binOp, {clang::BO_GT, clang::BO_EQ}, {Singleton::True});
                break;
                // Equality operators
            case clang::BO_EQ:
                insertMutantSchemata(binOp, {clang::BO_LE, clang::BO_GE}, {Singleton::False});
                break;
            case clang::BO_NE:
                insertMutantSchemata(binOp, {clang::BO_LT, clang::BO_GT}, {Singleton::True});
                break;
            default:
                break;
            }
        }
    }

    void mutateBinaryAOR(clang::BinaryOperator* binOp) {
        switch (binOp->getOpcode()) {
            // Additive operators
        case clang::BO_Add:
            insertMutantSchemata(binOp,
                                 {clang::BO_Sub, clang::BO_Mul, clang::BO_Div, clang::BO_Rem},
                                 {Singleton::LHS, Singleton::RHS});
            break;
        case clang::BO_Sub:
            insertMutantSchemata(binOp,
                                 {clang::BO_Add, clang::BO_Mul, clang::BO_Div, clang::BO_Rem},
                                 {Singleton::LHS, Singleton::RHS});
            break;
            // Multiplicative operators
        case clang::BO_Mul:
            insertMutantSchemata(binOp,
                                 {clang::BO_Sub, clang::BO_Add, clang::BO_Div, clang::BO_Rem},
                                 {Singleton::LHS, Singleton::RHS});
            break;
        case clang::BO_Div:
            insertMutantSchemata(binOp,
                                 {clang::BO_Sub, clang::BO_Mul, clang::BO_Add, clang::BO_Rem},
                                 {Singleton::LHS, Singleton::RHS});
            break;
        case clang::BO_Rem:
            insertMutantSchemata(binOp,
                                 {clang::BO_Sub, clang::BO_Mul, clang::BO_Div, clang::BO_Add},
                                 {Singleton::LHS, Singleton::RHS});
            break;
        default:
            break;
        }
    }

    void mutateBinaryLCR(clang::BinaryOperator* binOp) {
        switch (binOp->getOpcode()) {
            // Logical operators
        case clang::BO_LAnd:
            insertMutantSchemata(
                binOp, {clang::BO_LOr},
                {Singleton::True, Singleton::False, Singleton::LHS, Singleton::RHS});
            break;
        case clang::BO_LOr:
            insertMutantSchemata(
                binOp, {clang::BO_LAnd},
                {Singleton::True, Singleton::False, Singleton::LHS, Singleton::RHS});
            break;
            // Bitwise operator
        case clang::BO_And:
            insertMutantSchemata(binOp, {clang::BO_Or}, {Singleton::LHS, Singleton::RHS});
            break;
        case clang::BO_Xor:
            // TODO not implemented by Dextool
            break;
        case clang::BO_Or:
            insertMutantSchemata(binOp, {clang::BO_And}, {Singleton::LHS, Singleton::RHS});
            break;
        default:
            break;
        }
    }

    void mutateBinaryUOI(clang::BinaryOperator* binOp) {}

    void mutateBinaryABS(clang::BinaryOperator* binOp) {}
};

class MutationConsumer : public clang::SemaConsumer {
private:
    MutatingVisitor* visitor = nullptr;
    clang::Sema* sema = nullptr;

public:
    // override in order to pass CI to custom visitor
    explicit MutationConsumer(clang::CompilerInstance* CI) : visitor(new MutatingVisitor(CI)) {
        if (sema != nullptr) {
            visitor->sema = sema;
            // llvm::errs() << "Huston, we have a s sema\n";
        }
    }

    virtual void InitializeSema(clang::Sema& S) {
        sema = &S;
        if (visitor != nullptr) {
            visitor->sema = sema;
        }
        sema->getDiagnostics().setSuppressAllDiagnostics(true);
        // llvm::errs() << "setting the sema\n";
    }

    // override to call our custom visitor on the entire source file
    // Note we do this with TU as then the file is parsed, with TopLevelDecl, it's parsed whilst
    // iterating
    virtual void HandleTranslationUnit(clang::ASTContext& Context) {
        // we can use ASTContext to get the TranslationUnitDecl, which is
        // a single Decl that collectively represents the entire source file
        visitor->TraverseDecl(Context.getTranslationUnitDecl());
    }

    //     // override to call our custom visitor on each top-level Decl
    //     virtual bool HandleTopLevelDecl(clang::DeclGroupRef DG) {
    //     // a DeclGroupRef may have multiple Decls, so we iterate through each one
    //     for (clang::DeclGroupRef::iterator i = DG.begin(), e = DG.end(); i != e; i++) {
    //     clang::Decl *D = *i;
    //     visitor->TraverseDecl(D); // recursively visit each AST node in Decl "D"
    //     }
    //     return true;
    //     }
};

unsigned const_count = 0;
unsigned templ_count = 0;
unsigned invalid_count = 0;
unsigned normal_count = 0;

class MutationFrontendAction : public clang::ASTFrontendAction {
    void insertMutantIntoDB(const MutantInsert* mi, unsigned mutantNR) {
        insertedMutants_count++;
#ifndef STANDALONE
        CppType::SchemataMutant sm;
        sm.mut_id = mutantNR;
        sm.loc = {mi->lineStart, mi->columnStart};
        sm.offset = {mi->exprOffs, mi->bracketsOffs};
        sm.status = mi->valid ? 0 : 3;
        sm.inject = CppString::getStr(mi->change.c_str());
        sm.filePath = CppString::getStr(mi->fE->tryGetRealPathName().str().c_str());
        sac->apiInsertSchemataMutant(sm);
#endif
    }

public:
    virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance& CI,
                                                                  clang::StringRef file) {
        // skip this entry if we only want to analyse each file once
        // (we might miss some mutants when doing this)
        if (!multiAnalysePerFile) {
#if _MSC_VER
            if (VisitedSourcePaths.find(file.str()) != VisitedSourcePaths.cend()) {
#else
            if (VisitedSourcePaths.find(file) != VisitedSourcePaths.cend()) {
#endif
                return nullptr;
            }
        }

        // the same file can be analysed multiple times as it is possible that in one project it
        // needs to be compiled multiple times with different flags
        // llvm::errs() << "Starting to mutate the following file and all of it's includes: " <<
        // file << "\n";
        std::cout << file.str() << "\n";
        rewriter = clang::Rewriter();
        rewriter.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());

        // Mark changed file as visited
#if _MSC_VER
        VisitedSourcePaths.insert(file.str());
#else
        VisitedSourcePaths.insert(file);
#endif
        return std::unique_ptr<clang::ASTConsumer>(
            new MutationConsumer(&CI)); // pass CI pointer to ASTConsumer
    }

    virtual void EndSourceFileAction() {
#ifdef EXPORT_MUTANTS
        std::ofstream ofstream(buildPath+"mutants.csv", std::ios_base::out | std::ios_base::app | std::ios_base::binary); // append, don't override
        if (!ofstream.is_open()){
            printf("file (%s) not open!!!\n", (buildPath+"mutants.csv").c_str());
            std::cerr << "Failed to open file: "<< SYSERROR() << std::endl;
        }
#endif
        // llvm::errs() << "Adding mutants to database\n";
        unsigned localMutantCount = 1;
        for (std::vector<MutantInsert*>::const_iterator it = mutantInserts.cbegin();
             it != mutantInserts.cend(); it++) {
            const MutantInsert* mi = *it;
            bool outsideConst = true;
            bool outsideTempl = true;
            if (mi->valid) {
                auto it = constLocs.find(mi->fE);
                if (it != constLocs.end()) {
                    // there are const expressions
                    for (auto itt = it->second->begin(); itt != it->second->end(); ++itt) {
                        if (itt->start <= mi->exprOffs && itt->end >= mi->bracketsOffs) {
                            outsideConst = false;
                            break;
                        } else if (itt->start > mi->exprOffs) {
                            break;
                        }
                    }
                }
                it = templateLocs.find(mi->fE);
                if (it != templateLocs.end()) {
                    // there are const expressions
                    for (auto itt = it->second->begin(); itt != it->second->end(); ++itt) {
                        if (itt->start <= mi->exprOffs && itt->end >= mi->bracketsOffs) {
                            outsideTempl = false;
                            break;
                        } else if (itt->start > mi->exprOffs) {
                            break;
                        }
                    }
                }

                if (outsideConst && outsideTempl) {
                    // insert mutant before orig expression
                    MutantRewriter* r = (MutantRewriter*)(&rewriter);
                    // mi->startExpr, mi->expr);
#ifdef SPLIT_STREAM
                    std::string mutant = "(f_test123fa( ";
#else
#ifdef EXPORT_REACHABLE_MUTANTS
                    std::string mutant = "(f_export_ms( ";
#else
                    std::string mutant = "(MUTANT_NR == ";
#endif
#endif
                    mutant += std::to_string(localMutantCount);
#if defined SPLIT_STREAM || defined EXPORT_REACHABLE_MUTANTS
                    mutant += ")";
#endif
                    mutant += " ? ";
                    mutant += mi->expr;
                    mutant += ": ";
                    r->InsertText(mi->fE, mi->exprOffs, mutant);
                    // insert brackets to close mutant schemata's if statement
                    r->InsertText(mi->fE, mi->bracketsOffs, mi->brackets, true);
                } else {
#ifdef INSERT_NON_COMPILING_MUTANTS
                    // insert mutant before orig expression
                    MutantRewriter* r = (MutantRewriter*)(&rewriter);
                    // mi->startExpr, mi->expr);
                    std::string mutant = "/*(MUTANT_NR == ";
                    mutant += std::to_string(localMutantCount);
                    mutant += " ? ";
                    mutant += mi->expr;
                    mutant += ": */";
                    r->InsertText(mi->fE, mi->exprOffs, mutant);
                    // insert brackets to close mutant schemata's if statement
                    r->InsertText(mi->fE, mi->bracketsOffs, "/*" + mi->brackets + "*/", true);
#endif
                }
            }
            // only insert mutant if we haven't inserted it before
            if (localMutantCount++ >= insertedMutants_count) {
                insertMutantIntoDB(mi, localMutantCount-1);
#ifdef EXPORT_MUTANTS
#ifndef EXPORT_NON_COMPILING_MUTANTS
                if (mi->valid && outsideConst && outsideTempl) {
#else
                if (outsideConst && outsideTempl) {
#endif
                    ofstream << localMutantCount-1 << ","
                             << mi->fE->tryGetRealPathName().str().c_str() << ","
                             << mi->changeOffsStart << ","// start of string to replace (from beginning of file)
                             << mi->changeOffsEnd << ","  // end of string to replace (from beginnign of file)
                             << (mi->valid && outsideConst && outsideTempl) << ",";
                    // write it out in hex, to avoid \n , etc  messing up the csv file
                    for (char const elt: mi->change) {
                        ofstream << std::hex << std::setw(2) << std::setfill('0')
                                << static_cast<int>(elt) << '_' << std::dec;// reset to decimal
                    }
                    ofstream << "\n";
                }
#endif
                if (!outsideConst) {
                    ++const_count;
                }
                if (!outsideTempl) {
                    ++templ_count;
                }
                if (outsideTempl && outsideConst && mi->valid) {
                    ++normal_count;
                }
                if (outsideTempl && outsideConst && !mi->valid) {
                    ++invalid_count;
                }
            }
        }
#ifdef SCHEMATA
        writeChangedFiles(false);
#else
        ofstream.flush();
        ofstream.close();
#endif
    }
};

// Apply a custom category to all command-line options so that they are the only ones displayed.
static llvm::cl::OptionCategory MutantShemataCategory("mutation-schemata options");

/**
 * Expecting: argv: -p ../googletest/build filePathToMutate1 filePathToMutate2 ...
 */
#ifndef STANDALONE
int setupClang(int argc, const char** argv, SchemataApiCpp* s, CppString::CppStr restricted) {
    sac = s;
    sac->apiBuildMutant();
    restrictedPath = restricted.cppStr->c_str();
#else
int main(int argc, const char** argv) {
    printf("starting the program\n");
#endif
    
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "-p") {
            buildPath = argv[i+1];
            break;
        }
    }

    printf("splitted the args\n");

    // parse the command-line args passed to your code
#if _MSC_VER
    llvm::Expected<clang::tooling::CommonOptionsParser> op = clang::tooling::CommonOptionsParser::create(argc, argv, MutantShemataCategory);
    if (auto Err = op.takeError()) {
        // on error, fail
        return 1;
    }
    printf("created CommonOptionsParser\n");

    // store all paths to mutate, but fix to absolute path
    for (std::string item : op->getSourcePathList()) {
        printf("getSourcePathList \n");
        SourcePaths.push_back(clang::tooling::getAbsolutePath(item));
        printf("getAbsolutePath \n");
    }
    printf("stored sources\n");


    // create a new Clang Tool instance (a LibTooling environment)
    clang::tooling::ClangTool Tool(op->getCompilations(), op->getSourcePathList());
    printf("created clangTool\n");

#else
    clang::tooling::CommonOptionsParser op(argc, argv, MutantShemataCategory);
    // store all paths to mutate, but fix to absolute path
    for (std::string item : op.getSourcePathList()) {
        SourcePaths.push_back(clang::tooling::getAbsolutePath(item));
    }

    // create a new Clang Tool instance (a LibTooling environment)
    clang::tooling::ClangTool Tool(op.getCompilations(), op.getSourcePathList());
#endif

    // run the Clang Tool, creating a new FrontendAction
    int result = Tool.run(clang::tooling::newFrontendActionFactory<MutationFrontendAction>().get());
    printf("ran clang tool, rt value: %i\n", result);

    /*
     * move newly created files onto the old files
     * Caling rewriter.overwriteChangedFiles() here causes a segmentation fault
     * as some rewrite buffers are already deconstructed.
     * Calling it in the EndSourceFileAction causes a sementation fault
     * in clang's sourcemaneger calling the ComputeLineNumbers function.
     * This is why we need to use temp files.
     */
#ifdef SCHEMATA
    overWriteChangedFile();
#endif
    
    std::cout << "Mutations found: " << mutant_count << "\n";
    std::cout << "Mutations validaded: " << insertedMutants_count-1 << "\n";
    std::cout << "In const(expr): " << const_count << "\n";
    std::cout << "In template: " << templ_count << "\n";
    std::cout << "Invalid: " << invalid_count << "\n";
    std::cout << "Inserted mutants: " << normal_count << "\n";

    llvm::errs() << "Mutant validation time: " << makesSenseTime << "ms\n";

    return result;
}
///////////////////////////////////////////////////////////////////////////////|
#endif // MS_CLANG_REWRITE

