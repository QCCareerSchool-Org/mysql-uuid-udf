#ifdef STANDARD
/* STANDARD is defined, don't use any mysql functions */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifdef __WIN__
typedef unsigned __int64 ulonglong; /* Microsofts 64 bit types */
typedef __int64 longlong;
#else
typedef unsigned long long ulonglong;
typedef long long longlong;
#endif /*__WIN__*/
#else
#include <my_global.h>
#include <my_sys.h>
#if defined(MYSQL_SERVER)
#include <m_string.h> /* To get strmov() */
#else
/* when compiled as standalone */
#include <string.h>
#define strmov(a, b) stpcpy(a, b)
#define memcpy_fixed(a, b, c) memcpy(a, b, c)
#endif
#endif
#include <mysql.h>
#include <ctype.h>

const int uuid_length = 36; // includes hyphens
const int uuid_bin_length = 16;

my_bool uuid_to_bin_init(UDF_INIT *initid, UDF_ARGS *args, char *message);
char *uuid_to_bin(UDF_INIT *initid, UDF_ARGS *args, char *result, unsigned long *length, char *is_null, char *error);
my_bool bin_to_uuid_init(UDF_INIT *initid, UDF_ARGS *args, char *message);
char *bin_to_uuid(UDF_INIT *initid, UDF_ARGS *args, char *result, unsigned long *length, char *is_null, char *error);

/**
 * Converts a single hexadecimal character to its numerical equivalent
 * 
 * (Note: returns an int rather than unsigned char so that we can handle -1 for invalid input)
 *
 * @param c the hexadecimal character
 * @return the numerical equivalent, or -1 for invalid input
 */
inline int hex_to_num(char c)
{
	if (c >= '0' && c <= '9')
	{
		return c - '0';
	}

	// convert to lowercase / set the 6th bit to 1 (e.g. A = 64, a = 97)
	c |= 32;

	if (c >= 'a' && c <= 'f')
	{
		return c - 'a' + 10;
	}

	return -1;
}

/**
 * Replaces a portion of a byte array with the hexadecimal representation of a particular two-byte number
 *
 * @param output   a pointer to the start of the array
 * @param position the position within the array to start replacing data
 * @param ch       the two-byte numerical data
 * @return         the next position to use
 */
inline int num_to_hex(char *output, int position, int ch)
{
	static char hex_values[] = "0123456789abcdef";
	output[position++] = hex_values[(ch >> 4) & 0xf];
	output[position++] = hex_values[ch & 0xf];
	return position;
}

/**
 * Converts a string representation of a UUID into a byte array
 * 
 * @param input the string representation of a UUID
 * @param output a 16-byte buffer for returning the result
 * @return my_bool 
 */

my_bool convert_hex_to_bytes(const char input[uuid_length], unsigned char output[uuid_bin_length])
{
	int i, j = 0;

	for (i = 0; i < uuid_length; i += 2)
	{
		/// skip hyphens
		if (i == 8 || i == 13 || i == 18 || i == 23)
		{
			i++;
		}

		int high_bits = hex_to_num(input[i]);
		int low_bits = hex_to_num(input[i + 1]);

		if (high_bits < 0 || low_bits < 0)
		{
			return 0;
		}

		output[j++] = (high_bits << 4) + low_bits;
	}

	return 1;
}

/**
 * Converts a byte array representing a UUID into its string representation
 * 
 * @param input the byte array representing the uuid
 * @param output a 36-byte buffer for returning the result
 */
void convert_bytes_to_hex(const unsigned char input[uuid_bin_length], char output[uuid_length])
{
	int i, j = 0;

	for (i = 0; i < uuid_bin_length; i++)
	{
		if (j == 8 || j == 13 || j == 18 || j == 23)
		{
			output[j++] = '-';
		}
		j = num_to_hex(output, j, input[i]);
	}
}

/**
 * Swaps the bytes in an array for optimized storage of UUID v1 data
 * 
 * @param dest a 16-byte buffer for returning the result
 * @param src a 16-byte buffer representing a UUID
 */
void swap_bytes(unsigned char dest[uuid_bin_length], const unsigned char src[uuid_bin_length])
{
	dest[0] = src[6];
	dest[1] = src[7];
	dest[2] = src[4];
	dest[3] = src[5];
	dest[4] = src[0];
	dest[5] = src[1];
	dest[6] = src[2];
	dest[7] = src[3];
	int i;
	for (i = 8; i < uuid_bin_length; i++)
	{
		dest[i] = src[i];
	}
}

