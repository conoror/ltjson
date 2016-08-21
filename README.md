# ltjson
A JSON parser, "Light JSON". The emphasis is on low memory usage and the
ability to free, reuse and/or continue the in memory json tree.
This is useful in embedded systems or messaging systems where the JSON tree
needs to be rebuilt over and over without leaking memory all over the place.

## The code
There are a number of lt*.c and lt*.h files but only ltjson.c needs to be
compiled and ltjson.h included. The other C files are included into ltjson.c
and protected with define guards.
I did this to keep file sizes reasonable inside a static namespace.

I use mingw so there's a quick make.bat to compile test.exe.

## Basic usage

* Include

    \#include "ltjson.h"

* Declare a jsontree. Init the pointer to NULL (important):

    ltjson_node_t *jsontree = 0;

* Parse the tree in buffer into the jsontree:

    ret = ltjson_parse(&jsontree, buffer, 0);

This will allocate memory as needed. If the tree in buffer is incomplete,
ret will be 0 with errno set to -EAGAIN and you can call ltjson_parse
again with the same parameters. If there's an error ret will be 0 with
errno set to ENOMEM, EILSEQ or EINVAL (see function descriptions below).

The last parameter is 0 to not use a hash tree and LTJSON_PARSE_USEHASH to
use a hash tree. Any object member names are hashed to avoid duplication
and speed searches. Any other strings are not hashed - they are just stored
as usual.

Once ret is 1 you can display the tree to the console with:

    ltjson_display(jsontree);

## Documentation

### General

#### ltjson_parse(treeptr, text, usehash) - Parse text into JSON tree
*Parameters*

* treeptr:   Pointer to json tree root
* text:      UTF-8 text
* usehash:   Use a hash to find duplicate node names

*Description*

Scan the JSON provided @text and parse it into the tree which is
pointed to by treeptr. If @*treeptr is NULL then a new tree is created.
If not NULL the memory storage is reused if the tree is closed or
text continues to be added if the tree is open.

If @text is NULL, the tree is forced closed and into an error state.

If @usehash is true, a new or recycled tree will obtain a name
lookup hash table. This is really only useful for large trees with
many duplicate names. If false, a recycled tree will lose the hash.

*Returns*
* 1 on success and the tree is parsed and closed
* 0 on error or tree is incomplete and errno is set to:
   - EAGAIN if tree incomplete and more text needed
   - EINVAL if invalid argument
   - EILSEQ if invalid JSON sequence (reason available)
   - ENOMEM if out of memory
  On ENOMEM, all storage will be freed and *treeptr is set to NULL

<hr />

#### ltjson_free(treeptr) - Free up all memory associate with tree
*Parameters*
* treeptr:   Pointer to valid tree

*Returns*
* 1 on success, writing NULL to *treeptr
* 0 and errno set to EINVAL if tree is not valid
<hr />


#### ltjson_lasterror(tree) - Describe the last error that occurred
*Parameters*
* tree:  Valid tree

*Returns*
* A pointer to a constant string describing the error
<hr />


#### ltjson_display(rnode) - Display the contents of a JSON subtree
*Parameters*
* @rnode:  Node to act as display root

*Description*
  
Display the JSON tree rooted at rnode, which can be an entire
subtree or just one node. The tree must be valid and closed to
do this and the routine walks back to the root to check.

*Returns*
* 1 on success
* 0 if tree is not valid/closed and sets errno (EINVAL)


### Statistics

#### ltjson_memstat(tree, stats, nents) - get memory usage statistics
*Parameters*
* tree:  Valid tree
* stats: Pointer to an array of ints which is filled
* nents: Number of entries in stats

*Description*

Tree does not have to be closed. No changes are made

*Returns*
* the number of stats placed in stats array
* 0 if tree/stats/nents not valid and sets errno (EINVAL)
<hr />


#### ltjson_statstring(index) - return statistic description string
*Parameters*
* index:  Valid index
 
*Returns*
* a const char string description of the statistic
* NULL if index is invalid (errno to ERANGE)
<hr />


#### ltjson_statdump(tree) - print out memory usage statistics
*Parameters*
* tree:  Valid tree

*Description*

Tree does not have to be closed. No changes are made

### Search and sort

#### ltjson_pathrefer(tree, path, nodeptr, nnodes) - Search for nodes
*Parameters*
* tree:      Valid closed and finalised (no error state) tree
* path:      Reference path expression
* nodeptr:   Pointer to nodestore for the answer
* nnodes:    Number of available nodes in @nodeptr

*Description*

Search the tree for the items specified in path and store any matches
found into the @nodeptr node array up to a max of @nnodes.

The reference path is an expression that must start with a / to
represent the root, followed by / separated object member or array
element references. Use [] to represent array offsets:

    /phoneNumbers/type
    /phoneNumbers[1]/type
    /[3]/store/book
    
An array offset can be left out or represented by [] or [*] to
denote "all elements" in the array. The offset is 0 based.
If an array is last item in a path, the array is returned, not
all the elements of the array (as for other parts of the path).

As, somewhat unfortunately, a name of "" is strictly allowed
under the json specification, the use of the illegal utf-8
sequence 0xFF is used to represent an empty name in a path.

*Returns*
* Number of matches found (not stored) on success or
* 0 on failure and, if an error, sets errno to one of
    - EINVAL if tree is not valid, closed or finalised
    - EILSEQ if path expression is not understood
    - ERANGE if path is too long
<hr />


#### ltjson_sort(snode, compar, parg) - Sort an object/array node
*Parameters*
* snode:  Node whose contents to sort
* compar: Pointer to function to determine order
* parg:   Optional argument pointer for any private data needed

