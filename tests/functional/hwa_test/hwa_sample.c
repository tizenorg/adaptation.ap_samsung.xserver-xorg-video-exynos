
/* Standart headers */
#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <getopt.h>

/* X Server's headers */
#include <X11/Xlib.h>
#include <xcb/xcb.h>
#include <xcb/hwa.h>
#include <xcb/dri3.h>
#include <xcb/randr.h>

#include "hwa_sample.h"

void test_hwc( void );

xcb_connection_t *c;
xcb_generic_error_t *error;

xcb_randr_crtc_t randr_crtc = 0;
char con_name[20] = {0,};

int main( int argc, char **argv )
{
    int rez;
    int option_index;
    int enable;
    int angle;
    char *scale_params = NULL;
    const char* short_options = "ic:r:a:x:s:h:o:";

    const struct option long_options[] =
    {
        {"help",no_argument,NULL,'?'},
        {"info",no_argument,NULL,'i'},
        {"version",no_argument,NULL,'v'},
        {"cursore",required_argument,NULL,'c'},
        {"cursorr",required_argument,NULL,'r'},
        {"accessibility",required_argument,NULL,'a'},
        {"scale",required_argument,NULL,'x'},
        {"show",required_argument,NULL,'s'},
        {"hide",required_argument,NULL,'h'},
        {"output",required_argument,NULL,'o'},
        {NULL,0,NULL,0}
    };

    c = xcb_connect (NULL, NULL);

    while ( ( rez = getopt_long_only( argc,argv, short_options, long_options,&option_index ) ) != -1 )
    {
        switch(rez)
        {
            case '?':
                help();
                break;

            case 'i':
                extensions_info();
                break;

            case 'v':
                hwa_query_version();
                break;

            case 'c':
                enable = atoi(optarg);
                hwa_cursor_enable( enable );
                break;

            case 'r':
                angle = atoi(optarg);
                hwa_cursor_rotate( angle );
                break;

            case 'a':
                enable = atoi(optarg);
                hwa_accessibility( enable );

                break;

            case 'x':
                scale_params = (char *)calloc(30, sizeof( char ) );

                enable = 0;

                optind--;
                for( ;optind < argc; optind++)
                {
                    if ( 2 == optind )
                        enable = atoi(argv[optind]);
                    else
                        strcpy( scale_params, argv[optind] );
                }

                hwa_scale( enable, scale_params );
                free( scale_params );

                break;

            case 's':
                printf("show layer = %s\n",optarg);
                hwa_show_layer(atoi(optarg));

                break;

            case 'h':
                printf("hide layer= %s\n",optarg);
                hwa_hide_layer(atoi(optarg));
                break;

            /*
             * TODO
             * output selector need implement
             * */
            case 'o':
                printf("[%s] output chosen \n",optarg);
                strcpy( con_name, optarg);
                printf("con_name = [%s] \n",con_name);
                break;

            default:
                PRINT(4);
                printf("ERROR: found unknown option\n\n");

                help();
                break;
        }
    }

    xcb_disconnect(c);

    return 0;
}

void extensions_info( void )
{
    xcb_list_extensions_reply_t *rep;
    xcb_query_extension_reply_t *q_rep;

    xcb_list_extensions_cookie_t ext_cookie;
    xcb_query_extension_cookie_t query_c;

    ext_cookie = xcb_list_extensions( c );
    rep = xcb_list_extensions_reply( c, ext_cookie, &error );
    xcb_flush(c);


    /* List all Extensions which X Server supports */
    printf( "\n-----------------------------------\n");

    xcb_str_iterator_t str_iter = xcb_list_extensions_names_iterator( rep );

    printf( "+++ index %d\n+++ %d\n", str_iter.index, str_iter.rem );

    while (str_iter.rem)
    {
        printf("%d bytes: ", xcb_str_name_length(str_iter.data));
        fwrite(xcb_str_name(str_iter.data), 1, xcb_str_name_length(str_iter.data), stdout);
        printf("\n");
        fixed_xcb_str_next(&str_iter);
    }

    printf( "\n-----------------------------------\n");

    /* Check does X Server support HWA */
    query_c = xcb_query_extension( c, 3, "HWA" );
    q_rep = xcb_query_extension_reply( c, query_c, &error );

    printf( "\n-----------------------------------\n");
    printf( "Check HWA:\n"
            "present: %d\n"
            "major opcode: %d\n"
            "first event: %d\n"
            "first error: %d\n",
            q_rep->present,
            q_rep->major_opcode,
            q_rep->first_event,
            q_rep->first_error );

    /* TODO process errors */

    free(rep);
    free(q_rep);
    printf( "-----------------------------------\n");
}

