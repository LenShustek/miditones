/***************************************************************************************
*
*  MIDITONES_SCROLL
*
*  Decode a PLAYTUNE bytestream of notes as a time-ordered scroll, sort of like
*  a piano roll with non-uniform time. Instrument and volume information, if
*  present, is displayed.
*
* This is a command-line program with no GUI.  There are two primary uses:
*
*  (1) To debug errors that cause some MIDI scripts to sound strange.
*
*  (2) To create a C-program array initialized with the bytestream, but annotated
*      with the original notes. This is semantically the same as the normal output
*      of MIDITONES, but is easier to edit manually. The downside is that the C
*      source code file is much larger.
*
*  In both cases it reads a xxx.bin file that was created from a MIDI file by
*  MIDITONES using the -b option.
*
*  Starting with the midi file "song.mid", do this:
*     miditones -b song
*     miditones_scroll song
*  and then the file "song.txt" will contain the piano roll.
*
*  If instead you do this:
*      miditones -b song
*      miditones_scroll -c song
*  then the file "song.c" will contain the annotated PLAYTUNE bytestream C code.
*
*  Other command-line options besides -c:
*
*    -tn  Up to n tone generators should be displayed. The default
*         is 6 and the maximum is 16.
*
*    -v   Expect the binary file to contain encoded note volume
*         information as generated from Miditones with the -v option,
*         even if there is no file header that says so.
*         The volume is displayed next to each note.
*
*    -vi  Expect volume information to be in the file, but ignore
*         it when creating the display.
*
*    -x   Show notes in hex instead of as octave-noteletter-sharp
*
*    -n   Don't show the bytestream data. (Ignored if -c is specified.)
*
*  For source code to this and related programs, see
*    www.github.com/LenShustek/miditones
*    www.github.com/LenShustek/arduino-playtune
*    www.github.com/LenShustek/Playtune_poll
*    www.github.com/LenShustek/Playtune_poll
*    www.github.com/LenShustek/Playtune_synth
*    www.github.com/LenShustek/Playtune_Teensy
*    www.github.com/LenShustek/ATtiny-playtune
*
*----------------------------------------------------------------------------------------
* The MIT License (MIT)
* Copyright (c) 2011,2013,2015,2016,2019,2021 Len Shustek
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF, OR
* IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
******************************************************************************************/
// formatted with: Astyle -style=lisp -indent=spaces=3 -mode=c
/*
* Change log
*
* 26 February 2011, L.Shustek, V1.0
*     - Initial release
* 29 December 2013, L.Shustek, V1.1
*     - Add a "-c" option to create C code output.
*       Thanks go to mats.engstrom for the idea.
* 04 April 2015, L. Shustek, V1.3
*     - Made friendlier to other compilers,import source of strlcpy and strlcat,
*       and fix various type mismatches that the LCC compiler didn't fret about.
*       Generate "const" data initialization for compatibility with Arduino IDE v1.6.x.
* 5 August 2016, L. Shustek, V1.4
*     - Add -v and -vi options to handle optional volume codes that Miditones can
*       now generate.
*     - Handle notes>127 as percussion, which Miditones can now generate
*     - Make the number of generators be 16 maximum, but the number actually displayed
*       can be specified by the -tn command-line option.
*     - Remove string.h because OS X has macros for strlcpy; thus had to add strlen().
*       It's tough to write even non-GUI code for many environments!
*     - Add casts for where address subtraction is a long, like OS X.
* 6 August 2016, L. Shustek, V1.5
*     - Handle optional instrument change information.
*     - Look for the optional self-describing file header.
* 30 September 2016, L. Shustek, V1.6
*     - Count the number of unnecessary "stop note" commands in the bytestream
* 3 January 2019, Chris van Marle, V1.7
*     - Handle repeat command (0xe0)
* 13 January 2019, L. Shustek, V1.8
*     - Warn about consecutive delays without intervening notes
*     - Mark the location of warnings with a '!' before the hex address
*     - Move the "duration" column (now "delay") to before the
*       bytestream, to better indicate what order events happen in.
*     - Write the output to <basefilename>.txt, if not -c
*     - Decode the header flags
*     - Reformat to condense the code
* 23 April 2021, L. Shustek, V1.9
*     - show a summary at the end of what instruments were used and how many times
*     - show the lowest and highest volume used
* 5 May 2021, L. Shustek, V1.10
*     - add -n option to not print the bytestream data
*     - don't show instrument summary if instrument data wasn't in the bytestream
*/