/**
 * Reverts the swapping of the bytes in an array for optimized storage of UUID v1 data
 * 
 * @param dest a 16-byte buffer for returning the result
 * @param src a 16-byte buffer with representing a byte-swapped UUID
 */
void unswap_bytes(unsigned char dest[uuid_bin_length], const unsigned char src[uuid_bin_length])
{
	dest[0] = src[4];
	dest[1] = src[5];
	dest[2] = src[6];
	dest[3] = src[7];
	dest[4] = src[2];
	dest[5] = src[3];
	dest[6] = src[0];
	dest[7] = src[1];
	int i;
	for (i = 8; i < uuid_bin_length; i++)
	{
		dest[i] = src[i];
	}
}

my_bool uuid_to_bin_init(UDF_INIT *initid, UDF_ARGS *args, char *message)
{
	// make sure we have at least one parameter passed
	if (args->arg_count < 1)
	{
		strcpy(message, "UUID_TO_BIN requires at least one argument");
		return 1;
	}

	// if the paramater is not a string, treat it like a string
	if (args->arg_type[0] != STRING_RESULT)
	{
		strcpy(message, "UUID_TO_BIN requires the first argument to be a string");
		return 1;
	}

	if (args->arg_count == 2 && args->arg_type[1] != INT_RESULT)
	{
		strcpy(message, "UUID_TO_BIN requires the second argument to be an integer");
		return 1;
	}

	// let mysql know that our output might be null
	initid->maybe_null = 1;

	// tell mysql how long our result will be
	initid->max_length = uuid_bin_length;

	// good to go
	return 0;
}

char *uuid_to_bin(UDF_INIT *initid, UDF_ARGS *args, char *result, unsigned long *length, char *is_null, char *error)
{
	// check for null input
	if (args->args[0] == 0)
	{
		*is_null = 1;
		return result;
	}

	// check length of first argument
	if (args->lengths[0] != uuid_length)
	{
		*is_null = 1;
		return result;
	}

	longlong swap = args->arg_count == 2 ? *((longlong *)args->args[1]) : 0;

	if (swap != 0 && swap != 1)
	{
		*error = 1;
		return result;
	}

	unsigned char bin[uuid_bin_length];

	if (!convert_hex_to_bytes(args->args[0], bin))
	{
		*is_null = 1;
		return result;
	}

	if (swap)
	{
		unsigned char swapped_bin[uuid_bin_length];
		swap_bytes(swapped_bin, bin);
		memcpy(result, swapped_bin, uuid_bin_length);
	}
	else
	{
		memcpy(result, bin, uuid_bin_length);
	}

	*length = uuid_bin_length;
	return result;
}

my_bool bin_to_uuid_init(UDF_INIT *initid, UDF_ARGS *args, char *message)
{
	// make sure we have at least one parameter passed
	if (args->arg_count < 1)
	{
		strcpy(message, "BIN_TO_UUID requires at least one argument");
		return 1;
	}

	// if the paramater is not a string, treat it like a string
	if (args->arg_type[0] != STRING_RESULT)
	{
		strcpy(message, "BIN_TO_UUID requires the first argument to be a binary");
		return 1;
	}

	if (args->arg_count == 2 && args->arg_type[1] != INT_RESULT)
	{
		strcpy(message, "BIN_TO_UUID requires the second argument to be an integer");
		return 1;
	}

	// let mysql know that our output might be null
	initid->maybe_null = 1;

	// tell mysql how long our result will be
	initid->max_length = uuid_length;

	// good to go
	return 0;
}

char *bin_to_uuid(UDF_INIT *initid, UDF_ARGS *args, char *result, unsigned long *length, char *is_null, char *error)
{
	// check for null input
	if (args->args[0] == 0)
	{
		*is_null = 1;
		return result;
	}

	// check length of first argument
	if (args->lengths[0] != uuid_bin_length)
	{
		*is_null = 1;
		return result;
	}

	longlong swap = args->arg_count == 2 ? *((longlong *)args->args[1]) : 0;

	if (swap != 0 && swap != 1)
	{
		*error = 1;
		return result;
	}

	unsigned char bin[uuid_bin_length];

	if (swap)
	{
		unswap_bytes(bin, args->args[0]);
	}
	else
	{
		memcpy(bin, args->args[0], uuid_bin_length);
	}

	char uuid[uuid_length];

	convert_bytes_to_hex(bin, uuid);
	
	memcpy(result, uuid, uuid_length);

	*length = uuid_length;
	return result;
}
