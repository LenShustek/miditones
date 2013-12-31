/*********************************************************************************
*
*   MIDITONES_SCROLL
*
*  Decode a PLAYTUNES bytestream of notes as a time-ordered scroll, sort of like a
*  piano roll with non-uniform time. This is a command-line program with no GUI.
*
*
*  There are two primary uses:
*
*  (1) To debug programming errors that cause some MIDI scripts to sound strange.
*
*  (2) To create a C-program array initialized with the bytestream, but annotated
*      with the original notes. This is semantically the same as the normal output
*      of MIDITONES, but is easier to edit manually. The downside is that the C
*      source code file is much larger.
*
*  In both cases it reads a .bin file that was created from a .mid file by MIDITONES
*  using the -b option.
*
*  For option (1), just invoke the program with the base filename. The output is to the
*  console, which can be directed to a file using the usual >file redirection.
*  Starting with the original midi file "song.mid", say this:
*     miditones -b song
*     miditones_scroll song >song.txt
*  and then "song.txt" will contain the piano roll.
*
*  For option (2), use the -c option to create a <basefile>.c file.
*  Starting with the original midi file:"song,mid", say this:
*      miditones -b song
*      miditones_scroll -c song
*  and then the file "song.c" will contain the PLAYTUNE bytestream code.
*
*----------------------------------------------------------------------------------
*   (C) Copyright 2011,2013, Len Shustek
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of version 3 of the GNU General Public License as
*   published by the Free Software Foundation at http://www.gnu.org/licenses,
*   with Additional Permissions under term 7(b) that the original copyright
*   notice and author attibution must be preserved and under term 7(c) that
*   modified versions be marked as different from the original.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
***********************************************************************************/
/*
* Change log
*  26 February 2011, L.Shustek, V1.0
*     -Initial release
*  29 December 2013, L. Shustek, V1.1
*     - Add a "-c" option to create C code output.
*       Thanks go to mats.engstrom for the idea.
*
*/

#define VERSION "1.1"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <time.h>

/***********  Global variables  ******************/

#define MAX_TONEGENS 6	/* max tone generators to display */
#define SILENT -1
static int gen_status[MAX_TONEGENS];

FILE *infile, *outfile;
unsigned char *buffer, *bufptr;
unsigned long buflen;


unsigned long timenow = 0;
unsigned char cmd, gen;
unsigned char *lastbufptr;
unsigned delay;
bool codeoutput = false;


static char *notename[128] = { // map from MIDI note number to octave and note name
    "-1C ","-1C#","-1D ","-1D#","-1E ","-1F ","-1F#","-1G ","-1G#","-1A ","-1A#","-1B ",
    " 0C "," 0C#"," 0D "," 0D#"," 0E "," 0F "," 0F#"," 0G "," 0G#"," 0A "," 0A#"," 0B ",
    " 1C "," 1C#"," 1D "," 1D#"," 1E "," 1F "," 1F#"," 1G "," 1G#"," 1A "," 1A#"," 1B ",
    " 2C "," 2C#"," 2D "," 2D#"," 2E "," 2F "," 2F#"," 2G "," 2G#"," 2A "," 2A#"," 2B ",
    " 3C "," 3C#"," 3D "," 3D#"," 3E "," 3F "," 3F#"," 3G "," 3G#"," 3A "," 3A#"," 3B ",
    " 4C "," 4C#"," 4D "," 4D#"," 4E "," 4F "," 4F#"," 4G "," 4G#"," 4A "," 4A#"," 4B ",
    " 5C "," 5C#"," 5D "," 5D#"," 5E "," 5F "," 5F#"," 5G "," 5G#"," 5A "," 5A#"," 5B ",
    " 6C "," 6C#"," 6D "," 6D#"," 6E "," 6F "," 6F#"," 6G "," 6G#"," 6A "," 6A#"," 6B ",
    " 7C "," 7C#"," 7D "," 7D#"," 7E "," 7F "," 7F#"," 7G "," 7G#"," 7A "," 7A#"," 7B ",
    " 8C "," 8C#"," 8D "," 8D#"," 8E "," 8F "," 8F#"," 8G "," 8G#"," 8A "," 8A#"," 8B ",
    " 9C "," 9C#"," 9D "," 9D#"," 9E "," 9F "," 9F#"," 9G "
};


