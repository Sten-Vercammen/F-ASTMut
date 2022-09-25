//
//  main.cpp
//  mutates a file given mutantnr,fileName,startPos,endPos,mutant
//  this file is used by the tradMutate.sh script
//
//  Created by Sten Vercammen on 23/11/2020.
//

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>

void extractArgs(int argc, char * argv [], std::string *args) {
    char *pch = std::strtok (argv[1], ",");
    int i = 0;
    while(pch != NULL && i < 6) {
        args[i++] = pch;
        pch = strtok (NULL, ",");
    }
}
void extractMutant(const char* m, std::string &mutant) {
    char *pch = std::strtok(const_cast<char*>(m), "_");
    while(pch != NULL) {
        mutant += (char)std::stoi(pch, 0, 16);
        pch = strtok (NULL, "_");
    }
}

void ensureBackup(std::string fileName) {
    std::ifstream file(fileName+"_orig", std::ios::binary);
    if(!file.is_open()) {
        std::ifstream  src(fileName, std::ios::binary);
        std::ofstream  dst(fileName+"_orig", std::ios::binary);
        dst << src.rdbuf();
        src.close();
        dst.close();
    }
}

void mutate(std::string fileName, long i0, long i1, std::string mutant) {
    ensureBackup(fileName);
    
    std::ifstream file(fileName+"_orig", std::ios::binary);
    // get length
    file.seekg(0,std::ios::end);
    long length = file.tellg();
    file.seekg(0,std::ios::beg);

    // store file in buffer
    char* buf = new char[length];
    file.read(buf,length);
    file.close();

    std::stringstream localStream;
    localStream.rdbuf()->pubsetbuf(buf,i0);

    std::ofstream ofstream(fileName, std::ios::binary | std::ios::trunc);
    // part 1
    ofstream << localStream.str();
    localStream.flush();
    // mutant
    ofstream << mutant;
    // part 2
    localStream.rdbuf()->pubsetbuf(&buf[i1], length-i1);
    ofstream << localStream.str();
    localStream.flush();
    ofstream.flush();
    ofstream.close();
}

#include <stdio.h>
#include <string.h>

int main(int argc, char * argv[]) {
    if (argc != 2) {
        printf("only got %i arguments, expected 2\nUsage: ./mutateFile mutantnr,fileName,startPos,endPos,mutant", argc);
    }

    std::string args[6];
    extractArgs(argc, argv, args);
        
    std::string mutant;
    extractMutant(args[5].c_str(), mutant);

    mutate(args[1], strtol (args[2].c_str(), NULL, 10), strtol (args[3].c_str(), NULL, 10), mutant);

    return 0;
}