#define VERSION "1.10"

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <time.h>
#include <inttypes.h>

/***********  Global variables  ******************/

#define MAX_TONEGENS 16         /* max tone generators we could display */

#define SILENT -1               /* "silent note" code */

int gen_note[MAX_TONEGENS];     // the note we're playing, or SILENT
int gen_volume[MAX_TONEGENS];   // the volume we're playing
int gen_instrument[MAX_TONEGENS];       // the instrument we're playing
bool gen_instrument_changed[MAX_TONEGENS];
bool gen_did_stopnote[MAX_TONEGENS]; // did we just do a stopnote?

FILE *infile, *outfile, *infofile;
unsigned char *buffer, *bufptr;
unsigned long buflen;
unsigned int num_tonegens = 6;  // default number of generators
unsigned int max_tonegen_found = 0;
unsigned int notes_skipped = 0;
unsigned int stopnotes_before_startnote = 0;
unsigned int consecutive_delays = 0;
bool warning = false;

unsigned long timenow = 0;
unsigned char cmd, gen;
unsigned char *lastbufptr;
unsigned delay;
bool codeoutput = false;
bool expect_volume = false;
bool ignore_volume = false;
bool showhex = false;
bool showbytestream = true;
bool got_instruments = false;
unsigned max_vol = 0, min_vol = 255;

struct file_hdr_t {             /* what the optional file header looks like */
   char id1;                    // 'P'
   char id2;                    // 't'
   unsigned char hdr_length;    // length of whole file header
   unsigned char f1;            // flag byte 1
   unsigned char f2;            // flag byte 2
   unsigned char num_tgens;     // how many tone generators are used by this score
};
#define HDR_F1_VOLUME_PRESENT 0x80
#define HDR_F1_INSTRUMENTS_PRESENT 0x40
#define HDR_F1_PERCUSSION_PRESENT 0x20


static char *notename[256] = {  /* maximum 5 characters */

   // map from MIDI note number to octave and note name,

   "-1C ", "-1C#", "-1D ", "-1D#", "-1E ", "-1F ", "-1F#", "-1G ", "-1G#", "-1A ", "-1A#", "-1B ",
   " 0C ", " 0C#", " 0D ", " 0D#", " 0E ", " 0F ", " 0F#", " 0G ", " 0G#", " 0A ", " 0A#", " 0B ",
   " 1C ", " 1C#", " 1D ", " 1D#", " 1E ", " 1F ", " 1F#", " 1G ", " 1G#", " 1A ", " 1A#", " 1B ",
   " 2C ", " 2C#", " 2D ", " 2D#", " 2E ", " 2F ", " 2F#", " 2G ", " 2G#", " 2A ", " 2A#", " 2B ",
   " 3C ", " 3C#", " 3D ", " 3D#", " 3E ", " 3F ", " 3F#", " 3G ", " 3G#", " 3A ", " 3A#", " 3B ",
   " 4C ", " 4C#", " 4D ", " 4D#", " 4E ", " 4F ", " 4F#", " 4G ", " 4G#", " 4A ", " 4A#", " 4B ",
   " 5C ", " 5C#", " 5D ", " 5D#", " 5E ", " 5F ", " 5F#", " 5G ", " 5G#", " 5A ", " 5A#", " 5B ",
   " 6C ", " 6C#", " 6D ", " 6D#", " 6E ", " 6F ", " 6F#", " 6G ", " 6G#", " 6A ", " 6A#", " 6B ",
   " 7C ", " 7C#", " 7D ", " 7D#", " 7E ", " 7F ", " 7F#", " 7G ", " 7G#", " 7A ", " 7A#", " 7B ",
   " 8C ", " 8C#", " 8D ", " 8D#", " 8E ", " 8F ", " 8F#", " 8G ", " 8G#", " 8A ", " 8A#", " 8B ",
   " 9C ", " 9C#", " 9D ", " 9D#", " 9E ", " 9F ", " 9F#", " 9G ",

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
   "P120 ", "P121 ", "P122 ", "P123 ", "P124 ", "P125 ", "P126 ", "P127" };

