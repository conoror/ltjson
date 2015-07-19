#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "ltjson.h"


static ltjson_node_t *jsontree = 0;

int dump_file(char *filename)
{
    int ret;
    FILE *fp;
    unsigned char buffer[64];

    fp = fopen(filename, "r");
    if (!fp)
    {
        perror("Open fail");
        return -1;
    }

    while ((ret = fread(buffer, 1, sizeof(buffer) - 1, fp)) > 0)
    {
        buffer[ret] = 0;

        ret = ltjson_parse(&jsontree, buffer);

        if (ret == -EAGAIN)
        {
            printf("Parse returns eagain. Around we go again\n");
            ret = ltjson_memory(jsontree);
            if (ret > 0)
                printf("Tree memory usage = %d bytes\n", ret);
        }
        else if (ret < 0)
        {
            printf("Error string = %s\n", strerror(-ret));

            if (ret == -EILSEQ)
                printf("Further info: %s\n", ltjson_lasterror(jsontree));
            fclose(fp);
            return -1;
        }
    }
    fclose(fp);

    if (ret == -EAGAIN)
    {
        printf("Tree was never closed.\n");
        return -1;
    }

    ret = ltjson_memory(jsontree);
    if (ret > 0)
        printf("Final tree memory usage = %d bytes\n", ret);

    ltjson_display(jsontree, 0);

    return 0;
}


int main()
{
    ltjson_node_t *searchnode[10];
    int ret;

    printf("Testing of ltjson library...\n");

    if (dump_file("test.txt"))
        exit(1);

    searchnode[0] = NULL;

    do {
        ret = ltjson_findname(jsontree, "number", searchnode);

        if (ret > 0)
        {
            printf("found!\n");
            printf("%s = ", searchnode[0]->name);
            if (searchnode[0]->ntype == LTJSON_STRING)
                printf("%s\n", searchnode[0]->val.vstr);
            else
                printf("????\n");
        }
        else if (ret < 0)
        {
            printf("Search returns error\n");
            break;
        }

    } while (searchnode[0]);

    printf("testing path refer\n");
    ret = ltjson_pathrefer(jsontree, "/phoneNumbers[]", searchnode, 10);

    printf("ltjson_pathrefer returns %d\n", ret);

    if (ret > 0)
    {
        int i;

        for (i = 0; i < ret; i++)
        {
            printf("-- Match --\n");
            ltjson_display(jsontree, searchnode[i]);
        }
    }


    if (dump_file("test1.txt"))
        exit(1);

    ret = ltjson_pathrefer(jsontree, "/store/book/title", searchnode, 10);

    if (ret > 0)
    {
        int i;

        for (i = 0; i < ret; i++)
        {
            printf("-- Match --\n");
            ltjson_display(jsontree, searchnode[i]);
        }
    }


    ret = ltjson_free(&jsontree);
    printf("return from free = %s\n", strerror(-ret));

    return 0;
}

