/*
 *  lthash.c (as include): Hashing functions
 *
 *  This code is supposed to be #included into the main ltjson.c file
 *  which makes the codebase less unwieldy but keep static namespace.
 *
 *  Distribution and use of this software are as per the terms of the
 *  Simplified BSD License (also known as the "2-Clause License")
 *
 *  Copyright 2016 Conor F. O'Rourke. All rights reserved.
 */

#ifdef _LTJSON_INLINE_INCLUDE_


/* The hash table is implemented as a classic "bucket" table of
   pointers to table entries. Each entry is a linked list of strings
   that match any particular hash. The bucket table itself is only
   an array of pointers, not an array of cells.

   The table entries are allocated in blocks of size NHASH_CELL_ALLOC
   plus 2; the last two entries in the block used for table maintenance.
   table[NHASH_CELL_LINKCUR].next points to the next available empty
   cell while table[NHASH_CELL_LINKNEXT].next points to the head of the
   next table allocation. Any next pointer will point to a completely
   filled table (so new tables are inserted before, not after).
*/




/*
 *  djbhash(s) - Hash string s
 *
 *  Hashing algorithm from Dan Berntein, public domain, comp.lang.c
 *  I continually comment on how truly appalling the K&R C hash is
 *  so that noone will ever use it. It's just terrible!
 */

static unsigned long djbhash(const char *s)
{
    unsigned long hash = 5381;
    int c;

    while ((c = (const unsigned char)*s++) != '\0')
        hash = ((hash << 5) + hash) + c;

    return hash;
}




/*
 *  djbnhash(s, n) - Hash string s for length n
 *
 *  As above except like strncmp - takes n as a size_t
 */

static unsigned long djbnhash(const char *s, size_t n)
{
    unsigned long hash = 5381;
    int c;

    while (n-- && (c = (const unsigned char)*s++) != '\0')
        hash = ((hash << 5) + hash) + c;

    return hash;
}




/*
 *  nhash_addstore(jsoninfo) - Allocate memory for new cell store
 *
 *  Adds a new cell store block by chaining the new allocation in
 *  between jsoninfo->nhstore and the old store (if it exists).
 *  Initialises relevant parts of the new store block.
 *
 *  Returns 1 on success, 0 on failure with errno set to ENOMEM
 */

static int nhash_addstore(ltjson_info_t *jsoninfo)
{
    struct nhashcell *nhcp;

    assert(jsoninfo);

    nhcp = malloc(sizeof(struct nhashcell) * (NHASH_CELL_ALLOC + 2));
    if (nhcp == NULL)
    {
        errno = ENOMEM;
        return 0;
    }

    nhcp[NHASH_CELL_LINKCUR].next = &nhcp[0];             /* First free */
    nhcp[NHASH_CELL_LINKNEXT].next = jsoninfo->nhstore;   /* Previous store */

    jsoninfo->nhstore = nhcp;
    return 1;
}




/*
 *  nhash_free(jsoninfo) - Free all data associated with the hash
 *
 *  If jsoninfo contains a hash table and/or a hash store, free them.
 *  The table is one allocation but the store is a chained list of
 *  blocks.
 *  Sets the jsoninfo entries to NULL when finished.
 */

static void nhash_free(ltjson_info_t *jsoninfo)
{
    struct nhashcell *nhcp, *nhstore;

    if (!jsoninfo)
        return;

    if (jsoninfo->nhtab)
    {
        free(jsoninfo->nhtab);
        jsoninfo->nhtab = 0;
    }

    nhstore = jsoninfo->nhstore;

    while (nhstore)
    {
        nhcp = nhstore[NHASH_CELL_LINKNEXT].next;
        free(nhstore);
        nhstore = nhcp;
    }

    jsoninfo->nhstore = 0;
}




/*
 *  nhash_new(jsoninfo) - Set info structure up with new hash
 *
 *  If jsoninfo contains an old hash table and hash store, free
 *  those first. Then allocate memory for the table and initialise
 *  the pointers. And create a store using nhash_newstore().
 *  Finally, reset the hit/miss statistic counters to 0.
 *
 *  Returns 1 on success, 0 on failure with errno set to ENOMEM
 */

static int nhash_new(ltjson_info_t *jsoninfo)
{
    int i;

    assert(jsoninfo);

    nhash_free(jsoninfo);

    jsoninfo->nhtab = malloc(sizeof(struct nhashcell *) * NHASH_NBUCKETS);
    if (!jsoninfo->nhtab)
    {
        errno = ENOMEM;
        return 0;
    }

    for (i = 0; i < NHASH_NBUCKETS; i++)
        jsoninfo->nhtab[i] = NULL;

    if (!nhash_addstore(jsoninfo))
    {
        free(jsoninfo->nhtab);
        errno = ENOMEM;
        return 0;
    }

    jsoninfo->nh_nhits   = 0;
    jsoninfo->nh_nmisses = 0;

    return 1;
}




/*
 *  nhash_reset(jsoninfo) - Reset the hash system up for reuse
 *
 *  Wipe the bucket pointers, clear out the cells and reset the
 *  hit/miss counters.
 */

