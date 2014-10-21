//
// Copyright 2013,2014 Tarim
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
// arguments:
// -debounce N	number of milliseconds to ignore results
// +format	format for output content:%s time:%llu file:%s
// --unique	discard repeated results (useful with debounce)
// --duplicates	report repeated results
// --delimiters	string of characters which delimit "lines"
// filename
// --timeout N	not implemented yet - will allow watchdog
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

typedef uint64_t long_time_t;
const unsigned int bufferSize = 1024;
const unsigned int bufferCount = 3;
const long_time_t FOREVER = -1;
const nfds_t pfdMax = 23;	// number of files
// nfds_t fdsIndex
// fds[fdsIndex].fd file descriptor
// fds[fdsIndex].events events we're interested in
// fds[fdsIndex].revents events that happened

char formatDefault[] = "%s\n";
char *formatArg = formatDefault;
char delimitersDefault[] = "\n";
char *delimitersArg = delimitersDefault;
long_time_t debounceArg = 0;
bool duplicatesArg = true;

//
// descriptor class
//

nfds_t pfdCount = 0;
struct pollfd pfd[pfdMax];

class poll_info_c {
public:
    char *pathname;
    struct stat status;

    nfds_t index;
    char *format;

    int openMode;
    short pollEvents;

    //
    // debouncing is done by holding back data instead of printing
    // which appears within debounce time.  If device is quiet for debounce
    // time then print the held data if it's diffferent to the last output
    // otherwise silently discard it
    //
    // This method gives quickest response to any change on the device while
    // dropping bounce information and still reporting final state in case
    // it differs

    long_time_t debounceTime;
    bool reseek;
    bool duplicates;
    char *delimiters;

    long_time_t readTime;

    //
    // buffer system is coded for the kind of efficiancy that caused the
    // heartbleed bug - be warned!
    //
    char buffer[bufferCount][bufferSize];
    uint8_t bufferIndex;
    char *readPos;	// start of buffer unless previously data unfinished
    char *printPos;	// last printPos (usually in a different buffer)
    char *heldPos;	// held data from debounce



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
    void openpfd( const nfds_t pfdi, char *fname ) {
	index = pfdi;
	pathname = fname;

	statpfd();

	pfd[index].fd = open( pathname, openMode );
	if( pfd[index].fd < 0 ) {
	    perror( pathname );
	    exit( 1 );
	}
	pfd[index].events = pollEvents;

	format = formatArg;
	readTime = 0;
	debounceTime = debounceArg * 1000;
	delimiters = delimitersArg;
	duplicates = duplicatesArg;

	printPos = &(buffer[0][0]);
	*printPos = '\0';
	bufferIndex = 1;
	readPos = &(buffer[1][0]);
	heldPos = NULL;
    };



    //
    // Check held (debounced) data or readable data
    //
    long_time_t checkpfd( long_time_t now ) {
	bool nobounce = now - readTime >= debounceTime;
	// held data from a debounce might need printing
	if( heldPos && nobounce ) {
	    printpfd( heldPos );
	    heldPos = NULL;
	}

	// shouldn't get an End Of File - but in case we do
	if( pfd[index].revents & POLLHUP ) {
	    fprintf( stderr, "EOF: %s\n", pathname );
	    pfd[index].fd = -1;		// disable polling fd
	}

	// if there's data to read then parse it
	if( (pfd[index].revents & pfd[index].events) && readpfd() ) {
	    readTime = now;
	    char *startPos = &(buffer[bufferIndex][0]);
	    char *eolPos;

	    // step through the lines in the buffer
	    while( (eolPos = strpbrk( startPos, delimiters )) ) {
	        *eolPos = '\0';		// overwrite with C string delimiter
	        if( nobounce ) {
		    printpfd( startPos );
        
	        } else {
		    heldPos = startPos;	// hold instead of print if bouncing
	        }
	        startPos = eolPos + 1;
		nobounce = debounceTime == 0;	// in case >1 line in buffer
	    }

	    // find a buffer which doesn't have printPos or heldPos in it
	    uint8_t buffersUsed = bufferOf( printPos ) | bufferOf( heldPos );
	    for( bufferIndex = 0; buffersUsed & (1<<bufferIndex); ++bufferIndex ) {
		// shouldn't ever happen
		if( bufferIndex >= bufferCount ) {
		    fprintf( stderr, "No free buffers: %s\n", pathname );
		    exit( 1 );
		}
	    }
	    readPos = &(buffer[bufferIndex][0]);

	    // copy partial line to beginning of buffer
	    unsigned int remainder = strlen( startPos );
	    if( remainder ) {
		strncpy( readPos, startPos, remainder );
		readPos += remainder;
	    }
	}

	// if held data then return the time we need to be checked again
	return heldPos ? debounceTime - (now - readTime) : FOREVER;
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

	charCount = read( pfd[index].fd, readPos, bufferSize-2 + &(buffer[bufferIndex][0]) - readPos );
	if( charCount < 0 ) {
	    if( errno == EAGAIN || errno == EWOULDBLOCK ) {
		return 0;

	    } else {
		perror( "read" );
		exit( 1 );	// or should we set pfd[index].fd to -1?
	    }
	}

	readPos += charCount;
	// if not enough room in buffer then pretend it was a complete line
	if( readPos == bufferSize-2 + &(buffer[bufferIndex][0]) ) {
	    *(readPos++) = *delimiters;
	    ++charCount;
	}
	*readPos = '\0';
	return charCount;
    };