void hwa_query_version( void )
{
    xcb_hwa_query_version_cookie_t hwa_version_cookie;
    xcb_hwa_query_version_reply_t *hwa_version_reply = NULL;

    /* 111 and 222 are just random numbers */
    hwa_version_cookie = xcb_hwa_query_version ( c, 111, 222 );
    xcb_flush(c);

    hwa_version_reply = xcb_hwa_query_version_reply( c, hwa_version_cookie, &error );

    if(hwa_version_reply)
    {
        printf( "\n-----------------------------------\n");
        printf("Query Version:\n");
        printf("hwa_query_version: Call was ok\nmajor_version=%d\nminor_version=%d \n",
                     hwa_version_reply->major_version,
                     hwa_version_reply->minor_version );
        printf( "-----------------------------------\n");

        free(hwa_version_reply);
    }
    else
    {
        printf("hwa_query_version: ERROR" );
    }


}

void hwa_cursor_enable( int enable )
{
    xcb_hwa_cursor_enable_cookie_t cursor_cookie;
    xcb_hwa_cursor_enable_reply_t *cursor_enb_reply = NULL;

    cursor_cookie = xcb_hwa_cursor_enable_unchecked( c, enable );
        xcb_flush(c);

    cursor_enb_reply = xcb_hwa_cursor_enable_reply( c,cursor_cookie, &error);

    if(cursor_enb_reply)
    {
        printf( "\n-----------------------------------\n");
        printf("Cursor Enable:\n");
        printf("hwa_cursor_enable: Call was ok\nreply.status=%d \n", cursor_enb_reply->status );
        printf( "-----------------------------------\n");

        free(cursor_enb_reply);
    }
    else
    {
        printf("hwa_cursor_enable: ERROR" );
        exit(1);
    }
}

void hwa_cursor_rotate( int angle )
{
    xcb_hwa_cursor_rotate_cookie_t cursor_rotate_cookie;
    xcb_hwa_cursor_rotate_reply_t *cursor_rot_reply = NULL;

    /*hwa_cursor_enable( ON );*/

    randr_crtc = (xcb_randr_crtc_t) get_crtc_via_randr("LVDS1");

    cursor_rotate_cookie = xcb_hwa_cursor_rotate_unchecked (c, randr_crtc, angle );
    xcb_flush(c);
    cursor_rot_reply = xcb_hwa_cursor_rotate_reply ( c,  cursor_rotate_cookie, &error);

    if(cursor_rot_reply)
    {
        printf( "\n-----------------------------------\n");
        printf("Cursor Rotate:\n");
        printf("hwa_cursor_rotate: Call was ok\nreply.status=%d, angle = %d \n ", cursor_rot_reply->status, angle );
        printf( "-----------------------------------\n");

        free(cursor_rot_reply);
    }
    else
    {
        printf("hwa_cursor_rotate: ERROR" );
    }
}

