/*********************************************************************************
*
*  MIDITONES_SCROLL
*
*  Decode a PLAYTUNE bytestream of notes as a time-ordered scroll, sort of like a
*  piano roll with non-uniform time. This is a command-line program with no GUI.
*
*
*  There are two primary uses:
*
*  (1) To debug errors that cause some MIDI scripts to sound strange.
*
*  (2) To create a C-program array initialized with the bytestream, but annotated
*  with the original notes. This is semantically the same as the normal output
*  of MIDITONES, but is easier to edit manually. The downside is that the C
*  source code file is much larger.
*
*  In both cases it reads a xxx.bin file that was created from a MIDI file by
*  MIDITONES using the -b option.
*
*
*  For use case (1), just invoke the program with the base filename. The output is
*  to the console, which can be directed to a file using the usual >file redirection.
*  For example, starting with the original midi file "song.mid", say this:
*
*     miditones -b song
*     miditones_scroll song >song.txt
*
*  and then the file "song.txt" will contain the piano roll.
*
*
*  For use case (2), use the -c option to create a <basefile>.c file.
*  For example, starting with the original midi file "song.mid", say this:
*
*     miditones -b song
*     miditones_scroll -c song
*
*  and then the file "song.c" will contain the PLAYTUNE bytestream C code.
*
*
*  Other command-line options:
*
*    -tn  says that up to n tone generators should be displayed. The default
*         is 6 and the maximum is 16.
*
*    -v   says that we expect the binary file to contain encoded note volume
*         information as generated from Miditones with the -v option. That
*         volume information is displayed next to each note.
*
*    -vi  says that we expect volume information to be in the file, but we
*         should ignore it when creating the display.
*
*
*  For source code to this and related programs, see
*     https://github.com/LenShustek/arduino-playtune
*     https://github.com/LenShustek/miditones
*
*----------------------------------------------------------------------------------
*   (C) Copyright 2011,2013,2016 Len Shustek
*
*   This program is free software. You can redistribute it and/or modify
*   it under the terms of version 3 of the GNU General Public License as
*   published by the Free Software Foundation at www.gnu.org/licenses,
*   with Additional Permissions under term 7(b) that the original copyright
*   notice and author attibution must be preserved and under term 7(c) that
*   modified versions be marked as different from the original.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY,without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
***********************************************************************************/
/*
* Change log
*
* 26 February 2011, L.Shustek, V1.0
*     - Initial release
* 29 December 2013, L.Shustek, V1.1
*     - Add a "-c" option to create C code output.
*       Thanks go to mats.engstrom for the idea.
* 04 April 2015, L. Shustek, V1.3
*     - Made friendlier to other compilers: import source of strlcpy and strlcat,
*       and fix various type mismatches that the LCC compiler didn't fret about.
*       Generate "const" data initialization for compatibility with Arduino IDE v1.6.x.
* 5 August 2016, L. Shustek, V1.4
*     - Add -v and -vi options to handle optional volume codes that Miditones can
*       now generate.
*     - Handle notes>127 as percussion, which Miditones can now generate
*     - Make the number of generators be 16 maximum, but the number actually displayed
*       can be specified by the -tn command-line option.
*     - Remove string.h because OS X has macros for strlcpy; thus had to add strlen().
*       (It's tough to write even non-GUI code for many environments!)
*     - Add casts for where address subtraction is a long, like OS X.
*/

#define VERSION "1.4"

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <time.h>
#include <inttypes.h>


/***********  Global variables  ******************/

#define MAX_TONEGENS 16		/* max tone generators we could display */

int gen_note[MAX_TONEGENS];   // the note we're playing, or SILENT
#define SILENT -1
int gen_volume[MAX_TONEGENS]; // the volume we're playing

FILE *infile, *outfile;
unsigned char *buffer, *bufptr;
unsigned long buflen;
unsigned int num_tonegens = 6; // default number of generators
unsigned int max_tonegen_found = 0;
unsigned int notes_skipped;

unsigned long timenow = 0;
unsigned char cmd, gen;
unsigned char *lastbufptr;
unsigned delay;
bool codeoutput = false;
bool expect_volume = false;
bool ignore_volume = false;