    //
    // print and update printPos to point to what we just printed
    //
    void printpfd( char *startPos ) {
	if( *startPos && ( duplicates || strcmp( printPos, startPos ) != 0 ) ) {
	    printf( format, startPos, readTime, pathname );
	    fflush( stdout );
	    printPos = startPos;
	}
    };
};



//
// parse the backslashes in an argument string
//
char lookupStr[] = "abfnrtv\"\'\\123456789";
char translateStr[] = "\a\b\f\n\r\t\v\"\'\\\001\002\003\004\005\006\007\010\011";
char *parseArg( char *format ) {
    for( char *pos = format; *pos; ++pos ) {
	if( *pos == '\\' ) {
	    char *find = strchr( lookupStr, *(pos+1) );
	    if( find ) {
		// replace with translated character and shift everything up
		*pos = translateStr[ find - lookupStr ];
		strcpy( pos+1, pos+2 );
	    }
	}
    }
    return format;
};



poll_info_c pollInfo[pfdMax];

//
// the main listener loop
//
void listen() {
    struct timeval tv;
    long_time_t now;
    long_time_t timeout = FOREVER;

    while( true ) {
    	if( poll( pfd, pfdCount, timeout == FOREVER ? -1 : timeout / 1000 ) < 0 ) {
	    perror( "poll" );
	    exit( 1 );
	}

	// set time poll says there is data
	gettimeofday( &tv, NULL );
	now = (long_time_t)tv.tv_sec * 1000000 + tv.tv_usec;

	// devices return a timeout if they need calling when they have no data
	timeout = FOREVER;
        for( nfds_t pfdIndex = 0; pfdIndex < pfdCount; ++pfdIndex ) {
	    timeout = std::min( timeout, pollInfo[pfdIndex].checkpfd( now ) );
	}
    }
};



int main( const int argc, char **argv ) {
    int arg;
    for( arg = 1; arg < argc; ++arg ) {
	if( strcmp( argv[arg], "--debounce" ) == 0 ) {
	    debounceArg = strtoul( argv[++arg], NULL, 0 );
	    if( errno ) {
		perror( "debounce" );
		exit( 1 );
	    }

	} else if( strcmp( argv[arg], "--unique" ) == 0 ) {
	    duplicatesArg = false;

	} else if( strcmp( argv[arg], "--duplicate" ) == 0 ) {
	    duplicatesArg = true;

	} else if( strcmp( argv[arg], "--delimiters" ) == 0 ) {
	    delimitersArg = parseArg( argv[++arg] );

	} else if( *argv[arg] == '-' ) {
	    fprintf( stderr, "Unknown option: %s\n", argv[arg] );
	    exit( 1 );

	} else if( *argv[arg] == '+' ) {
	    formatArg = parseArg( argv[arg] + 1 );

	} else {
	    if( pfdCount >= pfdMax ) {
		fprintf( stderr, "Too many files\n" );
		exit( 1 );
	    }

	    pollInfo[pfdCount].openpfd( pfdCount, argv[arg] );
	    ++pfdCount;
	}
    }

    if( pfdCount == 0 ) {
	fprintf( stderr, "Usage: %s [[--debounce TIME] [--unique] [--duplicate] [--delimiters DELIMITERS] FILE] ...\n", argv[0] );
	exit( 2 );
    }

    listen();
};
