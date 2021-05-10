#include <Windows.h>
#include <iostream>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <unordered_map>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <ratio>
#include <chrono>
#include <ctype.h>
#include <cstring>
#include <cassert>
#include <random>

//STD
using namespace std;

#include "cout.h"

//MIDI
#include "midifile/MidiFile.h"
using namespace smf;

#define DEFAULT_DIVLEN 5.0
#define DIVLEN_MULT (256.0 / DEFAULT_DIVLEN)
bool DEBUG_SEE = true;
double divlen = DEFAULT_DIVLEN;
int starttick = 0;
int track = 0;
int channel = 0;
bool wasPlayingNote = false;
char datamdx[0x4000][24];
string notesD[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
MidiFile midifile;

//Output - display a number
string hexShow(int T)
{
    stringstream stream;
    if (T < 16)
    {
        stream << "0";
    }
    stream << hex << T;
    return stream.str();
}

//Process
void ProcessCMD(int index, uint_fast8_t CMD, uint_fast8_t ARG1, uint_fast8_t ARG2, uint_fast8_t ARG3) {
    if (CMD == 0xF2 || (CMD == 0xF3 && !wasPlayingNote))
    {
        if (CMD == 0xF2) {
            wasPlayingNote = false;
        }
        int duration = int(double(ARG1) * divlen);
        if (DEBUG_SEE) { cout << white << "Silence" << (CMD == 0xF3 ? " (Using ext)" : "") << ": " << duration; }
        starttick += duration;
    }
    else if (CMD == 0xD6)
    {
        divlen = ARG1 == 255 ? DEFAULT_DIVLEN : (double(ARG1) / DIVLEN_MULT);
        if (DEBUG_SEE) { cout << white << "Divlen Change: " << divlen; }
    }
    else if (CMD == 0xD2 || CMD == 0xD5)
    {
        if (DEBUG_SEE) { cout << white << "Bank/Instrument Change: " << int(ARG1); }
    }
    else if (CMD == 0xD0)
    {
        midifile.addTempo(track, starttick, ARG1);
        if (DEBUG_SEE) { cout << white << "Tempo Change: " << int(ARG1); }
    }
    else if (CMD == 0xF3 && !wasPlayingNote)
    {
        if (DEBUG_SEE) { cout << white << "ext already taken care of"; }
    }
    else if (CMD <= 96) {
        wasPlayingNote = true;
        int duration = int(double(ARG1) * divlen);
        //Scan for note extension commands
        if (DEBUG_SEE) { cout << lua_color << "Scanning for ext.. "; }
        while (true) {
            index += 4;
            uint_fast8_t C_CHECK = uint_fast8_t(datamdx[index + 3][channel]);
            if (DEBUG_SEE) { cout << hex << int(C_CHECK) << " " << dec; }
            //if it's a new note command, or silence, or stop then don't
            if ((C_CHECK <= 96 || C_CHECK == 0xEE) || (C_CHECK == 0xF2 || C_CHECK == 0xFF)) { break; }
            //continue to scan
            if (C_CHECK == 0xF3) {
                int exten = int(double(uint_fast8_t(datamdx[index + 2][channel])) * divlen);
                duration += exten;
                if (DEBUG_SEE) { cout << blue << "EXT +" << exten << lua_color << " "; }
            }
        }
        if (DEBUG_SEE) { cout << purple << notesD[CMD % 12] << "-" << int(CMD / 12) << " - " << int(ARG1) << " (" << duration << ")" << " V: " << int(ARG3) << white; }
        midifile.addNoteOn(track, starttick, channel, CMD, min(127, int(ARG3) * 3));
        midifile.addNoteOff(track, starttick + duration, channel, CMD);
        starttick += duration;
    }
    else {
        if (DEBUG_SEE) { cout << red << "Unknown/Unimplemented command: " << hex << hexShow(CMD) << hexShow(ARG1) << hexShow(ARG2) << hexShow(ARG3) << white << dec; }
    }
}

//Data
#define printArgs yellow << hex << "C: " << int(CMD) << " A: " << int(ARG1) << "-" << int(ARG2) << "-" << int(ARG3) << dec << white << " "

void ProcessLoop(int index, int stopIndex = 0) {
    wasPlayingNote = false;
    int loopStart = 0;
    int loopEnd = 0;
    int loopCmdCall = 0;
    int cueIni = 0;
    int cueEnd = 0;
    while (true) {
        if (index == stopIndex && stopIndex > 0) { break; }
        uint_fast8_t ARG3 = datamdx[index][channel];
        uint_fast8_t ARG2 = datamdx[index + 1][channel];
        uint_fast8_t ARG1 = datamdx[index + 2][channel];
        uint_fast8_t CMD = datamdx[index + 3][channel];
        if (DEBUG_SEE) { cout << "       " << printArgs; }

        //End of track
        if (CMD == 0xFF || CMD == 0x00) {
            break;
        }
        //Looping commands
        if (CMD == 0xED) {
            loopStart = index + 4;
            loopCmdCall = 0;
            if (DEBUG_SEE) {
                cout << cyan << "LOOP START: " << hex << loopStart << white << dec;
            }
        }
        else if (CMD == 0xEE) {
            loopCmdCall++;
            if (loopCmdCall == 1) {
                loopEnd = index;
                if (DEBUG_SEE) {
                    cout << cyan << "LOOP END: " << hex << loopStart << white << dec;
                }
            }
            else {
                if (DEBUG_SEE) {
                    cout << cyan << "LOOP CLONE BEGIN" << endl;
                }
                for (int loopCmd = loopStart; loopCmd < loopEnd; loopCmd += 4)
                {
                    uint_fast8_t ARG3 = datamdx[loopCmd][channel];
                    uint_fast8_t ARG2 = datamdx[loopCmd + 1][channel];
                    uint_fast8_t ARG1 = datamdx[loopCmd + 2][channel];
                    uint_fast8_t CMD = datamdx[loopCmd + 3][channel];
                    if (DEBUG_SEE) {
                        cout << green << "           INLOOP " << printArgs;
                    }
                    ProcessCMD(loopCmd, CMD, ARG1, ARG2, ARG3);
                    if (DEBUG_SEE) {
                        cout << endl;
                    }
                }
            }
        }
        //CUE LOOPING
        else if (CMD == 0xE7) {
            cueIni = index + 4;
            if (DEBUG_SEE) {
                cout << cyan << "CUE LOOP START: " << dec << cueIni << white;
            }
        }
        else if (CMD == 0xE8) {
            cueEnd = index;
            if (DEBUG_SEE) {
                cout << cyan << "CUE LOOP END: " << dec << loopStart << " REP: " << int(ARG1) << " times" << endl;
                cout << green << "       CUE START" << endl;
            }
            for (int loops = 1; loops < ARG1; loops++)
            {
                ProcessLoop(cueIni, cueEnd);
            }
            if (DEBUG_SEE) {
                cout << green << "       CUE END" << white << endl;
            }
        }
        //Process as normal
        else {
            ProcessCMD(index, CMD, ARG1, ARG2, ARG3);
        }
        index += 4;
        if (DEBUG_SEE) {
            cout << endl;
        }
    }
    cout << green << "Complete: Tr. " << white << dec << channel << " - " << lua_color << "x" << index << " x" << hex << stopIndex << dec << "   " << yellow << midifile.getEventCount(track) << " events" << white << endl;
}

#define FETCH_BYTES if(Pointer > 0x4000) { BYTES = 0xFEFF; } else { uint_fast8_t BY1 = uint_fast8_t(MDX_RAW[Pointer+2]); uint_fast8_t BY2 = uint_fast8_t(MDX_RAW[Pointer+3]); \
BYTES = BY2 + (BY1 << 8); }

