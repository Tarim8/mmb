//
// Copyright 2013,2014,2015 Tarim
//
// Poll is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// Poll is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public License
// along with Poll.  If not, see <http://www.gnu.org/licenses/>.
//

//
// Poll waits for interrupts on character devices, named pipes and
// /sys/class/gpio/gpioX/value style files.  It then echoes the updated
// contents of the file, optionally with filename and a timestamp.
//

//
// Philosophy:
// As poll is generally responding to real-time events; it tries to be
// as time efficient as possible.  String handling is all C style rather
// than C++ Strings. There is no mallocing of space (there may be some
// within library routines but we have no control of this) and no dynamic
// creation of class instances.
//
// Input "lines" are read into fixed length, pre-allocated buffers and
// transferred to an output buffer for sending on its way in a, hopefully,
// atomic operation.
//



#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <algorithm>



const unsigned int bufferSize = 1024;
const unsigned int bufferCount = 3;
typedef uint64_t long_time_t;
const long_time_t FOREVER = -1;



//
// Output class
//
// This is a singleton class (could probably be a namespace) to
// copy data to a buffer and then, hopefully, atomically write it
// all to the output stream.  Should be atomic if outputSize is
// less then PIPE_BUFSIZE - though this isn't checked.
//
class Output {
private:
    static const unsigned int outputSize = 2*bufferSize;
    char outputBuf[outputSize];
    unsigned int outputPos;

public:
    //
    // start a new line of output
    //
    void start() {
        outputPos = 0;
    };



    //
    // Add a string to the output buffer
    //
    const int string( const char *str ) {
        const unsigned int len = strlen( str );

        if( len < outputSize - outputPos ) {
            strcpy( outputBuf + outputPos, str );
            outputPos += len;

        } else {
            outputPos = outputSize;
        }

        return len;
    };



    //
    // Add a long real to the output buffer
    //
    const int real( const long_time_t val ) {
        const int len = snprintf( outputBuf + outputPos, outputSize - outputPos, "%llu", val );

        if( len >= 0 ) {
            outputPos += len;

        } else {
            outputPos = outputSize;
        }

        return len;
    };



    //
    // flush the output buffer to stdout
    //
    void flush() {
        if( write( STDOUT_FILENO, outputBuf, outputPos ) < 0 ) {
            perror( "write" );
            exit( 1 );
        }
    };

} output;



//
// fds provided by poll.h
//
// nfds_t                       // fdsIndex type
// fds[fdsIndex].fd             // File descriptor
// fds[fdsIndex].events         // Events we're interested in
// fds[fdsIndex].revents        // Events that happened
//



//
// PFDOption class
//
// Poll file descriptor options associated with output
//
class PFDOption {
public:
    char *format;               // Start of format string
    char *formatEnd;            // End of format string
    char *delimiters;           // Line end delimiters
    long_time_t debounce;       // Time to wait for value to settle
    bool duplicates;            // Allow duplicate values to be sent onwards

    void init() {
        format = (char *)"V\n";
        formatEnd = format + strlen( format );
        delimiters = (char *)"\n";
        debounce = 0;
        duplicates = true;
    };
};



//
// Poll file descriptor class
//
// Handle a file descriptor and all its associated options
//

const nfds_t pfdMax = 64;       // number of file descriptors
nfds_t pfdCount = 0;
struct pollfd pfd[pfdMax];      // structure used by poll.h

class PollFileDescriptor {
public:
    char *pathname;
    struct stat status;

    nfds_t index;

    int openMode;
    short pollEvents;
    bool reseek;

    long_time_t readTime;

    PFDOption option;

    //
    // buffer system is coded with the kind of efficiancy that caused the
    // heartbleed bug - be warned!
    //
    char buffer[bufferCount][bufferSize];
    uint8_t bufferIndex;
    char *readPtr;      // start of buffer unless previously data unfinished
    char *printPtr;     // last printed data (usually in a different buffer)
    char *heldPtr;      // held data from debounce



    //
    // check status and set modes
    //
    void statpfd() {
        if( stat( pathname, &status ) < 0 ) {
            perror( pathname );
            exit( 1 );
        }

        // system file e.g. /sys/class/gpio/gpioN/value
        if( S_ISREG( status.st_mode ) ) {
            openMode = O_RDONLY;
            pollEvents = POLLPRI;
            reseek = true;

        // character device e.g. /dev/ttyUSB0
        } else if( S_ISCHR( status.st_mode ) ) {
            openMode = O_RDONLY | O_NONBLOCK;
            pollEvents = POLLIN;
            reseek = false;

        // fifo e.g. /tmp/fifo
        } else if( S_ISFIFO( status.st_mode ) ) {
            openMode = O_RDWR | O_NONBLOCK;
            pollEvents = POLLIN;
            reseek = false;

        } else {
            fprintf( stderr, "Invalid device: %s\n", pathname );
            exit( 1 );
        }
    };