static void nhash_reset(ltjson_info_t *jsoninfo)
{
    struct nhashcell *nhcp;
    int i;

    if (!jsoninfo || !jsoninfo->nhtab)
        return;

    for (i = 0; i < NHASH_NBUCKETS; i++)
        jsoninfo->nhtab[i] = NULL;

    nhcp = jsoninfo->nhstore;

    while (nhcp)
    {
        nhcp[NHASH_CELL_LINKCUR].next = &nhcp[0];
        nhcp = nhcp[NHASH_CELL_LINKNEXT].next;
    }

    jsoninfo->nh_nhits   = 0;
    jsoninfo->nh_nmisses = 0;
}




/*
 *  nhash_stats(jsoninfo) - Get hash system statistics
 *
 *  Returns total memory usage
 */

static int nhash_stats(ltjson_info_t *jsoninfo,
                       int *bfillp, int *callocp, int *cfillp)
{
    struct nhashcell *nhcp;
    int i, bfcnt, cacnt, cfcnt, tmem;

    if (!jsoninfo || !jsoninfo->nhtab)
    {
        if (bfillp)
            *bfillp = 0;
        if (callocp)
            *callocp = 0;
        if (cfillp)
            *cfillp = 0;

        return 0;
    }

    bfcnt = cacnt = cfcnt = tmem = 0;

    tmem = sizeof(struct nhashcell *) * NHASH_NBUCKETS;

    for (i = 0; i < NHASH_NBUCKETS; i++)
    {
        if (jsoninfo->nhtab[i] != NULL)
            bfcnt++;
    }

    nhcp = jsoninfo->nhstore;

    while (nhcp)
    {
        cacnt += NHASH_CELL_ALLOC;
        tmem += sizeof(struct nhashcell) * (NHASH_CELL_ALLOC + 2);
        cfcnt += nhcp[NHASH_CELL_LINKCUR].next - &nhcp[0];

        nhcp = nhcp[NHASH_CELL_LINKNEXT].next;
    }

    if (bfillp)
        *bfillp = bfcnt;
    if (callocp)
        *callocp = cacnt;
    if (cfillp)
        *cfillp = cfcnt;

    return tmem;
}




/*
 *  nhash_insert(jsoninfo, s) - Insert string into sstore if required
 *
 *  Lookup the hash of s in jsoninfo's hashtable, if there is one, and
 *  if a string exists already, return that one, otherwise add it to
 *  sstore and to the hash table and return the new string.
 *
 *  Safe to call if no hashtable exists as it just adds the string to the
 *  string store. Adding a blank string ("") just returns a static pointer.
 *
 *  Returns: Pointer to new or existing string on success
 *           NULL on error and sets errno (ENOMEM)
 */

static const char *nhash_insert(ltjson_info_t *jsoninfo, const char *s)
{
    struct nhashcell *nhcp;
    unsigned long hashval;

    assert(jsoninfo && s);

    if (!*s)
        return ltjson_empty_name;

    if (!jsoninfo->nhtab || !jsoninfo->nhstore)
    {
        /* No hash. Improvise... */
        const char *nvstr;

        nvstr = sstore_add(&jsoninfo->sstore, s);
        if (!nvstr)
            return NULL;

        return nvstr;
    }

    hashval = djbhash(s) % NHASH_NBUCKETS;

    for (nhcp = jsoninfo->nhtab[hashval]; nhcp != NULL; nhcp = nhcp->next)
    {
        if (strcmp(s, nhcp->s) == 0)
        {
            jsoninfo->nh_nhits++;
            return nhcp->s;             /* Found */
        }
    }

    if (jsoninfo->nhtab[hashval])
        jsoninfo->nh_nmisses++;


    /* No match. Get an empty cell and store new string... */

    if (jsoninfo->nhstore[NHASH_CELL_LINKCUR].next ==
            &jsoninfo->nhstore[NHASH_CELL_LINKCUR])
    {
        /* "Next available" cell pointer points to its own storage
           cell. This means we're out of cells in the current block */

        if (!nhash_addstore(jsoninfo))
            return NULL;
    }

    nhcp = jsoninfo->nhstore[NHASH_CELL_LINKCUR].next++;


    nhcp->s = sstore_add(&jsoninfo->sstore, s);
    if (!nhcp->s)
        return NULL;

    nhcp->next = jsoninfo->nhtab[hashval];
    jsoninfo->nhtab[hashval] = nhcp;

    return nhcp->s;
}




/*
 *  nhash_lookup(jsoninfo, s) - Lookup string in hash table
 *
 *  Returns: Pointer to constant string on success
 *           NULL if not found with errno set to 0
 *           NULL if no hash table with errno set to ENOENT
 */

static const char *nhash_lookup(ltjson_info_t *jsoninfo, const char *s)
{
    struct nhashcell *nhcp;
    unsigned long hashval;

    assert(jsoninfo && s);

    if (!*s)
        return ltjson_empty_name;

    if (!jsoninfo->nhtab)
    {
        errno = ENOENT;
        return NULL;
    }

    hashval = djbhash(s) % NHASH_NBUCKETS;

    for (nhcp = jsoninfo->nhtab[hashval]; nhcp != NULL; nhcp = nhcp->next)
    {
        if (strcmp(s, nhcp->s) == 0)
            return nhcp->s;
    }

    errno = 0;
    return NULL;
}


#endif  /* _LTJSON_INLINE_INCLUDE_ */


/* vi:set expandtab ts=4 sw=4: */
