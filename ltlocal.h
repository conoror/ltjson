/*
 *  ltlocal.h: Definitions local to ltjson library
 *
 *  This code is supposed to be #included once in ltjson.c and
 *  not anywhere else. Thus the define guards. It's a H file more
 *  out of convention than anything else.
 *
 *  Distribution and use of this software are as per the terms of the
 *  Simplified BSD License (also known as the "2-Clause License")
 *
 *  Copyright 2016 Conor F. O'Rourke. All rights reserved.
 */


#ifndef _LTJSON_INLINE_INCLUDE_
  #error "This header is local to the ltjson library. Do not include it"
#endif


/* The ctype calls are disturbing in that they break horribly if
   passed a signed char (defined range is: -1 to 255). I redefine
   them with appropriate casts. */

#define c_toupper(x)   toupper((unsigned char)(x))
#define c_tolower(x)   tolower((unsigned char)(x))

#define c_isalnum(x)   isalnum((unsigned char)(x))
#define c_isalpha(x)   isalpha((unsigned char)(x))
#define c_iscntrl(x)   iscntrl((unsigned char)(x))
#define c_isdigit(x)   isdigit((unsigned char)(x))
#define c_isgraph(x)   isgraph((unsigned char)(x))
#define c_islower(x)   islower((unsigned char)(x))
#define c_isprint(x)   isprint((unsigned char)(x))
#define c_ispunct(x)   ispunct((unsigned char)(x))
#define c_isspace(x)   isspace((unsigned char)(x))
#define c_isupper(x)   isupper((unsigned char)(x))
#define c_isxdigit(x)  isxdigit((unsigned char)(x))

#define c_isascii(x)   isascii((unsigned char)(x))
#define c_isblank(x)   isblank((unsigned char)(x))


#define JSONNODE_MIN_ALLOC      8
#define JSONNODE_DEF_ALLOC      32

#define JSONNODE_NFLAGS_OPENOA  0x01        /* nflags only used while */
#define JSONNODE_NFLAGS_COLON   0x02        /* parsing incoming text  */

#define WORKSTR_INIT_ALLOC      32

#define NHASH_NBUCKETS          512
#define NHASH_CELL_ALLOC        128
#define NHASH_CELL_LINKCUR      (NHASH_CELL_ALLOC + 0)
#define NHASH_CELL_LINKNEXT     (NHASH_CELL_LINKCUR + 1)

#define SSTORE_MIN_ALLOC    64
#define SSTORE_DEF_ALLOC    (2048 - sizeof(struct sstore))


struct nhashcell {
    const char *s;
    struct nhashcell *next;
};


struct sstore
{
    int balloc;         /* Memory allocated for string storage */
    int bavail;         /* Bytes available */
    struct sstore *next;
    struct sstore *prev;
};


/* ltjson info structure, includes the root node at offset zero */

typedef struct {

    ltjson_node_t rootnode;     /* Root node of tree (not a pointer) */

    ltjson_node_t *root;        /* Root of tree (pointer to above)   */
    ltjson_node_t *open;        /* Tree open? This is current node   */
    ltjson_node_t *cbasenode;   /* Current basenode                  */

    int nodeasize;              /* node alloc size, set at startup   */

    char *workstr;              /* Working string for partials       */
    int   workalloc;            /* Amount allocated to workstr       */

    void *sstore;               /* Context handle to string store    */

    struct nhashcell **nhtab;   /* Name hash table, optional use     */
    struct nhashcell *nhstore;  /* Hash cell storage blocks          */
    int nh_nhits;               /* Stats: Hash match finds entry and */
    int nh_nmisses;             /*    hash match does not find entry */

    const char *lasterr;        /* 0 or description of error         */
    int incomplete;             /* If !0, continue adding to string  */

} ltjson_info_t;


/* Descriptions of errors. You could localise these I suppose. */

#define ERR_INT_NOERROR       ltjson_errordesc[0]
#define ERR_INT_INVALIDTREE   ltjson_errordesc[1]
#define ERR_INT_INTERNAL      ltjson_errordesc[2]

#define ERR_SEQ_BEGINTREE     ltjson_errordesc[3]
#define ERR_SEQ_UNEXPSTR      ltjson_errordesc[4]
#define ERR_SEQ_BADESCAPE     ltjson_errordesc[5]
#define ERR_SEQ_UNEXPNUM      ltjson_errordesc[6]
#define ERR_SEQ_OBJNONAME     ltjson_errordesc[7]
#define ERR_SEQ_BADNUMBER     ltjson_errordesc[8]
#define ERR_SEQ_UNEXPTXT      ltjson_errordesc[9]
#define ERR_SEQ_BADROBOT      ltjson_errordesc[10]
#define ERR_SEQ_TREEDUMP      ltjson_errordesc[11]

#define ERR_SEQ_NOCOLON       ltjson_errordesc[12]
#define ERR_SEQ_LEADCOMMA     ltjson_errordesc[13]
#define ERR_SEQ_UNEXPOA       ltjson_errordesc[14]
#define ERR_SEQ_MMCLOSEOBJ    ltjson_errordesc[15]
#define ERR_SEQ_MMCLOSEARR    ltjson_errordesc[16]
#define ERR_SEQ_BADCLOSURE    ltjson_errordesc[17]
#define ERR_SEQ_UNEXPCOLON    ltjson_errordesc[18]
#define ERR_SEQ_BADTEXT       ltjson_errordesc[19]


static const char *ltjson_errordesc[] =
{
    "No error",
    "JSON tree argument is not valid",

    "Internal parsing error (report bug)",

    "JSON tree must start with an object or array",
    "Unexpected string (missing comma?)",
    "Cannot decode an escape in string",
    "Unexpected number (missing comma?)",
    "Object entry with no name",
    "Cannot convert number representation",
    "Unexpected non-string text",
    "Cannot convert logic representation",
    "Tree forced to discontinue parse",

    "Expected a name-value separator (:)",
    "Comma after empty value",
    "Unexpected object or array (missing comma?)",
    "Mismatched object closure",
    "Mismatched array closure",
    "Empty entry at object or array close",
    "Unexpected name-value separator (:)",
    "Random unquoted text in content",
};


/* Descriptions of memory statistics */

#define MSTAT_TOTAL             0
#define MSTAT_NODES_ALLOC       1
#define MSTAT_NODES_USED        2
#define MSTAT_WORKSTR_ALLOC     3
#define MSTAT_SSTORE_NBLOCKS    4
#define MSTAT_SSTORE_ALLOC      5
#define MSTAT_SSTORE_FILLED     6
#define MSTAT_HASH_NBUCKETS     7
#define MSTAT_HASH_BUCKETFILL   8
#define MSTAT_HASHCELL_ALLOC    9
#define MSTAT_HASHCELL_FILLED   10
#define MSTAT_HASH_HITS         11
#define MSTAT_HASH_MISSES       12
#define MSTAT_NENTS             13

#if MSTAT_NENTS != LTJSON_MEMSTATS
  #error LTJSON_MEMSTATS must be equal to MSTAT_NENTS
#endif


static const char *ltjson_memstatdesc[] =
{
    "total memory (bytes)",

    "json nodes created",
    "json nodes filled",

    "working store (bytes)",

    "string store chains",
    "string store total (bytes)",
    "string store used (bytes)",

    "hash buckets created",
    "hash buckets filled",

    "hash cells created",
    "hash cells filled",

    "hash hits",
    "hash misses"
};


/* vi:set expandtab ts=4 sw=4: */
