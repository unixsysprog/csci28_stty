/*
 * ==========================
 *   FILE: ./sttyl.c
 * ==========================
 * Purpose: Set a limited number of options for a terminal device interface.
 *
 * Outline: sttyl with no arguments will print the current values for options
 *			it knows about. Special characters you can change are erase and
 *			kill. Other attributes can be set (turned on) using the name, or
 *			unset (turned off) by adding a '-' before the attribute. See usage
 *			below for examples.
 *
 * Usage:	./sttyl							-- no options, prints current vals
 *			./sttyl -echo onlcr erase ^X	-- turns off echo, turns on onlcr
 *											   and sets the erase char to ^X
 *
 * Tables: sttyl is a table-driven program. The tables are defined below.
 *		There is a single table that contains structs for each of the four
 *		flag types in termios: c_iflag, c_oflag, c_cflag, and c_lflag. There
 *		is a separate table that contains structs storing the special
 *		characters. The table of flags contains an offset corresponding to
 *		the position in a termios struct where the bit-mask for the flag can
 *		be found. To read how this is implemented and works, read the Plan
 *		document.
 */

/* INCLUDES */
#include	<stdio.h>
#include	<stddef.h>
#include	<termios.h>
#include	<stdlib.h>
#include	<string.h>
#include	<unistd.h>
#include	<sys/ioctl.h>
#include	<ctype.h>

/* CONSTANTS */
#define CHAR_MASK 64
#define ERROR 1
#define ON	1
#define OFF 0
#define YES 1
#define NO  0

/* TABLES DEFINITIONS */
struct table_t {tcflag_t flag; char *name; char *type; unsigned long mode; };
struct ctable_t {cc_t c_value; char *c_name; };

struct table_t table[] = {
	{ ICRNL		, "icrnl"	, "iflag"	, offsetof(struct termios, c_iflag)},
	{ OPOST		, "opost"	, "oflag"	, offsetof(struct termios, c_oflag)},
	{ HUPCL		, "hupcl"	, "cflag"	, offsetof(struct termios, c_cflag)},
	{ ISIG		, "isig"	, "lflag"	, offsetof(struct termios, c_lflag)},
	{ ICANON	, "icanon"	, "lflag"	, offsetof(struct termios, c_lflag)},
	{ ECHO		, "echo"	, "lflag"	, offsetof(struct termios, c_lflag)},
	{ ECHOE		, "echoe"	, "lflag"	, offsetof(struct termios, c_lflag)},
	{ ECHOK		, "echok"	, "lflag"	, offsetof(struct termios, c_lflag)},
	{ 0			, NULL		, NULL		, 0 }
};
struct ctable_t cchars[] = {
	{ VEOF		,	"eof"} ,
	{ VEOL		,	"eol" },
	{ VERASE	,	"erase" },
	{ VINTR		,	"intr" },
	{ VKILL		,	"kill" },
	{ VQUIT		,	"quit" },
	{ VSUSP		,	"susp" },
	{ 0			,	NULL }
};

/* DISPLAY INFO */
void show_tty(struct termios *);
void show_charset(struct termios *);
void show_flagset(struct termios *);

/* OPTION PROCESSING */
int valid_char_opt(char *, struct ctable_t **);
void change_char(struct ctable_t *, char *, struct termios *);
void get_option(char *, struct termios *);

/* TERMINAL FUNCTIONS */
void get_settings(struct termios *);
int set_settings(struct termios *);
struct winsize get_term_size();
int getbaud(int);

/* HELPER FUNCTIONS */
void fatal(char *, char *);
struct table_t * lookup(char *);

/* FILE-SCOPE VARIABLES*/
static char *progname;			//used for error-reporting

/*
 *	main()
 *	 Method: Load the current termios settings and process command-line
 *			 arguments. If none, it prints out the current values. Otherwise,
 *			 check for invalid/missing arguments, update the values, and set
 *			 the termios struct accordingly.
 *	 Return: 0 on success, 1 on error. If there is an invalid/missing
 *			 argument, the corresponding helper function will exit 1.
 */
int main(int ac, char *av[])
{
	struct termios ttyinfo;
	get_settings(&ttyinfo);							//pull in current settings
	progname = *av;									//init to program name
	struct ctable_t *c;								//for option processing

	if (ac == 1)									//no args, just progname
	{
		show_tty(&ttyinfo);							//show default info
		return 0;									//and stop
	}

	while(*++av)
	{
		if( valid_char_opt(*av, &c) == YES )		//special-char option?
		{
			if(av[1])								//check next arg exists
			{
				change_char(c, av[1], &ttyinfo);	//change it, or fatal()
				av++;								//extra arg skip
			}
			else
				fatal("missing argument to", *av);	//no arg for special-char
		}
		else										//a different attribute
			get_option(*av, &ttyinfo);
	}

	return set_settings(&ttyinfo);			//exit 1 on error, 0 on success
}

/*
 *	show_tty()
 *	Purpose: display the current settings for the tty.
 *	  Input: info, the struct containing the terminal information
 *	 Output: A collection of settings, separated by ';' and sorted by type.
 *	 Errors: get_term_size() will exit(1) if it encounters an error.
 */