void hwa_accessibility( int enable )
{
    xcb_hwa_screen_invert_negative_cookie_t hwa_scrn_invert_cookie = {0,};
    xcb_hwa_screen_invert_negative_reply_t *hwa_scrn_invert_reply = NULL;

    int crtc = get_crtc_via_randr("LVDS1");
    if (-1 == crtc)
    {
        printf ("Didn't find randr crtc!! :\n");
        exit (0);
    }

    hwa_scrn_invert_cookie = xcb_hwa_screen_invert_negative( c, crtc, enable );
    xcb_flush(c);

    hwa_scrn_invert_reply = xcb_hwa_screen_invert_negative_reply( c, hwa_scrn_invert_cookie, &error );

    if(hwa_scrn_invert_reply)
    {
        printf( "\n-----------------------------------\n");
        printf("Accessibility:\n");
        printf("hwa_accessibility: Call was ok\nreply.status=%d \n", hwa_scrn_invert_reply->status );
        printf( "-----------------------------------\n");

        free(hwa_scrn_invert_reply);
    }
    else
    {
        printf("hwa_accessibility: ERROR" );
    }
}

void hwa_scale( int enable, char* scale_param )
{
    xcb_hwa_screen_scale_cookie_t  hwa_scale_cookie;
    xcb_hwa_screen_scale_reply_t  *hwa_scale_reply;

    int x = 0, y = 0, width = 0, height = 0;
    int crtc = -1;

    if( enable && !scale_param )
    {
    printf( "hwa_scale: ERROR, parameters was not specified\n");
    help();
    exit(1);
    }
    else if ( enable && scale_param )
    {
        int *xywh = pars_params( scale_param );

        printf("x=%d y=%d w=%d h=%d\n",
                    xywh[0],
                    xywh[1],
                    xywh[2],
                    xywh[3] );

        int i = 0;
        for( ; i < NUM_PARAMS; i++ )
        {
            if( 0 > xywh[i] )
            {
            printf( "hwa_scale: ERROR, At least four parameters must be specified herein\n"
                            "                  Parameters must not to be negative\n");
            free(xywh);
            exit(1);
            }
        }

        x = xywh[0];
        y = xywh[1];
        width = xywh[2];
        height = xywh[3];

        free(xywh);
    }
    else if( 0 == enable )
    {
        x = 0;
        y = 0;
        width = 0;
        height = 0;
    }

    crtc = get_crtc_via_randr("LVDS1");
    if (-1 == crtc)
    {
        printf ("Didn't find randr crtc!! :\n");
        exit (0);
    }

    hwa_scale_cookie = xcb_hwa_screen_scale( c, crtc , enable, x, y, width, height );
    hwa_scale_reply = xcb_hwa_screen_scale_reply ( c, hwa_scale_cookie, &error );

    if(hwa_scale_reply)
    {
        printf( "\n-----------------------------------\n");
        printf("Scale:\n");
        printf("hwa_scale: Call was ok\nreply.status=%d \n", hwa_scale_reply->status );
        printf( "-----------------------------------\n");

        free(hwa_scale_reply);
    }
    else
    {
        printf("hwa_scale: ERROR" );
    }
}

void hwa_show_layer(int layer)
{
    randr_crtc = (xcb_randr_crtc_t) get_crtc_via_randr("LVDS1");
    if (-1 == randr_crtc)
    {
        printf ("Didn't find randr crtc!! :\n");
        exit (0);
    }

    printf ("<=== Overlay Layer Show  ==> \n");
    printf ("<=== Randr crtc=%d, layer=%d  ==> \n", randr_crtc, layer);
    xcb_hwa_overlay_show_layer (c, (xcb_randr_crtc_t) randr_crtc, layer);
    xcb_flush (c);

}

void hwa_hide_layer( int layer )
{
    randr_crtc = (xcb_randr_crtc_t) get_crtc_via_randr("LVDS1");
    if (-1 == randr_crtc)
    {
        printf ("Didn't find randr crtc!! :\n");
        exit (0);
    }

    printf ("<=== Overlay Layer Hide  ==> \n");
    printf ("<=== Randr crtc=%d, layer=%d  ==> \n", randr_crtc, layer);
    xcb_hwa_overlay_hide_layer (c, (xcb_randr_crtc_t) randr_crtc, layer);
    xcb_flush (c);

}

/*
 * output = LVDS, HDMI, Virtual
 *
 * */