*Description*

The compar function takes two ltjson nodes which are compared to
return the same codes as qsort and strcmp (ie: -1, 0, +1). The
function also takes two extra parameters, the jsontree itself is
the first and the optional private argument pointer is the second.

To get the jsontree itself, this routine walks back to the root
via the ancnode links.

*Returns*
* 1 on success
* 0 on error and sets errno (EINVAL)
<hr />


#### ltjson_search(rnode, name, fromnode, flags) - Search tree for name
*Parameters*
* rnode:     Subtree within which to search
* name:      Object member name
* fromnode:  Optional starting point
* flags:     Optional search flags

*Description*

Search through the JSON tree rooted at @rnode for the member name
@name, optionally resuming a previous search from the point after
the node @fromnode.

Flags supported are: LTJSON_SEARCH_NAMEISHASH to denote that the
name is one retrieved using ltjson_get_hashstring.

This routine does not check if @objnode is part of a closed tree.

*Returns*
* Pointer to the matched node on success
* NULL on failure with errno is set to:
    - EINVAL if passed null parameters
    - EPERM  if rnode is not an object/array
    - 0      if entry not found (not really an error)


### Utility functions

#### ltjson_promote(rnode, name) - Promote object member to first
*Parameters*
* rnode: Subtree within which to promote
* name:  Name of the object member to promote

*Description*

Traverse the JSON tree rooted at the subtree @rnode and promote
object members so that the member is listed first in that object.
This routine checks that rnode is part of a closed tree.

*Returns*
* 1 on success
* 0 on error and sets errno:
    - EINVAL if tree is not closed or name is null
    - EPERM  if rnode is not an object or array
    - ENOENT if no names were found to promote
<hr />


#### ltjson_get_hashstring(tree, name) - Lookup a name in the hash table
*Parameters*
* tree:  Valid closed tree
* name:  An object member name

*Returns*
* Pointer to constant string on success
* NULL on failure with errno is set to:
    - EINVAL if tree is not valid/closed etc
    - ENOENT if tree has no hash table
    - 0      if entry not found (not really an error)
<hr />


#### ltjson_mksearch(tree, name, flagsp) - Create a searchable name
*Parameters*
* tree:   Valid closed tree
* name:   An object member name
* flagsp: Pointer to search flags

*Description*

This is really just a helper function to create a search name without
messing around figuring out if the tree is hashed or not. It fetches
a hash value for name if the tree is hashed otherwise returns a pointer
to the name passed in. If that name is NULL, this function will return
the empty string. The flags pointer is twiddled to add or remove the
LTJSON_SEARCH_NAMEISHASH flag.

*Returns*
* Pointer to constant string (will not fail). However, still sets errno to reflect issues:
    - EINVAL if tree is not valid/closed/etc
    - ENOENT if tree is hashed and name is not there
    - 0      if returned string is good for searching
<hr />


#### ltjson_get_member(objnode, name, flags) - Retrieve object member
*Parameters*
* objnode: A pointer to an object node
* name:    An object member name
* flags:   Optional search flags

*Description*

Look through the object pointed to by @objnode for the member @name.
Flags supported are: LTJSON_SEARCH_NAMEISHASH to denote that the
name is one retrieved using ltjson_get_hashstring. This function does
not recurse down a tree, just hops from node to node within the object.

This routine does not check if @objnode is part of a closed tree.

*Returns*
* Pointer to the matched node on success
* NULL on failure with errno is set to:
    - EINVAL if passed null parameters
    - EPERM  if objnode is not an object
    - 0      if entry not found (not really an error)


### Adding

#### ltjson_addnode_after(tree, anode, ntype, name, sval) - Add a node after
*Parameters*
* tree:   Valid closed tree
* anode:  Node after which to insert the new node
* ntype:  New node type
* name:   New node name, if applicable
* sval:   New node string value, if applicable

*Description*

Insert a new node after @anode with the type @ntype. If anode's
ancestor node is an object, name is required. The ancestor of anode
must be an array or object and cannot be the root of the tree.

If the ntype is LTJSON_NTYPE_STRING then sval is used, if supplied,
to set the new node's string value. sval may be null or "" in which
case the value is a local static equal to "". You may then set sval
to another value.

For ntypes of integer and float, you need to set the value yourself.

*Returns*
* Pointer to the matched node on success
* NULL on failure with errno is set to:
    -  EINVAL if passed incorrect parameters
    -  ERANGE if ntype out of range for a node
    -  EPERM  if anode's ancestor is not an object or array
    -  ENOMEM if out of memory during node creation (tree remains)
<hr />


#### ltjson_addnode_under(tree, oanode, ntype, name, sval) - Add a node under
*Parameters*
* tree:   Valid closed tree
* oanode: Object or array node under which to insert the new node
* ntype:  New node type
* name:   New node name, if applicable
* sval:   New node string value, if applicable

*Description*

Insert a new node beneath @oanode with the type @ntype. If oanode
is an object, name is required. oanode must be an array or object
and can be the root of the tree.

If the ntype is LTJSON_NTYPE_STRING then sval is used, if supplied,
to set the new node's string value. sval may be null or "" in which
case the value is a local static equal to "". You may then set sval
to another value.

For ntypes of integer and float, you need to set the value yourself.

*Returns*
* Pointer to the matched node on success
* NULL on failure with errno is set to:
        - EINVAL if passed incorrect parameters
        - ERANGE if ntype out of range for a node
        - EPERM  if oanode is not an object or array
        - ENOMEM if out of memory during node creation (tree remains)

