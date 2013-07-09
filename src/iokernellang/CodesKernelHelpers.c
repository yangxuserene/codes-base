#include "CodesKernelHelpers.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>

#define CK_LINE_LIMIT 8192
#define CL_DEFAULT_GID 0

char * code_kernel_helpers_kinstToStr(int inst)
{
    switch(inst)
    {
        case WRITEAT:
            return "WRITEAT";
        case GETGROUPRANK:
            return "GETGROUPRANK";
        case GETGROUPSIZE:
            return "GETGROUPSIZE";
        case CLOSE:
            return "CLOSE";
        case OPEN:
            return "OPEN";
        case SYNC:
            return "SYNC";
        case SLEEP:
            return "SLEEP";
        case EXIT:
            return "EXIT";
        case DELETE:
            return "DELETE";
        default:
            return "UNKNOWN";
    };
    
    return "UNKNOWN";
}

char * code_kernel_helpers_cleventToStr(int inst)
{
    switch(inst)
    {
        case CL_WRITEAT:
            return "CL_WRITEAT";
        case CL_GETRANK:
            return "CL_GETRANK";
        case CL_GETSIZE:
            return "CL_GETSIZE";
        case CL_CLOSE:
            return "CL_CLOSE";
        case CL_OPEN:
            return "CL_OPEN";
        case CL_SYNC:
            return "CL_SYNC";
        case CL_SLEEP:
            return "CL_SLEEP";
        case CL_EXIT:
            return "CL_EXIT";
        case CL_DELETE:
            return "CL_DELETE";
        default:
            return "CL_UNKNOWN";
    };
    
    return "CL_UNKNOWN";
}

static int convertKLInstToEvent(int inst)
{
    switch(inst)
    {
        case WRITEAT:
            return CL_WRITEAT;
        case GETGROUPRANK:
            return CL_GETRANK;
        case GETGROUPSIZE:
            return CL_GETSIZE;
        case CLOSE:
            return CL_CLOSE;
        case OPEN:
            return CL_OPEN;
        case SYNC:
            return CL_SYNC;
        case SLEEP:
            return CL_SLEEP;
        case EXIT:
            return CL_EXIT;
        case DELETE:
            return CL_DELETE;
        default:
            return CL_UNKNOWN;
    };

    return CL_UNKNOWN;
}

static void codes_kernel_helper_parse_cf(char * io_kernel_path, char *
        io_kernel_def_path, char * io_kernel_meta_path, int task_rank, app_cf_info_t * task_info)
{
       int foundit = 0;
       char line[CK_LINE_LIMIT];
       FILE * ikmp = NULL;

       /* open the config file */
       ikmp = fopen(io_kernel_meta_path, "r");
       if(ikmp == NULL)
       {
           fprintf(stderr, "%s:%i could not open meta file (%s)... bail\n", __func__,
                   __LINE__, io_kernel_meta_path);
           exit(1);
       }

       /* for each line in the config file */
       while(fgets(line, CK_LINE_LIMIT, ikmp) != NULL)
       {
               char * token = NULL;
               int min = 0;
               int max = 0;
               int gid = 0;
               char * ctx = NULL;

               /* parse the first element... the gid */
               token = strtok_r(line, " \n", &ctx);
               if(token)
                   gid = atoi(token);

               if(gid == CL_DEFAULT_GID)
               {
                   fprintf(stderr, "%s:%i incorrect GID detected in kernel meta\
                           file. Cannot use the reserved GID\
                           CL_DEFAULT_GID(%i)\n", __func__, __LINE__,
                           CL_DEFAULT_GID);
               }

               /* parse the second element... min rank */
               token = strtok_r(NULL, " \n", &ctx);
               if(token)
                       min = atoi(token);

               /* parse the third element... max rank */
               token = strtok_r(NULL, " \n", &ctx);
               if(token)
                       max = atoi(token);

               /* parse the last element... kernel path */
               token = strtok_r(NULL, " \n", &ctx);
               if(token)
                       strcpy(io_kernel_path, token);

               /* if our rank is on this range... end processing of the config
                * file */
               if(task_rank >= min && task_rank <= max)
               {
                       task_info->gid = gid;
                       task_info->min = min;
                       task_info->max = max;
                       task_info->lrank = task_rank - min;
                       task_info->num_lrank = max - min + 1;

                       foundit = 1;

                       break;
               }
       }

       /* close the config file */
       fclose(ikmp);

       /* if we did not find the config file, set it to the default */
       if(foundit == 0)
       {
               /* default kernel */
               strcpy(io_kernel_path, io_kernel_def_path);

               /* default gid and task attrs */
               /* TODO can we detect the gaps instead of -1 */
               task_info->gid = CL_DEFAULT_GID;
               task_info->min = -1;
               task_info->max = -1;
               task_info->lrank = -1;
               task_info->num_lrank = -1;
       }

       return;
}