//Main
int main() {
    cout << white << R"(mdx2mid v0.1 (https://github.com/johnfortnitekennedyG/mdx2mid)

Convert mdx to midi, based off the original unMDX (https://github.com/metalgeardev/MGS1/blob/master/unMDX.rb)
Write the filename/path and hit enter.

MDX file: )";
    string name;
    cin >> name;
    string path = name;
    char MDX_RAW[0x4000];
    ifstream FILE_M(path, ios::binary);
    if (FILE_M.is_open()) {
        FILE_M.read(MDX_RAW, 0x4000);

        //Start read
        int Songs = MDX_RAW[0];
        cout << white << "Number of tracks: " << Songs << endl;
        for (int Song = 0; Song < Songs; Song++)
        {
            midifile.clear();
            cout << white << "Song " << Song << endl;

            int TR_I = 4 + (Song * 4);
            int Offset = uint_fast8_t(MDX_RAW[TR_I]) + (uint_fast8_t(MDX_RAW[TR_I + 1]) << 8);
            for (int i = 0; i < 16; i++) {
                //Pointer to sound data
                int TR_I = Offset + i * 4;
                int Pointer = uint_fast8_t(MDX_RAW[TR_I]) + (uint_fast8_t(MDX_RAW[TR_I+1]) << 8);
                //Valid
                memset(&datamdx[0][i], 0, 0x4000);
                //Check
                uint_fast16_t BYTES;
                FETCH_BYTES
                cout << white << "Track " << dec << i << ": " << green << "data at " << lua_color << " x" << hex << Pointer << dec << yellow << " BYTES: " << hex << int(BYTES) << "... ";
                //Valid
                if (BYTES != 65279)
                {
                    int INC = 0;
                    while (true)
                    {
                        datamdx[INC][i] = MDX_RAW[Pointer];
                        datamdx[INC + 1][i] = MDX_RAW[Pointer + 1];
                        datamdx[INC + 2][i] = MDX_RAW[Pointer + 2];
                        datamdx[INC + 3][i] = MDX_RAW[Pointer + 3];
                        INC += 4;
                        Pointer += 4;
                        FETCH_BYTES
                        if (BYTES == 65279)
                        {
                            cout << blue << "End of data at " << i << " at " << int(Pointer) << " bytes. Found a FEFF" << white << endl;
                            break;
                        }
                    }

                    //Track
                    starttick = 0;
                    channel = i;
                    divlen = DEFAULT_DIVLEN;
                    midifile.addTimbre(track, 0, channel, 0x40 + i);
                    ProcessLoop(0, 0);
                }
                else
                {
                    cout << red << "No data, skipping" << white << endl;
                }
            }

            //Write file
            midifile.sortTracks();
            midifile.write(name + "_song" + to_string(Song) + ".mid");
        }
    }
    else
    {
        cout << red << "File " << name << " not found." << white << endl;
    }
    return 0;
}