static char *notename[256] = {

    // map from MIDI note number to octave and note name,

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
    " 9C "," 9C#"," 9D "," 9D#"," 9E "," 9F "," 9F#"," 9G ",

    // or to channel 9 percussion notes as relocated by Miditones to notes 128..255

    "P000 ", "P001 ", "P002 ", "P003 ", "P004 ", "P005 ", "P006 ", "P007 ",
    "P008 ", "P009 ", "P010 ", "P011 ", "P012 ", "P013 ", "P014 ", "P015 ",
    "P016 ", "P017 ", "P018 ", "P019 ", "P020 ", "P021 ", "P022 ", "P023 ",
    "P024 ", "P025 ", "P026 ", "Laser", "Whip ", "ScrPu", "ScrPl", "Stick",
    "MetCk", "P033 ", "MetBl", "BassD", "KickD", "SnaSt", "SnaD ", "Clap ",
    "ESnaD", "FTom2", "HHatC", "FTom1", "HHatF", "LTom ", "HHatO", "LMTom",
    "HMTom", "CrCym", "HTom ", "RiCym", "ChCym", "RiBel", "Tamb ", "SpCym",
    "CowBl", "CrCym", "VSlap", "RiCym", "HBong", "LBong", "CongD", "Conga",
    "Tumba", "HTimb", "LTimb", "HAgog", "LAgog", "Cabas", "Marac", "SWhis",
    "LWhis", "SGuir", "LGuir", "Clave", "HWood", "LWood", "HCuic", "LCuic",
    "MTria", "OTria", "Shakr", "Sleig", "BelTr", "Casta", "SirdD", "Sirdu",
    "P088 ", "P089 ", "P090 ", "SnDmR", "OcDrm", "SmDrB", "P094 ", "P095 ",
    "P096 ", "P097 ", "P098 ", "P099 ", "P100 ", "P101 ", "P102 ", "P103 ",
    "P104 ", "P105 ", "P106 ", "P107 ", "P108 ", "P109 ", "P110 ", "P111 ",
    "P112 ", "P113 ", "P114 ", "P115 ", "P116 ", "P117 ", "P118 ", "P119 ",
    "P120 ", "P121 ", "P122 ", "P123 ", "P124 ", "P125 ", "P126 ", "P127"
};


/**************  command-line processing  *******************/