void show_tty(struct termios *info)
{
	//get terminal size and baud speed
	struct winsize w = get_term_size();		//exit 1 if error getting size
	int baud = getbaud(cfgetospeed(info));

	//print info
	printf("speed %d baud; ", baud);		//baud speed
	printf("rows %d; ", w.ws_row);			//rows
	printf("cols %d;\n", w.ws_col);			//cols
	show_charset(info);						//special characters
	show_flagset(info);						//current flag states

	return;
}

/*
 *	show_charset()
 *	Purpose: Print the list of special characters and their current values.
 *	  Input: info, the struct containing terminal information
 *	 Output: A header identifying output as "cchars: ", followed by
 *			 ';' delimited "type = char" values.
 *	 Method: For disabled values, as denoted by _POSIX_VDISABLE, print
 *			 "<undef>" (courtesy of the 2019-03-13 section by Brandon
 *			 Williams). For unprintable values, use ^X notation, where X
 *			 is the value XORed with the CHAR_MASK, 64 or ASCII '@'. In
 *			 practice, this adds 64 to values 0-31 and subtracts 64 from
 *			 value 127, the DEL char (This idea was mentioned in piazza
 *			 post @171.). For all other values, they are printable ASCII.
 *	   Note: Unlike the struct for flags, which stores the type of flag
 *			 the array, the cchars does not since its identity is defined
 *			 by its unique table. In this case, for printing out a header,
 *			 the value of "cchars: " is coded into the print statement rather
 *			 than a variable.
 */
void show_charset(struct termios *info)
{
	int i;

	//iterate through the cchars table (defined at top)
	for(i = 0; cchars[i].c_name != NULL; i++)
	{
		//if the first value, print a header
		if(i == 0)
			printf("cchars: ");

		//get value from termios struct for the current cchar
		cc_t value = info->c_cc[cchars[i].c_value];

		//print the name and corresponding value, see "Method" above
		if (value == _POSIX_VDISABLE)
			printf("%s = <undef>; ", cchars[i].c_name);
		else if(iscntrl(value))
			printf("%s = ^%c; ", cchars[i].c_name, value ^ CHAR_MASK);
		else
			printf("%s = %c; ", cchars[i].c_name, value);
	}

	return;
}

/*
 *	show_flagset()
 *	Purpose: Print the current state of terminal flags.
 *	  Input: info, the struct containing terminal information
 *	 Output: For each flag type (e.g. iflags, oflags, etc.), print a header
 *			 for each, followed by a space-delimited list of the flags. A
 *			 leading dash signifies that flag is OFF, otherwise it is ON.
 *			 Each flag type starts a new line.
 *	 Method: Iterate through the table containing all terminal flags. If
 *			 the flag is the first of its type, store the flag type and
 *			 print it as a header, with a new line, a la the macOS version
 *			 of stty. Then, using the offset stored in the table, go to the
 *			 correct place in the termios struct to compare with the current
 *			 flag value.
 */
void show_flagset(struct termios * info)
{
	int i;
	char * type = NULL;

	//iterate through the table of flags (defined at top)
	for(i = 0; table[i].name != NULL; i++)
	{
		//If the first a given type, store the flag type and print as header
		if(type == NULL || strcmp(type, table[i].type) != 0)
		{
			type = table[i].type;				//switch to new flag type
			printf("\n%ss: ", type);			//print extra 's' to header
		}

		//get the pointer to termios struct stored in "entry"
		tcflag_t * mode_p = (tcflag_t *)((char *)(info) + table[i].mode);

		//check if the flag is ON or OFF
		if ((*mode_p & table[i].flag) == table[i].flag)
			printf("%s ", table[i].name);		//if ON, just print
		else
			printf("-%s ", table[i].name);		//if OFF, add '-'
	}

	//if printed flags were printed, add a tailing newline
	if (i > 0)
		printf("\n");

	return;
}

/*
 *	valid_char_opt()
 *	Purpose: Check to see if an argument matches one of the special chars.
 *	  Input: arg, the option to check
 *			 c, a pointer to store the struct that matched
 *	 Return: YES, if the arg was found in the cchars table. Otherwise, NO.
 */
int valid_char_opt(char * arg, struct ctable_t **c)
{
	int i;

	for(i = 0; cchars[i].c_name != NULL; i++)	//go through all char options
	{
		if(strcmp(arg, cchars[i].c_name) == 0)	//if it matches arg requested
		{
			*c = &cchars[i];					//store ptr to that struct
			return YES;							//return YES
		}
	}

	return NO;
}

/*
 *	change_char()
 *	Purpose: Update a control char -- "erase" or "kill" are accepted.
 *	  Input: c, the struct containing the index to update
 *			 value, the command-line to arg containing the new char
 *			 info, the struct containing terminal information
 *	 Errors: If the "value" argument is more than 1-char long, it is
 *			 invalid, so print error and exit 1.
 *	   Note: Bullet #2 in the assignment handout mentions the program is
 *			 not required to handle caret-letter input. If it did, this
 *			 is where it would be implemented.
 */