static char *instrumentname[128] = {    /* maximum 6 characters */
   "APiano", "BPiano", "EPiano", "HPiano", "E1Pian", "E2Pian", "Harpsi", "Clavic",
   "Celest", "Glockn", "MusBox", "Vibrap", "Marimb", "Xyloph", "TubBel", "Dulcim",
   "DOrgan", "POrgan", "ROrgan", "COrgan", "dOrgan", "Accord", "Harmon", "TAccor",
   "NyGuit", "StGuit", "JzGuit", "ClGuit", "MuGuit", "OvGuit", "DsGuit", "HaGuit",
   "AcBass", "FiBass", "PiBass", "FrBass", "S1Bass", "S2Bass", "y1Bass", "y2Bass",
   "Violin", "Viola ", "Cello ", "CnBass", "TrStng", "PzStng", "OrHarp", "Timpan",
   "S1Ensb", "S1Ensb", "y1Strg", "y2Strg", "ChAhhs", "VcOohs", "SyVoic", "OrcHit",
   "Trumpt", "Trombn", "Tuba  ", "MuTrum", "FrHorn", "Brass ", "y1Bras", "y2Bras",
   "SopSax", "AltSax", "TenSax", "BarSax", "Oboe  ", "EnHorn", "Basson", "Clarin",
   "Piccol", "Flute ", "Record", "PFlute", "BlBotl", "Shakuh", "Whistl", "Ocarin",
   "Square", "Sawtoo", "Callip", "Chiff ", "Charag", "Voice ", "Fifths", "BassLd",
   "Pad1  ", "Pad2  ", "Pad3  ", "Pad4  ", "Pad5  ", "Pad6  ", "Pad7  ", "Pad 8 ",
   "FX1   ", "FX2   ", "FX3   ", "FX4   ", "FX5   ", "FX6   ", "FX7   ", "FX8   ",
   "Sitar ", "Banjo ", "Shamis", "Koto  ", "Kalimb", "Bagpip", "Fiddle", "Shanai",
   "TnkBel", "Agogo ", "StDrum", "WdBlok", "TaiDrm", "MelTom", "SynDrm", "RevCym",
   "GuitFr", "Breath", "Seashr", "BirdTw", "Phone ", "Copter", "Claps ", "Guns   " };
int instrument_count[128] = { 0 };


/**************  command-line processing  *******************/

void SayUsage (char *programName) {
   static char *usage[] = {
      "Display a MIDITONES bytestream",
      "Usage: miditones_scroll <basefilename>",
      "   reads <basefilename>.bin",
      " -tn displays up to n tone generators",
      " -v expects and displays volume information",
      " -vi expects and ignores volume information",
      " -c  creates an annotated C source file as <basefile>.c",
      " -x  show notes in hex instead of octave/note",
      " -n  don't show the bytestream data",
      "" };
   int i = 0;
   while (usage[i][0] != '\0')
      fprintf (stderr, "%s\n", usage[i++]); }

int HandleOptions (int argc, char *argv[]) {
   /* returns the index of the first argument that is not an option; i.e.
      does not start with a dash or a slash */

   int i, firstnonoption = 0;

   /* --- The following skeleton comes from C:\lcc\lib\wizard\textmode.tpl. */
   for (i = 1; i < argc; i++) {
      if (argv[i][0] == '/' || argv[i][0] == '-') {
         switch (toupper (argv[i][1])) {
         case 'H':
         case '?':
            SayUsage (argv[0]);
            exit (1);
         case 'C':
            codeoutput = true;
            break;
         case 'X':
            showhex = true;
            break;
         case 'N':
            showbytestream = false;
            break;
         case 'T':
            if (sscanf (&argv[i][2], "%d", &num_tonegens) != 1 || num_tonegens < 1
                  || num_tonegens > MAX_TONEGENS)
               goto opterror;
            break;
         case 'V':
            expect_volume = true;
            if (argv[i][2] == '\0')
               break;
            if (toupper (argv[i][2]) == 'I')
               ignore_volume = true;
            else
               goto opterror;
            break;

            /* add more  option switches here */
opterror:
         default:
            fprintf (stderr, "unknown option: %s\n", argv[i]);
            SayUsage (argv[0]);
            exit (4); } }
      else {
         firstnonoption = i;
         break; } }
   return firstnonoption; }

/***************  portable string length  *****************/

int strlength (const char *str) {
   int i;
   for (i = 0; str[i] != '\0'; ++i);
   return i; }

