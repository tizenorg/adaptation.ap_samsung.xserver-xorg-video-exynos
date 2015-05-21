#include <stdio.h>
#include <regex.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include "hwc-sample.h"

void*
thread1_hwcsample (void *data)
{
    char str[100] = {"0", }, buf[10], name[10];
    int i = 0, coords[5] = {0, }, len = 0;
    char* temp = NULL;

    regex_t regex;
    int reti;
    regmatch_t pmatch[10];

    char *pattern =
            "-?(move|resize|focus|set)\x20?([0-9]+)?\x20?([0-9]+)?\x20?([0-9]+)?\x20?([0-9]+)?\x20?([0-9]+)?\x20?([0-9]+)?";
    /* Compile regular expression */
    reti = regcomp (&regex, pattern, REG_EXTENDED | REG_ICASE); //"GET.*HTTP"
    if (reti)
    {
        fprintf (stderr, "Could not compile regex\n");
        return NULL;
    }

    while (1)
    {
        temp = fgets (str, sizeof(str), stdin);
        if (!temp)
        {
            printf ("error while fgets call.\n");
            exit ( EXIT_FAILURE);
        }

        /* Execute regular expression */
        reti = regexec (&regex, str, 10, pmatch, 0);
        if (!reti)
        {
            /*find name of command*/
            len = pmatch[1].rm_eo - pmatch[1].rm_so;
            strncpy (name, str + pmatch[1].rm_so, len);
            name[len] = '\0';

            for (i = 0; i < 5; i++)
            {
                len = pmatch[2 + i].rm_eo - pmatch[2 + i].rm_so;
                strncpy (buf, str + pmatch[2 + i].rm_so, len);
                buf[len] = '\0';
                coords[i] = atoi (buf);
                printf ("coords[%d]=%d ,", i, coords[i]);
            }
            /*coords[]:
             * 0 - number of window
             * 1 - x coordinate
             * 2 - y coord
             * 3 - width
             * 4 - height
             */
            if (!strcmp (name, "focus"))
            {
                change_focus ();
                hwc_set ();
            }

            if (!strcmp (name, "set"))
            {
                ai.count = coords[0];
                hwc_set ();
            }

            if (!strcmp (name, "move"))
            {
                ai.dst[coords[0]].x = coords[1];
                ai.dst[coords[0]].y = coords[2];
                ai.wnd_to_change = coords[0];
                ai.hwc_set = 4;
                hwc_movew ();
            }
            if (!strcmp (name, "resize"))
            {
                ai.dst[coords[0]].x = coords[1];
                ai.dst[coords[0]].y = coords[2];
                ai.dst[coords[0]].width = coords[3];
                ai.dst[coords[0]].height = coords[4];
                ai.wnd_to_change = coords[0];
                ai.hwc_set = 5;
                hwc_resizew ();
            }

        }
        else if (reti == REG_NOMATCH)
        {
            puts ("No match");
        }
        else
        {
            regerror (reti, &regex, str, sizeof(str));
            fprintf (stderr, "Regex match failed: %s\n", str);
            exit (1);
        }

    }

    regfree (&regex);

    return NULL;
}