    //
    // open the file descriptor and set the modes
    //        
    void openpfd( const nfds_t pfdi, char *fname, PFDOption *argOption ) {
        index = pfdi;
        pathname = fname;

        statpfd();

        pfd[index].fd = open( pathname, openMode );
        if( pfd[index].fd < 0 ) {
            perror( pathname );
            exit( 1 );
        }
        pfd[index].events = pollEvents;

        readTime = 0;
        option = *argOption;

        printPtr = &(buffer[0][0]);
        *printPtr = '\0';
        bufferIndex = 1;
        readPtr = &(buffer[1][0]);
        heldPtr = NULL;
    };



    //
    // Check readable data, debounce and held data for uniqueness
    //
    long_time_t checkpfd( long_time_t now ) {

        // debouncing is done by holding back data instead of printing
        // which appears within debounce time.  If device is quiet for debounce
        // time then print the held data if it's different to the last output
        // otherwise silently discard it
        //
        // This method gives quickest response to any change on the device while
        // dropping bounce information and still reporting final state in case
        // it differs

        bool nobounce = now - readTime >= option.debounce;

        // held data from a debounce might need printing
        if( heldPtr && nobounce ) {
            printpfd( heldPtr );
            heldPtr = NULL;
        }

        // shouldn't get an end of file - but in case we do
        if( pfd[index].revents & POLLHUP ) {
            fprintf( stderr, "EOF: %s\n", pathname );
            pfd[index].fd = -1;                // disable polling fd
        }

        // if there's data to read then parse it
        if( (pfd[index].revents & pfd[index].events) && readpfd() ) {
            readTime = now;
            char *startPtr = &(buffer[bufferIndex][0]);
            char *eolPtr;

            // step through the lines in the buffer
            while( (eolPtr = strpbrk( startPtr, option.delimiters )) ) {
                *eolPtr = '\0';         // overwrite with C string delimiter

                if( nobounce ) {
                    printpfd( startPtr );
        
                } else {
                    heldPtr = startPtr; // hold instead of print if bouncing
                }

                startPtr = eolPtr + 1;
                nobounce = option.debounce == 0;   // in case >1 line in buffer
            }

            // find a buffer which doesn't have printPtr or heldPtr in it
            uint8_t buffersUsed = bufferOf( printPtr ) | bufferOf( heldPtr );
            for( bufferIndex = 0; buffersUsed & (1<<bufferIndex); ++bufferIndex ) {
                // shouldn't ever happen
                if( bufferIndex >= bufferCount ) {
                    fprintf( stderr, "No free buffers: %s\n", pathname );
                    exit( 1 );
                }
            }
            readPtr = &(buffer[bufferIndex][0]);

            // copy partial line to beginning of buffer
            unsigned int remainder = strlen( startPtr );
            if( remainder ) {
                strncpy( readPtr, startPtr, remainder );
                readPtr += remainder;
            }
        }

        // if held data then return the time we need to be checked again
        return heldPtr ? option.debounce - (now - readTime) : FOREVER;
    };



    //
    // bufferOf - returns bit set of buffer which pointer is in
    //
    uint8_t bufferOf( char *ptr ) {
        if( ptr ) {
            return 1 << ((ptr - &(buffer[0][0])) / bufferSize);
        } else {
            return 0;
        }
    };



    //
    // read from a pfd
    //
    unsigned int readpfd() {
        int charCount;
        if( reseek ) {
            lseek( pfd[index].fd, 0, SEEK_SET );
        }

        charCount = read( pfd[index].fd, readPtr, bufferSize-2 + &(buffer[bufferIndex][0]) - readPtr );
        if( charCount < 0 ) {
            if( errno == EAGAIN || errno == EWOULDBLOCK ) {
                return 0;

            } else {
                perror( "read" );
                exit( 1 );        // or should we set pfd[index].fd to -1?
            }
        }

        readPtr += charCount;
        // if not enough room in buffer then pretend it was a complete line
        if( readPtr == bufferSize-2 + &(buffer[bufferIndex][0]) ) {
            *(readPtr++) = *option.delimiters;
            ++charCount;
        }
        *readPtr = '\0';
        return charCount;
    };



    //
    // print and update printPtr to point to what we just printed
    //
    void printpfd( char *startPtr ) {
        if( *startPtr && ( option.duplicates || strcmp( printPtr, startPtr ) != 0 ) ) {
            output.start();
            char *formatPtr = option.format;

            while( formatPtr < option.formatEnd ) {
                switch( *formatPtr ) {
                case '+':
                    break;
                case 'V':
                    output.string( startPtr );
                    break;
                case 'P':
                    output.string( pathname );
                    break;
                case 'T':
                    output.real( readTime );
                    break;
                default:
                    output.string( "%" );
                    break;
                }

                formatPtr += output.string( formatPtr + 1 ) + 2;
            }

            output.flush();
            printPtr = startPtr;
        }
    };
} pollFileDescriptor[pfdMax];