/***************  safe string copy  *****************/

unsigned int strlcpy (char *dst, const char *src, unsigned int siz) {
   char *d = dst;
   const char *s = src;
   unsigned int n = siz;
   if (n != 0) {  /* Copy as many bytes as will fit */
      while (--n != 0) {
         if ((*d++ = *s++) == '\0')
            break; } }
   /* Not enough room in dst, add NUL and traverse rest of src */
   if (n == 0) {
      if (siz != 0)
         *d = '\0';             /* NUL-terminate dst */
      while (*s++); }
   return (s - src - 1);        /* count does not include NUL */
}

/***************  safe string concatenation  *****************/

unsigned int strlcat (char *dst, const char *src, unsigned int siz) {
   char *d = dst;
   const char *s = src;
   unsigned int n = siz;
   unsigned int dlen;
   /* Find the end of dst and adjust bytes left but don't go past end */
   while (n-- != 0 && *d != '\0') d++;
   dlen = d - dst;
   n = siz - dlen;
   if (n == 0) return (dlen + strlength (s));
   while (*s != '\0') {
      if (n != 1) {
         *d++ = *s;
         n--; }
      s++; }
   *d = '\0';
   return (dlen + (s - src));   /* count does not include NUL */
}

/***************  Found a file format error  ************************/

void file_error (char *msg, unsigned char *bufptr) {
   unsigned char *ptr;
   fprintf (outfile, "\n---> file format error at position %04X (%d), time %d.%03d: %s\n",
            (unsigned int) (bufptr - buffer), (unsigned int) (bufptr - buffer), timenow / 1000, timenow % 1000, msg);
   /* print some bytes surrounding the error */
   ptr = bufptr - 16;
   if (ptr < buffer) ptr = buffer;
   for (; ptr <= bufptr + 16 && ptr < buffer + buflen; ++ptr)
      fprintf (outfile, ptr == bufptr ? " [%02X]  " : "%02X ", *ptr);
   fprintf (outfile, "\n"); }

/**************  Output a line for the current status as we start a delay  **************/

// show the current time, status of all the tone generators, and the bytestream data that got us here

void print_status (void) {
   unsigned gen;
   bool any_instr_changed = false;
   for (gen = 0; gen < num_tonegens; ++gen)
      any_instr_changed |= gen_instrument_changed[gen];
   if (any_instr_changed) {
      if (codeoutput)
         fprintf (outfile, "//");
      fprintf (outfile, "%15s", "");
      for (gen = 0; gen < num_tonegens; ++gen) {
         if (gen_instrument_changed[gen]) {
            gen_instrument_changed[gen] = false;
            fprintf (outfile, "%6s", instrumentname[gen_instrument[gen]]); }
         else
            fprintf (outfile, "%6s", "");
         if (expect_volume && !ignore_volume)
            fprintf (outfile, "%5s", ""); }
      fprintf (outfile, "\n"); }

   if (codeoutput)
      fprintf (outfile, "/*");  // start comment
   // print the current timestamp
   fprintf (outfile, "%7d.%03d ", timenow / 1000, timenow % 1000);
   // print the current status of all tone generators
   for (gen = 0; gen < num_tonegens; ++gen) {
      if (showhex && gen_note[gen] != SILENT)
         fprintf(outfile, " 0x%02X ", gen_note[gen]);
      else fprintf (outfile, "%6s", gen_note[gen] == SILENT ? " " : notename[gen_note[gen]]);
      if (expect_volume && !ignore_volume)
         if (gen_note[gen] == SILENT)
            fprintf (outfile, "     ");
         else
            fprintf (outfile, " v%-3d", gen_volume[gen]); }
   // display the hex commands that created these changes
   fprintf (outfile, "%3u.%03u %c", delay/1000, delay%1000, warning ? '!' : ' ');
   if (showbytestream) fprintf (outfile, "%04X: ", (unsigned int) (lastbufptr - buffer));
   warning = false;
   if (codeoutput)
      fprintf (outfile, "*/ "); // end comment
   if (showbytestream) for (; lastbufptr <= bufptr; ++lastbufptr)
         fprintf (outfile, codeoutput ? "0x%02X," : "%02X ", *lastbufptr);
   fprintf (outfile, "\n");
   lastbufptr = bufptr + 1; }