int get_crtc_via_randr( char* output_to_find )
{
    xcb_randr_get_screen_resources_cookie_t screenresc_cookie;
    xcb_randr_get_screen_resources_reply_t* screenresc_reply = NULL;
    xcb_randr_crtc_t randrcrtc_first;
    int i = 0, k = 0;

    /* Get the first X screen */
    xcb_screen_t* x_first_screen = xcb_setup_roots_iterator( xcb_get_setup(c)).data;

    xcb_window_t x_window_dummy = xcb_generate_id(c);

    /* Create dummy X window */
    xcb_create_window(c, 0, x_window_dummy, x_first_screen->root, 0, 0, 1, 1, 0, 0, 0, 0, 0);

    //Flush pending requests to the X server
    xcb_flush(c);

    //Send a request for screen resources to the X server
    screenresc_cookie = xcb_randr_get_screen_resources( c, x_window_dummy );

    /* Take reply from Xserver */
    screenresc_reply = xcb_randr_get_screen_resources_reply( c, screenresc_cookie, 0);

    if(screenresc_reply)
    {

        /* Take each crtc, and create screen for each of them*/
        xcb_randr_crtc_t *randr_crtcs = xcb_randr_get_screen_resources_crtcs(screenresc_reply);

        for (i = 0; i < screenresc_reply->num_crtcs; ++i)
            {
                /* We take information of the output crtc */
                xcb_randr_get_crtc_info_cookie_t crtc_info_cookie =
                        xcb_randr_get_crtc_info (c, randr_crtcs [i], XCB_CURRENT_TIME);
                xcb_randr_get_crtc_info_reply_t *crtc_randr_info =
                        xcb_randr_get_crtc_info_reply (c, crtc_info_cookie, NULL);

                /* Ignore CRTC if it hasn't OUTPUT */
                if (!xcb_randr_get_crtc_info_outputs_length (crtc_randr_info))
                    continue;

                xcb_randr_output_t *randr_outputs = xcb_randr_get_crtc_info_outputs (crtc_randr_info);

                for (k = 0; k < xcb_randr_get_crtc_info_outputs_length (crtc_randr_info); ++k)
                {
                    xcb_randr_get_output_info_cookie_t output_info_cookie =
                                 xcb_randr_get_output_info (c, randr_outputs [k], XCB_CURRENT_TIME);
                    xcb_randr_get_output_info_reply_t  *output_randr_info =
                                 xcb_randr_get_output_info_reply (c, output_info_cookie, NULL);

                    if (!output_randr_info)
                        continue;

                    int length = xcb_randr_get_output_info_name_length (output_randr_info);

                    /* Add null to name, it doesn't '\0' terminated  */
                    char *outputname = malloc ((length + 1) * sizeof(char *));
                    memcpy (outputname, xcb_randr_get_output_info_name (output_randr_info), length);
                    outputname [length] = '\0';

                    /*
                    printf ("<-Screen[%i] INFO: (%s):\n", j, name);
                    printf (" width_mm: %i\n", output_info_r->mm_width);
                    printf (" height_mm: %i\n", output_info_r->mm_height);
                    */

                    if (!strcmp (outputname, output_to_find)){
                        randrcrtc_first = output_randr_info->crtc;
                        free (output_randr_info);
                        free (crtc_randr_info);
                        printf ("<-RANDR CRTCID= %d\n",randrcrtc_first );
                        return randrcrtc_first;
                    }

                    free (output_randr_info);
                }

                free (crtc_randr_info);
            }

        free (screenresc_reply);
    }
    else
        return -1;

    return -1;
}

int* pars_params( char* scale_param )
{
    int indx = 0;
    int i = 0;
    int* xywh = (int*)calloc( NUM_PARAMS, sizeof( int ) );
    char *delimeter = ":";

    for( ; i < NUM_PARAMS; i++ )
        xywh[i] = -1;

    char *token = (char *)strtok( scale_param, delimeter );

    while( token != NULL )
    {
        xywh[indx++] = atoi(token);
        token = (char *)strtok( NULL, delimeter );
    }

    return xywh;
}