char lookupChr[] = "abfnrtv\"\'\\123456789";
char translateChr[] = "\a\b\f\n\r\t\v\"\'\\\001\002\003\004\005\006\007\010\011";

//
// Argument class
//
// Parse the arguments to set up the poll file descriptors
//
// Arguments:
// filename             File to monitor
// +format              Format for output content: %V, time: %T, file: %P
// -debounce N          Number of milliseconds to ignore results
// --unique             Discard repeated results (useful with debounce)
// --duplicates         Report repeated results
// --delimiters         Characters which delimit input lines
// --default            Reset default options for subsequent files
// --timeout N          Not implemented yet - will allow watchdog
//

char formatChr[] = "VPT";

class Argument {
public:
    PFDOption option;

    Argument() {
        option.init();
    };

    //
    // parse the backslashes in a string
    //
    char *parseString( char *arg ) {
        char *ptr;
        for( ptr = arg; *ptr; ++ptr ) {
            if( *ptr == '\\' ) {
                char *find = strchr( lookupChr, ptr[1] );
                if( find ) {
                    // replace with translated character and shift everything up
                    *ptr = translateChr[ find - lookupChr ];
                    strcpy( ptr+1, ptr+2 );
                }
            }
        }
        return arg;
    };



    //
    // parse a format string
    //
    char *parseFormat( char *format ) {
        // +%V is same as V, so shift everything up for efficiency
        if( format[1] == '%' && strlen( format ) >= 3 && strchr( formatChr, format[2] ) ) {
            strcpy( format, format+2 );
        }

        char *ptr;
        for( ptr = format; *ptr; ++ptr ) {
            if( *ptr == '%' ) {
                if( ptr[1] == '%' ) {
                    strcpy( ptr+1, ptr+2 );
                } else if( strchr( formatChr, ptr[1] ) ) {
                    *ptr = '\0';
                }
            }
        }
        return ptr;
    };



    //
    // Parse the command line arguments
    //
    void parse( const int argc, char **argv ) {
        for( int arg = 1; arg < argc; ++arg ) {
            if( strcmp( argv[arg], "--default" ) == 0 ) {
                option.init();

            } else if( strcmp( argv[arg], "--unique" ) == 0 ) {
                option.duplicates = false;

            } else if( strcmp( argv[arg], "--duplicate" ) == 0 ) {
                option.duplicates = true;

            } else if( strcmp( argv[arg], "--delimiters" ) == 0 ) {
                option.delimiters = parseString( argv[++arg] );

            } else if( strcmp( argv[arg], "--debounce" ) == 0 ) {
                option.debounce = strtoul( argv[++arg], NULL, 0 ) * 1000;
                if( errno ) {
                    perror( "debounce" );
                    exit( 1 );
                }

            } else if( *argv[arg] == '-' ) {
                fprintf( stderr, "Unknown option: %s\n", argv[arg] );
                exit( 1 );

            } else if( *argv[arg] == '+' ) {
                option.format = parseString( argv[arg] );
                option.formatEnd = parseFormat( option.format );

            } else {
                if( pfdCount >= pfdMax ) {
                    fprintf( stderr, "Too many files\n" );
                    exit( 1 );
                }

                pollFileDescriptor[pfdCount].openpfd( pfdCount, argv[arg], &option );
                ++pfdCount;
            }
        }
    };
} arguments;





//
// main
//
int main( const int argc, char **argv ) {
    arguments.parse( argc, argv );

    if( pfdCount == 0 ) {
        fprintf( stderr, "Usage: %s [[--debounce TIME] [--unique] [--duplicate] [--delimiters DELIMITERS] [+format] FILE] ...\n", argv[0] );
        exit( 2 );
    }

    struct timeval tv;
    long_time_t now;
    long_time_t timeout = FOREVER;

    while( true ) {
        if( poll( pfd, pfdCount, timeout == FOREVER ? -1 : timeout / 1000 ) < 0 ) {
            perror( "poll" );
            exit( 1 );
        }

        gettimeofday( &tv, NULL );
        now = (long_time_t)tv.tv_sec * 1000000 + tv.tv_usec;

        // devices return a timeout if they need calling when they have no data
        timeout = FOREVER;
        for( nfds_t pfdIndex = 0; pfdIndex < pfdCount; ++pfdIndex ) {
            timeout = std::min( timeout, pollFileDescriptor[pfdIndex].checkpfd( now ) );
        }
    }
};