int codes_kernel_helper_parse_input(CodesIOKernel_pstate * ps, CodesIOKernelContext * c,
                          codeslang_inst * inst)
{
    int yychar;
    int status;
    int codes_inst = CL_NOOP;

    /* swap in the symbol table for the current LP context */
    CodesIOKernelScannerSetSymTable(c);

    do
    {
        c->locval = CodesIOKernel_get_lloc(*((yyscan_t *)c->scanner_));
        yychar = CodesIOKernel_lex(c->lval, c->locval, c->scanner_);
        c->locval = NULL;
      
        /* for each instructions */
        switch(yychar)
        {
            /* for each instrunction that triggers a simulator event */
            case WRITEAT:
            case GETGROUPRANK:
            case GETGROUPSIZE:
            case CLOSE:
            case OPEN:
            case SYNC:
            case SLEEP:
            case EXIT:
            case DELETE:
            {
                c->inst_ready = 0;
                status = CodesIOKernel_push_parse(ps, yychar, c->lval, c->locval, c);
                codes_inst = convertKLInstToEvent(yychar);
		break;
            }
            /* not a simulator event */
            default:
            {
                status = CodesIOKernel_push_parse(ps, yychar, c->lval, c->locval, c);
		break;
            }
        };

        /* if the instruction is ready (all preq data is ready ) */
        if(c->inst_ready)
        {
            /* reset instruction ready state */
            c->inst_ready = 0;

            switch(codes_inst)
            {
                case CL_GETRANK:
                case CL_GETSIZE:
                case CL_WRITEAT:
                case CL_OPEN:
                case CL_CLOSE:
                case CL_SYNC:
                case CL_EXIT:
                case CL_DELETE:
                case CL_SLEEP:
                {
                    int i = 0;

                    inst->event_type = codes_inst;
                    inst->num_var = c->var[0];
                    for(i = 0 ; i < inst->num_var ; i++)
                    {
                        inst->var[i] = c->var[i + 1];
                    }

		    break;
                }
                /* we don't need to store the instructions args */
                default:
                {
                    continue;
                }
            };
            
	    /* we have all of the data for the instruction... bail */
            break;
        }
    /* while there are more instructions to parse in the stream */
    }while(status == YYPUSH_MORE);

    /* return the simulator instruction */
    return codes_inst;
}

int codes_kernel_helper_bootstrap(char * io_kernel_path, char *
        io_kernel_def_path, char * io_kernel_meta_path,
        int rank, CodesIOKernelContext * c,
        CodesIOKernel_pstate ** ps, app_cf_info_t * task_info,
        codeslang_inst * next_event)
{
    int t = CL_NOOP;
    int ret = 0;
    char * kbuffer = NULL;
    int fd = 0;
    off_t ksize = 0;
    struct stat info;

    /* get the kernel from the file */
    codes_kernel_helper_parse_cf(io_kernel_path, io_kernel_def_path,
            io_kernel_meta_path, rank, task_info);

    /* stat the kernel file */
    ret = stat(io_kernel_path, &info);
    if(ret != 0)
    {
        fprintf(stderr, "%s:%i could not stat kernel file (%s), exiting\n",
                __func__, __LINE__, io_kernel_path);
        perror("stat() error: ");
        exit(1);
    }

    /* get the size of the file */
    ksize = info.st_size;

    /* allocate a buffer for the kernel */
    kbuffer = (char *)malloc(sizeof(char) * ksize);

    /* get data from the kernel file */
    fd = open(io_kernel_path, O_RDONLY);
    ret = pread(fd, kbuffer, ksize, 0);
    close(fd);

    /* init the scanner */
    CodesIOKernelScannerInit(c);

    if(ret <= ksize)
    {
        CodesIOKernel__scan_string(kbuffer, c->scanner_);
	
	*ps = CodesIOKernel_pstate_new();

        /* get the first instruction */
        t = codes_kernel_helper_parse_input(*ps, c, next_event);
    }
    else
    {
        fprintf(stderr, "not enough buffer space... bail\n");
        exit(1);
    }

    /* cleanup */
    free(kbuffer);

    return t;
}