/**************  command-line processing  *******************/

void SayUsage(char *programName){
    static char *usage[] = {
        "Display a MIDITONES bytestream",
        "Usage: miditones_scroll <basefilename>",
        " reads <basefilename>.bin",
        "-c option creates an annoted C source file as <basefile>.c",
        ""							};
    int i=0;
    while (usage[i][0] != '\0') fprintf(stderr, "%s\n", usage[i++]);
}

int HandleOptions(int argc,char *argv[]) {
    /* returns the index of the first argument that is not an option; i.e.
    does not start with a dash or a slash*/

    int i,firstnonoption=0;

    /* --- The following skeleton comes from C:\lcc\lib\wizard\textmode.tpl. */
    for (i=1; i< argc;i++) {
        if (argv[i][0] == '/' || argv[i][0] == '-') {
            switch (toupper(argv[i][1])) {
            case 'C':
                codeoutput = true;
                break;
            case 'H':
            case '?':
                SayUsage(argv[0]);
                exit(1);
                /* add more  option switches here */
opterror:
            default:
                fprintf(stderr,"unknown option: %s\n",argv[i]);
                SayUsage(argv[0]);
                exit(4);
            }
        }
        else {
            firstnonoption = i;
            break;
        }
    }
    return firstnonoption;
}


/***************  Found a fatal input file format error  ************************/

void file_error (char *msg, unsigned char *bufptr) {
    unsigned char *ptr;
    fprintf(stderr, "\n---> file format error at position %04X (%d): %s\n", bufptr-buffer, bufptr-buffer, msg);
    /* print some bytes surrounding the error */
    ptr = bufptr - 16;
    if (ptr < buffer) ptr = buffer;
    for (; ptr <= bufptr+16 && ptr < buffer+buflen; ++ptr) fprintf (stderr, ptr==bufptr ? " [%02X]  ":"%02X ", *ptr);
    fprintf(stderr, "\n");
    exit(8);
}

/****************  Output a line for the current status as we start a delay ****************/

// show the current time, status of all the tone generators and the bytestream data that got us here

void print_status(void) {
    if (codeoutput) fprintf (outfile, "/*"); // start comment
    // print the current timestamp
    fprintf (outfile, "%5d %7d.%03d ", delay, timenow/1000, timenow%1000);
    // print the current status of all tone generators
    for (gen=0; gen<MAX_TONEGENS; ++gen) {
        fprintf (outfile, "%6s", gen_status[gen] == SILENT ? " " : notename[gen_status[gen]]);
    }
    // display the hex commands that created these changes
    fprintf (outfile, "   %04X: ", lastbufptr-buffer); // offset
    if (codeoutput) fprintf (outfile, "*/ "); // end comment
    for (; lastbufptr <= bufptr; ++lastbufptr) fprintf (outfile, codeoutput ? "0x%02X," : "%02X ", *lastbufptr);
    fprintf (outfile, "\n");
    lastbufptr = bufptr+1;
}


int countbits (unsigned int bitmap) {
    int count;
    for (count=0; bitmap; bitmap >>= 1)
       count += bitmap & 1;
    return count;
}



/*********************  main loop  ****************************/