void change_char(struct ctable_t * c, char *value, struct termios *info)
{
	if (strlen(value) > 1 || ! isascii(value[0]))	//not an acceptable char
		fatal("invalid integer argument", value);	//exit

	info->c_cc[c->c_value] = value[0];				//set the value

	return;
}

/*
 *	get_option()
 *	Purpose: Turn the given option on/off in the termios struct.
 *	  Input: option, the argument to check and turn on/off
 *	 Return: If the option is not found in the table, fatal() is called
 *			 to print an error message and exit 1. Otherwise, the flag
 *			 is set to ON or OFF.
 */
void get_option(char *option, struct termios *info)
{
	int status = ON;
	char * original = option;					//"store" the original
	struct table_t * entry = NULL;				//place to put flag info

	if(option[0] == '-')						//check if a leading dash
	{
		status = OFF;							//will turn option off
		option++;								//trim dash from option
	}

	if ( (entry = lookup(option)) == NULL)		//lookup appropriate flag
		fatal("illegal argument", original);	//couldn't find it, exit

	//store pointer to termios struct stored in "entry"
	tcflag_t * mode_p = (tcflag_t *)((char *)(info) + entry->mode);

	if(status == ON)
		*mode_p |= entry->flag;					//turn ON
	else
		*mode_p &= ~entry->flag;				//turn OFF

	return;
}

/*
 *	lookup()
 *	Purpose: Find a given option in the defined tables.
 *	  Input: option, the argument we are searching for
 *	 Return: A pointer to the corresponding flag, if a match is found.
 *			 Otherwise, NULL is returned to indicate failure.
 */
struct table_t * lookup(char *option)
{
	int i;
	for (i = 0; table[i].name != NULL; i++)
	{
		//option matched table entry
		if(strcmp(option, table[i].name) == 0)
			return &table[i];
	}

	return NULL;
}

/*
 *	fatal()
 *	Purpose: Print message to stderr and exit.
 *	  Input: err, the type of error that was encounters
 *			 arg, the value of the argument that caused a problem
 *	 Return: Exit with 1.
 */
void fatal(char *err, char *arg)
{
	fprintf(stderr, "%s: %s `%s'\n", progname, err, arg);
	exit(1);
}

/*
 *	get_term_size()
 *	Purpose: Get the current size of the terminal, in rows and cols.
 *	 Return: On error, message output to stderr and exit 1. Otheriwse,
 *			 ioctl() stores the winsize struct in "w" and returns that.
 *	   Note: ioctl() values copied from termfuncs.c from the more03
 *			 assignment files at the beginning of class.
 */
struct winsize get_term_size()
{
	struct winsize w;

	if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) != 0)
	{
		fprintf(stderr, "could not get window size\n");
		exit(1);
	}

	return w;
}

/*
 *	get_settings()
 *	Purpose: Retrieve the current terminal settings.
 *	  Input: info, the struct to store terminal information
 *	 Return: On error, message output to stderr and exit 1. Otherwise,
 *			 tcgetattr() stores the information in the struct passed in.
 */
void get_settings(struct termios *info)
{
	if ( tcgetattr(0, info) == -1 )
	{
		perror("cannot get tty info for stdin");
		exit(1);
	}

	return;
}

/*
 *	set_settings()
 *	Purpose: Apply changes to the terminal settings.
 *	  Input: info, the struct containing terminal information
 *	 Return: 0 on success, 1 on error. If an error, message will be
 *			 output to stderr.
 */
int set_settings(struct termios *info)
{
	if ( tcsetattr( 0, TCSANOW, info ) == -1 )
	{
		perror("Setting attributes");
		exit(1);
	}

	return 0;
}

/*
 *	getbaud()
 *	Purpose: Convert a speed_t value into the corresponding baud value.
 *	  Input: speed, the speed_t value stored in the termios struct
 *	 Return: The speed converted to an int.
 *	  Notes: The base of the code was copied from the showtty.c file from
 *			 the lecture materials for week 5. Minor modifications have
 *			 been made, adding more values according to the standards found
 *			 at the link below.
 *
 *	http://pubs.opengroup.org/onlinepubs/007904975/basedefs/termios.h.html
 */
int getbaud(int speed)
{
	switch(speed)
	{
		case B0:		return 0;		break;
		case B50:		return 50;		break;
		case B75:		return 75;		break;
		case B110:		return 110;		break;
		case B134:		return 134;		break;
		case B150:		return 150;		break;
		case B200:		return 200;		break;
		case B300:		return 300;		break;
		case B600:		return 600;		break;
		case B1200:		return 1200;	break;
		case B1800:		return 1800;	break;
		case B2400:		return 2400;	break;
		case B4800:		return 4800;	break;
		case B9600:		return 9600;	break;
		case B19200:	return 19200;	break;
		case B38400:	return 38400;	break;
		default:
			perror("failed to get baud speed");
			exit(1);
	}
}