void SayUsage(char *programName){
    static char *usage[] = {
        "Display a MIDITONES bytestream",
        "Usage: miditones_scroll <basefilename>",
        "   reads <basefilename>.bin",
        " -tn displays up to n tone generators",
        " -v expects and displays volume information",
        " -vi expects and ignores volume information",
        " -c option creates an annotated C source file as <basefile>.c",
        ""																				};
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
            case 'H':
            case '?':
                SayUsage(argv[0]);
                exit(1);
            case 'C':
                codeoutput = true;
                break;
            case 'T':
                if (sscanf(&argv[i][2],"%d",&num_tonegens) != 1 || num_tonegens <1 || num_tonegens > MAX_TONEGENS) goto opterror;
                printf("Displaying %d tone generators.\n", num_tonegens);
                break;
            case 'V':
                expect_volume = true;
                if (argv[i][2] == '\0')                    break;
                if (toupper(argv[i][2]) == 'I') ignore_volume = true;
                else goto opterror;
                break;

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

/***************  portable string length  *****************/

int strlength(const char *str) {
    int i;
    for (i=0; str[i] != '\0'; ++i) ;
    return i;
}

/***************  safe string copy  *****************/

unsigned int strlcpy(char *dst, const char *src, unsigned int 	siz) {
    char       *d = dst;
    const char *s = src;
    unsigned int      n = siz;
    /* Copy as many bytes as will fit */
    if (n != 0)	{
        while (--n != 0) {
            if ((*d++ = *s++) == '\0')                break;
        }
    }
    /* Not enough room in dst, add NUL and traverse rest of src */
    if (n == 0)	{
        if (siz != 0)            *d = '\0';          /* NUL-terminate dst */
        while (*s++)            ;
    }
    return (s - src - 1);       /* count does not include NUL */
}

/***************  safe string concatenation  *****************/

unsigned int strlcat(char *dst, const char *src, unsigned int siz) {
    char       *d = dst;
    const char *s = src;
    unsigned int      n = siz;
    unsigned int      dlen;
    /* Find the end of dst and adjust bytes left but don't go past end */
    while (n-- != 0 && *d != '\0')        d++;
    dlen = d - dst;
    n = siz - dlen;
    if (n == 0)        return (dlen + strlength(s));
    while (*s != '\0') {
        if (n != 1) 	{
            *d++ = *s;
            n--;
        }
        s++;
    }
    *d = '\0';
    return (dlen + (s - src));  /* count does not include NUL */
}


/***************  Found a fatal input file format error  ************************/

void file_error (char *msg, unsigned char *bufptr) {
    unsigned char *ptr;
    fprintf(stderr, "\n---> file format error at position %04X (%d): %s\n",
        (unsigned int)(bufptr-buffer), (unsigned int)(bufptr-buffer), msg);
    /* print some bytes surrounding the error */
    ptr = bufptr - 16;
    if (ptr < buffer) ptr = buffer;
    for (; ptr <= bufptr+16 && ptr < buffer+buflen; ++ptr) fprintf (stderr, ptr==bufptr ? " [%02X]  ":"%02X ", *ptr);
    fprintf(stderr, "\n");
    exit(8);
}

/**************  Output a line for the current status as we start a delay  **************/

// show the current time, status of all the tone generators, and the bytestream data that got us here

void print_status(void) {
    if (codeoutput) fprintf (outfile, "/*"); // start comment
    // print the current timestamp
    fprintf (outfile, "%5d %7d.%03d ", delay, timenow/1000, timenow%1000);
    // print the current status of all tone generators
    for (gen=0; gen<num_tonegens; ++gen) {
        fprintf (outfile, "%6s", gen_note[gen] == SILENT ? " " : notename[gen_note[gen]]);
        if (expect_volume && !ignore_volume)            if (gen_note[gen] == SILENT) fprintf (outfile, "     ");
            else fprintf (outfile, " v%-3d", gen_volume[gen]);
    }
    // display the hex commands that created these changes
    fprintf (outfile, "   %04X: ", (unsigned int)(lastbufptr-buffer)); // offset
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

    printf("MIDITONES_SCROLL V%s, (C) 2011,2016 Len Shustek\n", VERSION);
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

    /* Create the output file */

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
        fprintf(outfile, "const byte PROGMEM score [] = {\n");
    }

    /* Do the titles */

    fprintf(outfile, "\n");
    if (codeoutput) fprintf(outfile, "//");
    fprintf(outfile, "duration    time   ");
    for (i=0; i< num_tonegens; ++i)
        fprintf(outfile, expect_volume && !ignore_volume ? "   gen%-5d" : " gen%-2d", i);
    fprintf(outfile,"        bytestream code\n\n");
    for (gen=0; gen<num_tonegens; ++gen)
        gen_note[gen] = SILENT;
    tonegens_used = 0;
    lastbufptr = buffer;

    /* Process the commmands sequentially */

    for (bufptr = buffer; bufptr < buffer+buflen; ++bufptr) {
        cmd = *bufptr;
        if (cmd < 0x80) { //*****  delay
            delay = ((unsigned int)cmd << 8) + *++bufptr;
            print_status(); // tone generator status now
            timenow += delay;  // advance time
        }
        else if (cmd != 0xf0) { // a note command
            gen = cmd & 0x0f;
            if (gen > max_tonegen_found)                max_tonegen_found = gen;
            cmd = cmd & 0xf0;
            if (cmd == 0x90) { //******  note on
                gen_note[gen] = *++bufptr; // note number
                tonegens_used |= 1<<gen;  // record that we used this generator at least once
                if (expect_volume)                    gen_volume[gen] = *++bufptr; // volume
                if (gen >= num_tonegens)                    ++notes_skipped; // won't be displaying this note
            }
            else if (cmd == 0x80) { //******  note off

                if (gen_note[gen] == SILENT) file_error ("tone generator not on", bufptr);
                gen_note[gen] = SILENT;
            }
            else file_error ("unknown command", bufptr);
        }
    }


    /*  Do the final cleanup  */

    delay = 0;
    --bufptr;
    if (codeoutput) --bufptr;  //don't do 0xF0 for code, because we don't want the trailing comma
    print_status();  // print final status
    if (codeoutput) {
        fprintf(outfile, " 0xf0};\n");
        num_tonegens_used = countbits(tonegens_used);
        fprintf(outfile, "// This score contains %ld bytes, and %d tone generator%s used.\n",
            buflen, num_tonegens_used, num_tonegens_used == 1 ? " is" : "s are");
    }
    printf("\nAt most %u tone generators were used.\n", max_tonegen_found+1);
    if (notes_skipped)        printf("%u notes were not displayed because we were told to show only %u generators.\n", notes_skipped, num_tonegens);
    printf ("Done.\n");
}