int main(int argc,char *argv[]) {
    int argno, i;
    char *filebasename;
#define MAXPATH 80
    char filename[MAXPATH];
    unsigned int tonegens_used;  // bitmap of tone generators used
    unsigned int num_tonegens_used;  // count of tone generators used


    printf("MIDITONES_SCROLL V%s, (C) 2011 Len Shustek\n", VERSION);
    printf("See the source code for license information.\n\n");
    if (argc == 1) { /* no arguments */
        SayUsage(argv[0]);
        return 1;
    }

    /* process options */

    argno = HandleOptions(argc,argv);
    filebasename = argv[argno];

    /* Open the input file */

    strlcpy(filename, filebasename, MAXPATH);
    strlcat(filename, ".bin", MAXPATH);
    infile = fopen(filename, "rb");
    if (!infile) {
        fprintf(stderr, "Unable to open input file %s", filename);
        return 1;
    }

    /* Open the output file */

    if (codeoutput) {
        strlcpy(filename, filebasename, MAXPATH);
        strlcat(filename, ".c", MAXPATH);
        outfile = fopen(filename, "w");
        if (!infile) {
            fprintf(stderr, "Unable to open output file %s", filename);
            return 1;
        }
    }
    else outfile = stdout;

    /* Read the whole input file into memory */

    fseek(infile, 0, SEEK_END); /* find file size */
    buflen = ftell(infile);
    fseek(infile, 0, SEEK_SET);

    buffer = (unsigned char *) malloc (buflen+1);
    if (!buffer) {
        fprintf(stderr, "Unable to allocate %ld bytes for the file", buflen);
        return 1;
    }

    fread(buffer, buflen, 1, infile);
    fclose(infile);
    printf("Processing %s.bin, %ld bytes\n", filebasename, buflen);
    if (codeoutput) {
        time_t rawtime;
                    time (&rawtime);
        fprintf(outfile, "// Playtune bytestream for file \"%s.bin\"", filebasename);
        fprintf(outfile, " created by MIDITONES_SCROLL V%s on %s\n", VERSION, asctime(localtime(&rawtime)));

        fprintf(outfile, "byte PROGMEM score [] = {\n");
    }

    /* Process the commmands sequentially */

    fprintf(outfile, "\n");
    if (codeoutput) fprintf(outfile, "//");
    fprintf(outfile, "duration    time   ");
    for (i=0; i< MAX_TONEGENS; ++i) fprintf(outfile, " gen%-2d", i);
    fprintf(outfile,"        bytestream code\n\n");

    for (gen=0; gen<MAX_TONEGENS; ++gen) gen_status[gen] = SILENT;
    tonegens_used = 0;
    lastbufptr = buffer;

    for (bufptr = buffer; bufptr < buffer+buflen; ++bufptr) {
        cmd = *bufptr;
        if (cmd < 0x80) { //*****  delay
            delay = ((unsigned int)cmd << 8) + *++bufptr;
            print_status(); // tone generator status now
            timenow += delay;  // advance time
        }
        else {
            gen = cmd & 0x0f;
            if (gen >= MAX_TONEGENS) file_error ("too many tone generators used", bufptr);
            cmd = cmd & 0xf0;
            if (cmd == 0x90) {//******  note on
                gen_status[gen] = *++bufptr; // note number
                if (gen_status[gen] > 127) file_error ("note higher than 127", bufptr);
                tonegens_used |= 1<<gen;  // record that we used this generator at least once
            }
            else if (cmd == 0x80) { //******  note off
                if (gen_status[gen] == SILENT) file_error ("tone generator not on", bufptr);
                gen_status[gen] = SILENT;
            }
            else if (cmd == 0xf0) { // end of score
            }
            else file_error ("unknownn command", bufptr);
        }
    }

    delay = 0;
    --bufptr;
    if (codeoutput) --bufptr;  //don't do 0xF0 because don't want the trailing comma
    print_status();  // print final status

    if (codeoutput) {
        fprintf(outfile, " 0xf0};\n");
        num_tonegens_used = countbits(tonegens_used);
        fprintf(outfile, "// This score contains %ld bytes, and %d tone generator%s used.\n",
            buflen, num_tonegens_used, num_tonegens_used == 1 ? " is" : "s are");
    }
    printf ("Done.\n");

}
