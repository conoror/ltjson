#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "ltjson.h"


int main()
{
    ltjson_node_t *jsontree = 0;
    ltjson_node_t *searchnode;
    int ret;
    FILE *fp;
    unsigned char buffer[256];

    printf("Testing of ltjson library...\n");

    fp = fopen("test.txt", "r");
    if (!fp)
    {
        perror("Open fail");
        exit(1);
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
            exit(1);
        }
    }
    fclose(fp);

    if (ret == -EAGAIN)
    {
        printf("Tree was never closed.\n");
        exit(1);
    }

    ret = ltjson_memory(jsontree);
    if (ret > 0)
        printf("Final tree memory usage = %d bytes\n", ret);

    ltjson_display(jsontree);

    searchnode = NULL;

    do {
        ret = ltjson_findname(jsontree, "number", &searchnode);

        if (ret == 0)
        {
            if (searchnode)
            {
                printf("found!\n");
                printf("%s = ", searchnode->name);
                if (searchnode->ntype == LTJSON_STRING)
                    printf("%s\n", searchnode->val.vstr);
                else
                    printf("????\n");
            }
        }
        else
        {
            printf("Search returns error\n");
            break;
        }

    } while (searchnode);

    ret = ltjson_free(&jsontree);
    printf("return from free = %s\n", strerror(-ret));

    return 0;
}