int countbits (unsigned int bitmap) {
   int count;
   for (count = 0; bitmap; bitmap >>= 1)
      count += bitmap & 1;
   return count; }


/*********************  main loop  ****************************/

int main (int argc, char *argv[]) {
   int argno;
   char *filebasename;
#define MAXPATH 80
   char filename[MAXPATH];

   printf ("MIDITONES_SCROLL V%s, (C) 2011,2019 Len Shustek\n", VERSION);
   if (argc == 1) {             /* no arguments */
      SayUsage (argv[0]);
      return 1; }

   argno = HandleOptions (argc, argv);   /* process options */
   if (codeoutput) showbytestream = true;
   filebasename = argv[argno];

   strlcpy (filename, filebasename, MAXPATH);   // Open the input file
   strlcat (filename, ".bin", MAXPATH);
   infile = fopen (filename, "rb");
   if (!infile) {
      fprintf (stderr, "Unable to open input file %s", filename);
      return 8; }
   printf("Opening %s\n", filename);

   strlcpy (filename, filebasename, MAXPATH); // open the output file
   strlcat (filename, codeoutput ? ".c" : ".txt", MAXPATH);
   outfile = fopen (filename, "w");
   if (!outfile) {
      fprintf (stderr, "Unable to open output file %s", filename);
      return 8; }
   printf("Creating %s\n", filename);
   infofile = codeoutput ? stdout : outfile;

   /* Read the whole input file into memory */

   fseek (infile, 0, SEEK_END); /* find file size */
   buflen = ftell (infile);
   fseek (infile, 0, SEEK_SET);
   buffer = (unsigned char *) malloc (buflen + 1);
   if (!buffer) {
      fprintf (stderr, "Unable to allocate %ld bytes for the file", buflen);
      return 8; }
   fread (buffer, buflen, 1, infile);
   fclose (infile);

   /* write the prologue */

   time_t rawtime;
   time (&rawtime);
   printf ("Reading %s.bin with %ld bytes\n", filebasename, buflen);
   if (!codeoutput) {
      fprintf(outfile, "MIDITONES_SCROLL V%s on %s", VERSION, asctime(localtime(&rawtime)));
      fprintf(outfile, "command line: ");
      for (int i = 0; i < argc; i++) fprintf(outfile, "%s ", argv[i]);
      fprintf(outfile, "\n");
      fprintf(outfile, "reading %s.bin with %ld bytes\n", filebasename, buflen);
      if (num_tonegens < MAX_TONEGENS) fprintf(outfile, "displaying only %d tone generators.\n", num_tonegens); }
   else {
      fprintf (outfile, "// Playtune bytestream for file \"%s.bin\"", filebasename);
      fprintf (outfile, " created by MIDITONES_SCROLL V%s on %s\n", VERSION,
               asctime (localtime (&rawtime)));
      fprintf (outfile, "const byte PROGMEM score [] = {\n"); }
   bufptr = lastbufptr = buffer;

   /* Check for the optional self-describing file header */

   struct file_hdr_t *hdrptr = (struct file_hdr_t *) buffer;
   if (buflen > sizeof (struct file_hdr_t) && hdrptr->id1 == 'P' && hdrptr->id2 == 't') {
      fprintf(infofile, "\nfound Pt self-describing file header with flags %02X %02X\n", hdrptr->f1, hdrptr->f2);
      fprintf(infofile, "  number of tone generators used: %d\n", hdrptr->num_tgens);
      if (hdrptr->f1 & HDR_F1_VOLUME_PRESENT)      fprintf(infofile, "  volume levels are present\n");
      if (hdrptr->f1 & HDR_F1_INSTRUMENTS_PRESENT) fprintf(infofile, "  instrument changes are present\n");
      if (hdrptr->f1 & HDR_F1_PERCUSSION_PRESENT)  fprintf(infofile, "  percussion is encoded as notes 128 to 255\n");

      expect_volume = hdrptr->f1 & HDR_F1_VOLUME_PRESENT;
      bufptr += hdrptr->hdr_length;
      lastbufptr = bufptr;
      if (codeoutput) {
         fprintf (outfile, "'P','t', 6, 0x%02X, 0x%02X, %2d, // (Playtune file header)\n",
                  hdrptr->f1, hdrptr->f2, hdrptr->num_tgens); } }

   /* Do the titles */

   fprintf (outfile, "\n");
   if (codeoutput) fprintf (outfile, "//");
   fprintf (outfile, "       time   ");
   for (unsigned i = 0; i < num_tonegens; ++i)
      fprintf (outfile, expect_volume && !ignore_volume ? "   gen%-5d" : " gen%-2d", i);
   fprintf(outfile, "delay");
   if (showbytestream) fprintf(outfile, "  addr  bytestream code");
   fprintf(outfile, "\n\n");
   for (gen = 0; gen < num_tonegens; ++gen)
      gen_note[gen] = SILENT;

   unsigned tonegens_used = 0;
   bool gotcommand = true;

   /* Process the commmands in order */

   for (; bufptr < buffer + buflen; ++bufptr) {
      cmd = *bufptr;
      if (cmd < 0x80) {         /*  delay  */
         if (!gotcommand) {
            ++consecutive_delays;
            warning = true; }
         gotcommand = false;
         delay = ((unsigned int)cmd << 8) + *++bufptr;
         print_status();       // tone generator status now
         timenow += delay;      // advance time
         for (gen = 0; gen < MAX_TONEGENS; ++gen)
            gen_did_stopnote[gen] = false; }
      else if (cmd != 0xf0 && cmd != 0xe0) {   /* a command */
         gotcommand = true;
         gen = cmd & 0x0f;
         if (gen > max_tonegen_found)
            max_tonegen_found = gen;
         cmd = cmd & 0xf0;
         if (cmd == 0x90) {     /*  note on  */
            gen_note[gen] = *++bufptr;  // note number
            tonegens_used |= 1 << gen;  // record that we used this generator at least once
            ++instrument_count[gen_instrument[gen]]; // count a use of this instrument
            if (gen_did_stopnote[gen]) { // unnecesary stop note
               ++stopnotes_before_startnote;
               warning = true; }
            if (expect_volume) {
               unsigned volume = gen_volume[gen] = *++bufptr;     // volume
               if (volume > max_vol) max_vol = volume;
               if (volume < min_vol) min_vol = volume; }
            if (gen >= num_tonegens) ++notes_skipped; // won't be displaying this note
         }
         else if (cmd == 0x80) {        /*  note off  */
            if (gen_note[gen] == SILENT)
               file_error("tone generator not on", bufptr);
            gen_note[gen] = SILENT;
            gen_did_stopnote[gen] = true; }
         else if (cmd == 0xc0) {        /* change instrument */
            got_instruments = true;
            gen_instrument[gen] = *++bufptr & 0x7f;
            gen_instrument_changed[gen] = true; }
         else {
            file_error("unknown command", bufptr);
            fprintf(stderr, "*** unknown command in input bytestream; see output file\n");
            exit(8); } } }

   /*  Do the final cleanup  */

   delay = 0;
   --bufptr;
   if (codeoutput)
      --bufptr;                 //don't do 0xf0 for code, because we don't want the trailing comma
   print_status ();             // print final status
   if (codeoutput) {
      fprintf (outfile, " 0x%02x};\n", *(bufptr+1) & 0xf0);
      unsigned num_tonegens_used = countbits (tonegens_used);
      fprintf (outfile, "// This score contains %ld bytes, and %d tone generator%s used.\n",
               buflen, num_tonegens_used, num_tonegens_used == 1 ? " is" : "s are"); }
   else
      fprintf (outfile, "\n");

   fprintf (infofile, "At most %u tone generators were used.\n", max_tonegen_found + 1);
   if (notes_skipped)
      fprintf (infofile, "%u notes were not displayed because we were told to show only %u generators.\n",
               notes_skipped, num_tonegens);
   fprintf (infofile, "%u stopnote commands were unnecessary.\n", stopnotes_before_startnote);
   fprintf (infofile, "%u consecutive delays could have been merged.\n", consecutive_delays);
   if (stopnotes_before_startnote + consecutive_delays > 0) fprintf (infofile, "(Those locations are marked with \"!\")\n");
   if (got_instruments) {
      fprintf(infofile, "instruments used:\n");
      for (int i = 0; i < 128; ++i)
         if (instrument_count[i]) {
            fprintf(infofile, " %s (%3d, 0x%02X) %7d\n", instrumentname[i], i, i, instrument_count[i]); } }
   if (expect_volume)
      fprintf(infofile, "volume ranged from %d to %d\n", min_vol, max_vol);
   printf ("Done.\n"); }
